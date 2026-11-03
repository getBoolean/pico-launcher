"""Registration edits only the owned MCP entry and supports scoped rollback."""

import importlib.util
import json
from pathlib import Path
import tempfile
import tomllib
import unittest


REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "mcp_registration", REPO / "tools/emulator/mcp_registration.py")
registration = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(registration)


class RegistrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.config = self.root / "codex/config.toml"
        self.config.parent.mkdir()
        self.original = b'# user settings\r\nmodel = "example"\r\n\r\n[mcp_servers.other]\r\ncommand = "other.exe"\r\n'
        self.config.write_bytes(self.original)
        self.install = self.root / "installed/install.json"
        self.install.parent.mkdir()
        self.manifest = {"version": "1.1.0-pico.1", "core_source_revision": "a" * 40}
        self.manifest["core_root"] = str(self.install.parent / "core")
        for key, filename in (("core_executable", "core/target/release/emucap-mcp.exe"),
                              ("bridge_executable", "core/target/release/emucap-desmume-nds-bridge.exe"),
                              ("desmume_executable", "native/desmume-cli.exe")):
            path = self.install.parent / filename
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"test package")
            self.manifest[key] = str(path)
        self.install.write_text(json.dumps(self.manifest), encoding="utf-8")

    def read_config(self):
        return tomllib.loads(self.config.read_bytes().decode("utf-8"))

    def test_preview_names_owned_paths_without_mutating_or_exposing_other_config(self):
        preview = registration.preview(self.config, self.install)
        self.assertEqual(self.config.read_bytes(), self.original)
        self.assertEqual(preview["server"], "emucap-pico")
        self.assertEqual(preview["command"], self.manifest["core_executable"])
        self.assertEqual(preview["env"]["EMUCAP_REPO_ROOT"], self.manifest["core_root"])
        self.assertEqual(preview["env"]["EMUCAP_PORT"], "0")
        self.assertNotIn("other.exe", json.dumps(preview))
        self.assertEqual(list(self.config.parent.iterdir()), [self.config])

    def test_apply_preserves_unrelated_bytes_and_records_exact_backup(self):
        result = registration.apply(self.config, self.install)
        self.assertTrue(self.config.read_bytes().startswith(self.original))
        self.assertEqual(Path(result["backup"]).read_bytes(), self.original)
        server = self.read_config()["mcp_servers"]["emucap-pico"]
        self.assertEqual(server["command"], self.manifest["core_executable"])
        self.assertEqual(server["env"]["EMUCAP_DESMUME_BIN"], self.manifest["desmume_executable"])
        self.assertEqual(server["env"]["EMUCAP_NDS_BRIDGE_BIN"], self.manifest["bridge_executable"])
        self.assertEqual(server["env"]["EMUCAP_EMU_HOME"], str(self.install.parent / "runtime"))

    def test_repeat_apply_is_idempotent(self):
        first = registration.apply(self.config, self.install)
        config_bytes = self.config.read_bytes()
        state_bytes = Path(first["state"]).read_bytes()
        again = registration.apply(self.config, self.install)
        self.assertEqual(again["action"], "already_registered")
        self.assertEqual(self.config.read_bytes(), config_bytes)
        self.assertEqual(Path(first["state"]).read_bytes(), state_bytes)
        self.assertEqual(len(list(self.config.parent.glob("*.bak"))), 1)

    def test_rollback_preserves_later_unrelated_edits_and_comments(self):
        registration.apply(self.config, self.install)
        later = b'\r\n# later user note\r\n[mcp_servers.later]\r\ncommand = "later.exe"\r\n'
        with self.config.open("ab") as output:
            output.write(later)
        registration.rollback(self.config)
        self.assertEqual(self.config.read_bytes(), self.original + later)
        self.assertNotIn("emucap-pico", self.read_config()["mcp_servers"])

    def test_rollback_restores_preexisting_quoted_entry_and_nested_env(self):
        previous = b'\r\n[mcp_servers."emucap-pico"]\r\ncommand = "old.exe"\r\n[mcp_servers."emucap-pico".env]\r\nOLD = "old value"\r\n'
        self.config.write_bytes(self.original + previous)
        registration.apply(self.config, self.install)
        registration.rollback(self.config)
        server = self.read_config()["mcp_servers"]["emucap-pico"]
        self.assertEqual(server, {"command": "old.exe", "env": {"OLD": "old value"}})
        self.assertTrue(self.config.read_bytes().startswith(self.original))

    def test_rollback_refuses_changed_owned_entry(self):
        registration.apply(self.config, self.install)
        changed = self.config.read_bytes().replace(b'emucap-mcp.exe', b'changed-mcp.exe')
        self.config.write_bytes(changed)
        with self.assertRaisesRegex(ValueError, "changed"):
            registration.rollback(self.config)
        self.assertEqual(self.config.read_bytes(), changed)

    def test_table_like_text_in_multiline_string_is_not_an_owned_table(self):
        original = b'note = """\n[mcp_servers.emucap-pico]\ncommand = "inside a string"\n"""\n'
        self.config.write_bytes(original)
        registration.apply(self.config, self.install)
        registration.rollback(self.config)
        self.assertEqual(self.config.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
