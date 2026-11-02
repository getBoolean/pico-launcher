"""Installer contracts exercised with local package archives."""

import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tarfile
import tempfile
import unittest
import urllib.parse
import urllib.request
import zipfile


REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("packages", REPO / "tools/emulator/packages.py")
packages = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(packages)


def digest(data):
    return hashlib.sha256(data).hexdigest()


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.version = "1.1.0-pico.1"
        self.revision = "a" * 40

    def archive(self, name, root, files, windows):
        path = self.root / name
        if windows:
            with zipfile.ZipFile(path, "w") as archive:
                for relative, data in files.items():
                    archive.writestr(f"{root}/{relative}", data)
        else:
            with tarfile.open(path, "w:gz") as archive:
                for relative, data in files.items():
                    member = tarfile.TarInfo(f"{root}/{relative}")
                    member.size = len(data)
                    member.mode = 0o755 if relative.endswith("mcp") or relative.endswith("bridge") or relative == "desmume-cli" else 0o644
                    archive.addfile(member, io.BytesIO(data))
        return {"url": path.as_uri(), "sha256": digest(path.read_bytes()), "root": root}

    def lock(self, platform="windows", omit=None, wrong_revision=False):
        windows = platform == "windows"
        suffix = ".exe" if windows else ""
        binaries = {f"emucap-mcp{suffix}": b"core", f"emucap-desmume-nds-bridge{suffix}": b"bridge"}
        core = {f"target/release/{name}": data for name, data in binaries.items()}
        core["CORE-BUILD.json"] = json.dumps({
            "version": self.version,
            "source_revision": "b" * 40 if wrong_revision else self.revision,
            "binaries": {name: digest(data) for name, data in binaries.items()},
        }).encode()
        core["NOTICE"] = b"core license"
        native = {f"desmume-cli{suffix}": b"native", "licenses/COPYING": b"native license"}
        if windows:
            native["SDL2.dll"] = b"runtime dependency"
        native_manifest = {"files": {name: {"sha256": digest(data), "bytes": len(data)} for name, data in native.items()}}
        native["NATIVE-PACKAGE.json"] = json.dumps(native_manifest).encode()
        if omit:
            del native[omit]
        extension = "zip" if windows else "tar.gz"
        return {"format": 1,
                "core": {"version": self.version, "source_revision": self.revision,
                         platform: self.archive(f"core.{extension}", "core-root", core, windows)},
                "desmume": {"source_revision": "c" * 40,
                            platform: self.archive(f"native.{extension}", "native-root", native, windows)}}

    def test_installs_complete_layout_and_reports_paths_on_both_hosts(self):
        for platform in ("windows", "linux"):
            with self.subTest(platform=platform):
                lock = self.lock(platform)
                destination = self.root / platform
                result = packages.install(lock, platform, destination)
                self.assertEqual(result["core_source_revision"], self.revision)
                self.assertEqual(result["version"], self.version)
                for key in ("core_executable", "bridge_executable", "desmume_executable"):
                    self.assertTrue(Path(result[key]).is_file(), key)
                self.assertEqual((destination / "core-root/NOTICE").read_bytes(), b"core license")
                self.assertEqual((destination / "native-root/licenses/COPYING").read_bytes(), b"native license")
                self.assertEqual(json.loads((destination / "install.json").read_text(encoding="utf-8")), result)
                if platform == "windows":
                    self.assertEqual((destination / "native-root/SDL2.dll").read_bytes(), b"runtime dependency")
                elif os.name == "posix":
                    self.assertTrue(Path(result["core_executable"]).stat().st_mode & 0o111)

    def test_bad_archive_hash_fails_before_destination_is_created(self):
        lock = self.lock()
        lock["desmume"]["windows"]["sha256"] = "0" * 64
        destination = self.root / "install"
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            packages.install(lock, "windows", destination)
        self.assertFalse(destination.exists())

    def test_repeat_install_is_identical_and_preserves_unrelated_files(self):
        lock = self.lock()
        config = self.root / "config.toml"
        config.write_text('[mcp_servers.existing]\ncommand = "existing"\n', encoding="utf-8")
        before = config.read_bytes()
        destination = self.root / "install"
        first = packages.install(lock, "windows", destination)
        (destination / "notes.txt").write_text("owner note", encoding="utf-8")
        second = packages.install(lock, "windows", destination)
        self.assertEqual(first, second)
        self.assertEqual(config.read_bytes(), before)
        self.assertEqual((destination / "notes.txt").read_text(encoding="utf-8"), "owner note")

    def test_missing_runtime_dependency_is_named_and_not_installed(self):
        lock = self.lock(omit="SDL2.dll")
        destination = self.root / "install"
        with self.assertRaisesRegex(ValueError, "SDL2.dll"):
            packages.install(lock, "windows", destination)
        self.assertFalse(destination.exists())

    def test_missing_core_executable_is_named_and_not_installed(self):
        lock = self.lock()
        archive = Path(urllib.request.url2pathname(urllib.parse.urlparse(lock["core"]["windows"]["url"]).path))
        with zipfile.ZipFile(archive) as original:
            files = {name: original.read(name) for name in original.namelist() if not name.endswith("emucap-mcp.exe")}
        with zipfile.ZipFile(archive, "w") as replaced:
            for name, data in files.items():
                replaced.writestr(name, data)
        lock["core"]["windows"]["sha256"] = digest(archive.read_bytes())
        with self.assertRaisesRegex(ValueError, "emucap-mcp.exe"):
            packages.install(lock, "windows", self.root / "install")

    def test_wrong_core_source_revision_is_rejected(self):
        lock = self.lock(wrong_revision=True)
        with self.assertRaisesRegex(ValueError, "source revision"):
            packages.install(lock, "windows", self.root / "install")


if __name__ == "__main__":
    unittest.main()
