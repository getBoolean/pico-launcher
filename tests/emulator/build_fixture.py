"""Prepare a complete Pico card tree and writable FAT image from pinned inputs."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zipfile


LOADER_FILES = ("picoLoader7.bin", "picoLoader9.bin", "aplist.bin",
                "savelist.bin", "patchlist.bin")


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def prepare_tree(repo, loader_archive, children, destination, manifest, loader_sha256):
    """Return input/content identity, creating a fresh tree only after admission."""
    repo, loader_archive, destination = map(Path, (repo, loader_archive, destination))
    if sha256(loader_archive) != loader_sha256:
        raise ValueError("Pico Loader archive SHA-256 does not match the dependency lock")
    with zipfile.ZipFile(loader_archive) as archive:
        missing = set(LOADER_FILES) - set(archive.namelist())
        if missing:
            raise ValueError(f"Missing loader files: {', '.join(sorted(missing))}")
        loader = {name: archive.read(name) for name in LOADER_FILES}
    header = (repo / "common/picoLoader7.h").read_text(encoding="utf-8")
    expected_api = int(re.search(r"#define\s+PICO_LOADER_API_VERSION\s+(\d+)", header)[1])
    binary_api = struct.unpack_from("<H", loader["picoLoader7.bin"], 10)[0]
    if binary_api != expected_api or binary_api != manifest["loader_api"]:
        raise ValueError(f"Loader API mismatch: binary {binary_api}, header {expected_api}")
    for theme in ("material", "raspberry"):
        if not (repo / "_pico/themes" / theme / "theme.json").is_file():
            raise ValueError(f"Missing theme: {theme}")
    for child in ("A", "B"):
        if not Path(children[child]).is_file():
            raise ValueError(f"Missing built child ROM: {child}")

    destination.mkdir(parents=True, exist_ok=False)
    shutil.copytree(repo / "_pico", destination / "_pico")
    for name, data in loader.items():
        (destination / "_pico" / name).write_bytes(data)
    (destination / "_pico/settings.json").write_text(
        json.dumps(manifest["settings"], indent=2) + "\n", encoding="utf-8")
    for rom in manifest["roms"]:
        target = destination / rom["path"]
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(children[rom["child"]], target)

    ranking = manifest["ranking"]
    approximate = destination / 'games/stress/aaa-cap'
    approximate.mkdir(parents=True)
    for index in range(ranking['approximate_count']):
        shutil.copyfile(children[ranking['child']], approximate / f'cpa-{index:03}.nds')
    literal = destination / 'games/stress/zzz-cap'
    literal.mkdir()
    shutil.copyfile(children[ranking['child']], literal / 'cap-exact.nds')
    long_path = destination / 'games/stress/longpath'
    for index in range(manifest['long_path']['depth']):
        long_path /= f'path_segment_{index:02}_' + 'x' * manifest['long_path']['padding']
    long_path.mkdir(parents=True)
    shutil.copyfile(children['A'], long_path / 'x.nds')

    # Empty directories extend traversal without hitting the result-count cap.
    def stress(parent, depth):
        if not depth:
            return
        for index in range(manifest["stress"]["breadth"]):
            folder = parent / f"d{index:02}"
            folder.mkdir(parents=True)
            stress(folder, depth - 1)

    stress(destination / "games/stress", manifest["stress"]["depth"])
    files = {}
    for path in sorted(destination.rglob("*")):
        if path.is_file():
            files[path.relative_to(destination).as_posix()] = sha256(path)
        os.utime(path, (manifest["timestamp"], manifest["timestamp"]))
    os.utime(destination, (manifest["timestamp"], manifest["timestamp"]))
    return {
        "format": manifest["format"],
        "loader_sha256": loader_sha256,
        "loader_api": binary_api,
        "manifest_sha256": hashlib.sha256(json.dumps(manifest, sort_keys=True).encode()).hexdigest(),
        "initial_state": manifest["initial_state"],
        "files": files,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--manifest", type=Path,
                        default=Path(__file__).parent / "fixtures/manifest.json")
    parser.add_argument("--lock", type=Path,
                        default=Path(__file__).resolve().parents[2] / "tools/emulator/versions.json")
    parser.add_argument("--loader-archive", type=Path, required=True)
    parser.add_argument("--child-a", type=Path, required=True)
    parser.add_argument("--child-b", type=Path, required=True)
    parser.add_argument("--launcher", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--mkfatimg", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    lock = json.loads(args.lock.read_text(encoding="utf-8"))
    # Require the exact launcher/ELF pair even though the launcher boots outside the card.
    build = {"launcher_sha256": sha256(args.launcher), "elf_sha256": sha256(args.elf)}
    args.output.mkdir(parents=True, exist_ok=False)
    tree = args.output / "card"
    identity = prepare_tree(args.repo, args.loader_archive,
                            {"A": args.child_a, "B": args.child_b}, tree, manifest,
                            lock["loader"]["sha256"])
    image = args.output / "card.img"
    # -t truncates unused sectors for read-only volumes; persistence needs the full image.
    subprocess.run([str(args.mkfatimg), str(tree), str(image),
                    str(manifest["image_size_kib"])],
                   check=True, env=dict(os.environ, TZ="UTC"))
    identity.update(build)
    identity["lock_sha256"] = sha256(args.lock)
    identity["image_sha256"] = sha256(image)
    (args.output / "identity.json").write_text(json.dumps(identity, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
