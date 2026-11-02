"""Install the locked emucap core and native DeSmuME packages without registration."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import tarfile
import tempfile
import urllib.parse
import urllib.request
import zipfile


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_file(path, expected_hash=None):
    if not path.is_file():
        raise ValueError(f"Missing required package file: {path}")
    if expected_hash and sha256(path) != expected_hash:
        raise ValueError(f"Package file SHA-256 mismatch: {path}")


def validate_install(lock, platform, destination):
    suffix = ".exe" if platform == "windows" else ""
    core = destination / lock["core"][platform]["root"]
    native = destination / lock["desmume"][platform]["root"]
    manifest_path = core / "CORE-BUILD.json"
    check_file(manifest_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest["version"] != lock["core"]["version"]:
        raise ValueError("Core package version does not match the lock")
    if manifest["source_revision"] != lock["core"]["source_revision"]:
        raise ValueError("Core package source revision does not match the lock")
    for name, expected in manifest["binaries"].items():
        check_file(core / "target/release" / name, expected)
    native_manifest_path = native / "NATIVE-PACKAGE.json"
    check_file(native_manifest_path)
    native_manifest = json.loads(native_manifest_path.read_text(encoding="utf-8"))
    for relative, entry in native_manifest["files"].items():
        check_file(native / relative, entry["sha256"])
    result = {
        "version": lock["core"]["version"],
        "core_source_revision": lock["core"]["source_revision"],
        "core_root": str(core.resolve()),
        "core_executable": str((core / "target/release" / f"emucap-mcp{suffix}").resolve()),
        "bridge_executable": str((core / "target/release" / f"emucap-desmume-nds-bridge{suffix}").resolve()),
        "desmume_executable": str((native / f"desmume-cli{suffix}").resolve()),
    }
    for name in ("core_executable", "bridge_executable", "desmume_executable"):
        check_file(Path(result[name]))
    return result


def install(lock, platform, destination):
    """Install verified complete package trees, or validate an identical existing install."""
    destination = Path(destination).resolve()
    if destination.exists():
        result = validate_install(lock, platform, destination)
        recorded = json.loads((destination / "install.json").read_text(encoding="utf-8"))
        if recorded != result:
            raise ValueError(f"Existing installation identity differs: {destination}")
        return result

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".emucap-install-", dir=destination.parent) as temporary:
        temporary = Path(temporary)
        downloads = []
        for role in ("core", "desmume"):
            asset = lock[role][platform]
            archive = temporary / role
            urllib.request.urlretrieve(asset["url"], archive)
            if sha256(archive) != asset["sha256"]:
                raise ValueError(f"{role} archive SHA-256 does not match the lock")
            downloads.append((archive, asset))
        stage = temporary / "payload"
        stage.mkdir()
        for archive, asset in downloads:
            if urllib.parse.urlparse(asset["url"]).path.endswith(".zip"):
                with zipfile.ZipFile(archive) as packed:
                    packed.extractall(stage)
            else:
                with tarfile.open(archive, "r:gz") as packed:
                    packed.extractall(stage, filter="data")
        result = validate_install(lock, platform, stage)
        for key in ("core_root", "core_executable", "bridge_executable", "desmume_executable"):
            result[key] = str(destination / Path(result[key]).relative_to(stage))
        (stage / "install.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        stage.rename(destination)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, default=Path(__file__).with_name("versions.json"))
    parser.add_argument("--platform", choices=("windows", "linux"), required=True)
    parser.add_argument("--destination", type=Path)
    args = parser.parse_args()
    try:
        lock = json.loads(args.lock.read_text(encoding="utf-8"))
        if lock["format"] != 1:
            raise ValueError("Unsupported emulator lock format")
        destination = args.destination
        if destination is None:
            if args.platform != "windows":
                parser.error("Linux installation requires --destination")
            destination = Path(os.environ["LOCALAPPDATA"]) / "emucap/pico" / lock["core"]["version"]
        print(json.dumps(install(lock, args.platform, destination), indent=2))
    except (OSError, ValueError, KeyError, tarfile.TarError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Emulator installation failed: {error}\n")


if __name__ == "__main__":
    main()
