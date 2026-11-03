"""Keep native CI tied to matching build artifacts and retain failed-run evidence."""
from pathlib import Path
import unittest


class CiContractTests(unittest.TestCase):
    def test_build_handoff_and_failure_evidence_are_declared(self):
        workflow = (Path(__file__).resolve().parents[2] / '.github/workflows/nightly.yml').read_text(encoding='utf-8')
        build, native = workflow.split('  emulator_search:\n')
        self.assertIn('name: Pico_Emulator_Fixture', build)
        self.assertIn('build/arm9.elf', build)
        self.assertIn('out/emulator/fixture/identity.json', build)
        for required in ('needs: pico_launcher', 'runs-on: ubuntu-24.04',
                         'name: Pico_Emulator_Fixture', 'path: out/emulator/ci-fixture',
                         '--require-hashes', '--rom out/emulator/ci-fixture/LAUNCHER.nds',
                         '--elf out/emulator/ci-fixture/build/arm9.elf',
                         '--fixture out/emulator/ci-fixture/out/emulator/fixture',
                         '--case current-folder --failure-probe',
                         'if: always()', 'name: Pico_Search_Evidence'):
            self.assertIn(required, native)
        self.assertNotIn('pull_request_target', workflow)
        self.assertNotIn('secrets.', native)
