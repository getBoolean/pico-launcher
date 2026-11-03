# Search regressions

See [native setup and CI evidence](../../docs/EmulatorTesting.md) for installation,
provider reconnection, rollback and verification limits.

Run the host checks with `bash tests/search/run.sh` (or `wsl bash tests/search/run.sh` on Windows).
They compile the production filtering/sorting, recursive walker, touch-filter and weak-reference
code with sanitizers. The walker checks depth and result limits, 255/256-byte path boundaries,
duplicate parents, hidden and unsupported files, cancellation and FAT failures. Hardware file
types, FAT handles, IRQ operations and touch samples are replaced at their boundaries; these
checks do not prove real card I/O or screen rendering.

Install the hash-locked SDK requirements in a virtual environment and prepare the complete
fixture with `build_fixture.py`. Run the same native MCP scenarios on Windows or Linux:

```text
python tests/emulator/search_cases.py --install-manifest <install.json> --rom <LAUNCHER.nds> --elf <arm9.elf> --fixture <fixture-directory> --output <new-evidence-directory>
```

The ROM, ELF and emulator lock must match the fixture identity. The runner copies immutable inputs and
starts each case from a fresh card. Inputs hold for six emulated frames by default;
`--press-frames 2` exercises short taps. `--case <name>` selects one or more cases:

| Case | Assertions |
| --- | --- |
| current-folder | Case-insensitive filtering, spaces, hyphens, backspace, repeated clear, empty query and recursive toggle |
| empty-and-limit | 31-character limit, overflow key, no-match navigation/launch safety, close and reopen |
| recursive | Duplicate basenames retain distinct parents; current-folder and nested matches coexist |
| cancellation | Original walk is Running at Y-clear and B-close input-handler entry; no stale results after clear; reopening preserves recursive search |
| replacement | Original walk is Running at replacement/toggle-off ApplySearch entry; replaced results cannot reappear |

In-flight witnesses use the matching ELF and temporary breakpoints before cancellation or
task disposal. The first ApplySearch after the snapshot is monitored; pointer reuse alone
cannot pass. A case fails if no running task is witnessed. The bounded stress tree is sized
for a useful input window without thousands of empty directories in every CI case.

Each output contains the MCP trace, ROM/ELF/fixture identity, state assertions, screenshots
and an exact-generation stop result. It also preserves the install manifest and hashes the
three runtime executables so a diagnostic native build can be distinguished from a published
package. Failures retain their card image. Images are marked
`flush_unconfirmed`: these scenarios do not perform the physical post-write read required
for persistence qualification. Screenshots are evidence, not automatically accepted visual
baselines. Physical DS input and timing remain separate hardware checks.

The `Native emulator search regressions` CI job downloads the matching build artifact and
the locked Linux packages, then publishes `Pico_Search_Evidence` even on failure. A manual
workflow run with `emulator_failure_probe` enabled deliberately fails after boot; its report,
screenshots, CPU state and confirmed managed stop verify the failure-evidence path. The
equivalent local command adds `--case current-folder --failure-probe` to the runner.
