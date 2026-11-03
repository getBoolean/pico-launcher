# Native emulator testing

Windows uses the native emucap MCP core and DeSmuME. Linux automation consumes
the same packages, card fixtures and expected outcomes. The emucap fork keeps
`feat/nds-storage-image` ready for upstream contribution and puts release
packaging on `pico/1.1.0-storage`.

## Package setup

`tools/emulator/versions.json` records the exact core, native emulator and loader
revisions, download URLs and SHA-256 hashes. Installers verify archive hashes and
payload manifests before moving complete package directories into place. Native
DLLs and license files stay beside the emulator. Installers write `install.json`
with absolute executable paths and leave MCP configuration to the registration
step.

On Windows, run:

```powershell
./tools/emulator/install-windows.ps1
```

The default destination is `%LOCALAPPDATA%/emucap/pico/<locked-version>/`.
The installer requires an existing Python 3.12 or newer. It uses UTF-8 mode and
keeps the machine's Python defaults unchanged.

On Linux, provide an explicit destination:

```sh
sh tools/emulator/install-linux.sh --destination "$PWD/out/emulator/install"
```

Create an isolated Python environment for the MCP checks, then install the
hash-locked dependencies. For Windows:

```powershell
uv venv --python 3.12 out/emulator/.venv
uv pip sync --python out/emulator/.venv/Scripts/python.exe --require-hashes tests/emulator/requirements.lock
```

## Windows registration and rollback

Preview the owned registration using the installed `install.json`:

```powershell
./tools/emulator/register-windows-mcp.ps1 -InstallManifest '<install-directory>/install.json'
```

Review the command, environment and resolved configuration path. Add `-Apply`
to register `emucap-pico`. Registration uses `$CODEX_HOME/config.toml` when set,
otherwise the user's `.codex/config.toml`. It backs up the configuration before
editing its own entry. The entry names the native core, bridge and DeSmuME
executables, an isolated runtime directory and an automatically selected port.

Reconnect the Codex provider and verify that the actual session discovers
`emucap-pico` tools. Standalone stdio verification and session tool discovery are
separate checks.

Targeted rollback preserves later edits to other servers:

```powershell
./tools/emulator/register-windows-mcp.ps1 -Rollback
```

Rollback refuses to overwrite an owned entry changed since registration. The
sibling `.emucap-pico.json` records the owned block and backup path; keep it for
rollback. Configuration backups can contain other server credentials and stay
outside this repository.

## Native smoke evidence

Verify the packaged server identity and storage schema:

```powershell
./tools/emulator/verify-windows.ps1 -InstallManifest '<install-directory>/install.json' -Output out/emulator/handshake
```

With a fresh writable fixture image, capture a bounded boot and input action:

```powershell
./tools/emulator/verify-windows.ps1 -InstallManifest '<install-directory>/install.json' -Rom LAUNCHER.nds -Image '<fresh-case-image>' -Output out/emulator/native-boot -Frames 120
```

Choose a button from the live input contract and add `-Press` to capture before
and after that input. Output directories must be new. Evidence includes server
identity, discovered tool schemas, action results, screenshots and stderr. Cleanup
uses the exact managed launch ID.

Qualification additionally requires known browser content, a saved setting
surviving restart, and a child ROM reaching its distinct marker. Record those
observations against the ROM, ELF, fixture and package identities.

## Writable card fixtures

The build workflow pins the BlocksDS container by digest and publishes
`Pico_Emulator_Fixture`: the launcher ROM, matching ARM9 ELF, writable card image,
fixture identity, scenario manifest and dependency lock. The identity records
the ROM, ELF, image, loader and lock hashes so runtime checks can reject a mixed
artifact set. The ordinary `Pico_Launcher` download also includes the ARM9 ELF.

Build the marked child ROMs inside the project's BlocksDS build environment:

```sh
make -C tests/emulator/fixtures/children CHILD=A
make -C tests/emulator/fixtures/children CHILD=B
```

`tests/emulator/build_fixture.py --help` lists the exact launcher, matching ARM9
ELF, child ROM, verified loader archive and `mkfatimg` inputs. The fixture includes
Material/Raspberry themes, loader API 3 assets, seeded settings, declared search
outcomes and stress folders. The full 32 MiB image is writable; `mkfatimg -t`
creates a truncated read-only volume and is unsuitable for persistence checks.

Keep the ordinary launcher ROM unpatched. DeSmuME attaches the image through
`--cflash-image` and supplies its embedded MPCF DLDI driver. Each independent case
owns a fresh image and MCP process; only a deliberate persistence case reuses its
own image across generations.

Managed stop forcibly terminates DeSmuME. Before a persistence restart, confirm
the guest save finished and a subsequent real card read returned. A cached
filesystem read does not establish that boundary. Stop the exact generation and
confirm process exit before inspecting or copying the image. Retain an image
without that read witness as `flush_unconfirmed` evidence. Savestates do not roll
back the external card image.

## Host checks

Run the emulator helper tests in the isolated environment:

```powershell
out/emulator/.venv/Scripts/python.exe -X utf8 -m unittest discover -s tests/emulator -p 'test_*.py' -v
```

Package and fixture checks establish their declared host-side contracts. Native
MCP access, guest state, reviewed visual baselines and physical DS behavior each
need their own evidence. Physical DS checks remain owner-run.
