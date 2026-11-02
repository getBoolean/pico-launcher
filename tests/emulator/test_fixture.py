"""Fixture preparation checks; guest card access is qualified separately."""

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("build_fixture", HERE / "build_fixture.py")
fixture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fixture)


class FixtureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "repo"
        for theme in ("material", "raspberry"):
            path = self.repo / "_pico/themes" / theme / "theme.json"
            path.parent.mkdir(parents=True)
            path.write_text('{}')
        header = self.repo / "common/picoLoader7.h"
        header.parent.mkdir()
        header.write_text("#define PICO_LOADER_API_VERSION 3\n")
        self.children = {}
        for child in ("A", "B"):
            path = self.root / f"child_{child}.nds"
            path.write_bytes((child.encode() * 1024))
            self.children[child] = path
        self.archive = self.root / "loader.zip"
        self.write_loader()
        self.manifest = json.loads((HERE / "fixtures/manifest.json").read_text())
        self.manifest["stress"] = {"breadth": 2, "depth": 2}

    def write_loader(self, api=3, omit=None):
        with zipfile.ZipFile(self.archive, "w") as archive:
            for name in ("picoLoader7.bin", "picoLoader9.bin", "aplist.bin",
                         "savelist.bin", "patchlist.bin"):
                if name != omit:
                    data = bytearray(32)
                    if name == "picoLoader7.bin":
                        data[10:12] = api.to_bytes(2, "little")
                    archive.writestr(name, data)

    def prepare(self, destination):
        digest = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        return fixture.prepare_tree(self.repo, self.archive, self.children,
                                    destination, self.manifest, digest)

    def test_complete_tree_has_seeded_settings_assets_loader_and_expected_roms(self):
        tree = self.root / "card"
        identity = self.prepare(tree)
        self.assertEqual(json.loads((tree / "_pico/settings.json").read_text()),
                         self.manifest["settings"])
        for theme in ("material", "raspberry"):
            self.assertTrue((tree / f"_pico/themes/{theme}/theme.json").is_file())
        for name in fixture.LOADER_FILES:
            self.assertTrue((tree / "_pico" / name).is_file())
        self.assertEqual((tree / "games/A/game.nds").read_bytes(), b"A" * 1024)
        self.assertEqual((tree / "games/B/game.nds").read_bytes(), b"B" * 1024)
        self.assertEqual(len(list((tree / "games/stress").rglob("d*"))), 6)
        self.assertEqual(identity["initial_state"]["query"], "")
        for case in self.manifest["search_expectations"]:
            for relative in case["paths"]:
                self.assertTrue((tree / relative.lstrip("/")).is_file())
        self.assertEqual((tree / "games/A/game.nds").stat().st_mtime,
                         self.manifest["timestamp"])

    def test_fresh_trees_have_same_identity_and_independent_writes(self):
        first, second = self.root / "first", self.root / "second"
        a, b = self.prepare(first), self.prepare(second)
        self.assertEqual(a, b)
        (first / "_pico/settings.json").write_text('{"theme":"raspberry"}')
        self.assertEqual(json.loads((second / "_pico/settings.json").read_text()),
                         self.manifest["settings"])

    def test_bad_archive_hash_fails_before_preparing_tree(self):
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            fixture.prepare_tree(self.repo, self.archive, self.children,
                                 self.root / "card", self.manifest, "0" * 64)
        self.assertFalse((self.root / "card").exists())

    def test_missing_loader_file_fails_before_preparing_tree(self):
        self.write_loader(omit="patchlist.bin")
        with self.assertRaisesRegex(ValueError, "patchlist.bin"):
            self.prepare(self.root / "card")
        self.assertFalse((self.root / "card").exists())

    def test_incompatible_loader_api_fails_before_preparing_tree(self):
        self.write_loader(api=2)
        with self.assertRaisesRegex(ValueError, "API"):
            self.prepare(self.root / "card")
        self.assertFalse((self.root / "card").exists())


if __name__ == "__main__":
    unittest.main()
