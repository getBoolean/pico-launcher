"""Preview, register and roll back only the emucap-pico Codex MCP entry."""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import tempfile
import tomllib


SERVER = "emucap-pico"
BEGIN = "# BEGIN emucap-pico registration"
END = "# END emucap-pico registration"


def parse(text):
    return tomllib.loads(text.removeprefix("\ufeff"))


def state_path(config):
    return config.with_name(config.name + ".emucap-pico.json")


def read_config(config):
    data = config.read_bytes() if config.exists() else b""
    text = data.decode("utf-8")
    parse(text)
    return data, text


def owned_sections(text):
    headers = []
    for match in re.finditer(r"^[ \t]*\[.*\][ \t]*(?:#.*)?(?:\r?\n|$)", text, re.MULTILINE):
        try:
            parse(text[:match.start()])
            header = parse(match.group())
        except tomllib.TOMLDecodeError:
            continue
        headers.append((match.start(), SERVER in header.get("mcp_servers", {})))
    return [(start, headers[index + 1][0] if index + 1 < len(headers) else len(text))
            for index, (start, owned) in enumerate(headers) if owned]


def remove_sections(text, sections):
    for start, end in reversed(sections):
        text = text[:start] + text[end:]
    return text


def desired_server(install_manifest):
    manifest = json.loads(install_manifest.read_text(encoding="utf-8"))
    for key in ("core_executable", "bridge_executable", "desmume_executable"):
        path = Path(manifest[key])
        if not path.is_absolute() or not path.is_file():
            raise ValueError(f"Missing absolute installed executable: {key}")
    root = Path(manifest["core_root"])
    if not root.is_absolute() or not root.is_dir():
        raise ValueError("Missing absolute installed core_root")
    return {
        "command": manifest["core_executable"],
        "env": {
            "EMUCAP_REPO_ROOT": manifest["core_root"],
            "EMUCAP_DESMUME_BIN": manifest["desmume_executable"],
            "EMUCAP_NDS_BRIDGE_BIN": manifest["bridge_executable"],
            "EMUCAP_EMU_HOME": str(install_manifest.parent.resolve() / "runtime"),
            "EMUCAP_PORT": "0",
        },
    }


def render(server, newline):
    lines = [BEGIN, f"[mcp_servers.{SERVER}]", "command = " + json.dumps(server["command"]),
             "env = { " + ", ".join(key + " = " + json.dumps(value)
                                     for key, value in server["env"].items()) + " }", END, ""]
    return newline.join(lines)


def preview(config, install_manifest):
    config, install_manifest = Path(config).resolve(), Path(install_manifest).resolve()
    _, text = read_config(config)
    server = desired_server(install_manifest)
    spans = owned_sections(text)
    proposal = remove_sections(text, spans)
    newline = "\r\n" if "\r\n" in text else "\n"
    proposal += (newline if proposal and not proposal.endswith("\n") else "") + render(server, newline)
    parse(proposal)
    return {"action": "preview", "server": SERVER, "config": str(config),
            "state": str(state_path(config)), "backup": "Timestamped sibling .bak before apply",
            **server}


def checked_state(config, text):
    path = state_path(config)
    if not path.exists():
        return None
    state = json.loads(path.read_text(encoding="utf-8"))
    if state["config"] != str(config):
        raise ValueError("Registration state belongs to a different config")
    if not state["active"]:
        return None
    if (text.count(state["installed_block"]) != 1
            or parse(text).get("mcp_servers", {}).get(SERVER) != state["server"]):
        raise ValueError("The owned MCP entry changed after registration; refusing to overwrite it")
    return state


def remove_registered(text, state):
    start = text.index(state["installed_block"])
    separator = state["separator"]
    if separator and text[:start].endswith(separator):
        start -= len(separator)
    end = text.index(state["installed_block"]) + len(state["installed_block"])
    return text[:start] + text[end:]


def write_config(config, original, replacement):
    current = config.read_bytes() if config.exists() else b""
    if current != original:
        raise ValueError("Config changed while preparing registration; retry from a fresh preview")
    config.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix=".emucap-pico-", dir=config.parent, delete=False) as output:
        temporary = Path(output.name)
        output.write(replacement.encode("utf-8"))
    try:
        temporary.replace(config)
    finally:
        temporary.unlink(missing_ok=True)


def apply(config, install_manifest):
    config, install_manifest = Path(config).resolve(), Path(install_manifest).resolve()
    original, text = read_config(config)
    server = desired_server(install_manifest)
    state = checked_state(config, text)
    if state and state["server"] == server:
        return {"action": "already_registered", "server": SERVER,
                "config": str(config), "state": str(state_path(config)), "backup": state["backup"]}
    if state:
        previous_owned = state["installed_block"]
        base = remove_registered(text, state)
    else:
        spans = owned_sections(text)
        previous_owned = "".join(text[start:end] for start, end in spans)
        base = remove_sections(text, spans)
    newline = "\r\n" if "\r\n" in text else "\n"
    separator = newline if base and not base.endswith("\n") else ""
    block = render(server, newline)
    proposal = base + separator + block
    parse(proposal)
    config.parent.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    backup = config.with_name(config.name + f".emucap-pico.{stamp}.bak")
    with backup.open("xb") as output:
        output.write(original)
    record = {"format": 1, "config": str(config), "active": True,
              "backup": str(backup), "previous_owned": previous_owned,
              "installed_block": block, "separator": separator, "server": server}
    write_config(config, original, proposal)
    try:
        state_path(config).write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    except OSError:
        write_config(config, proposal.encode("utf-8"), text)
        raise
    return {"action": "registered", "server": SERVER, "config": str(config),
            "state": str(state_path(config)), "backup": str(backup)}


def rollback(config):
    config = Path(config).resolve()
    original, text = read_config(config)
    state = checked_state(config, text)
    if not state:
        raise ValueError("No active emucap-pico registration to roll back")
    proposal = remove_registered(text, state)
    previous = state["previous_owned"]
    if previous:
        proposal += ("\n" if proposal and not proposal.endswith("\n") else "") + previous
    parse(proposal)
    write_config(config, original, proposal)
    state["active"] = False
    state_path(config).write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    return {"action": "rolled_back", "server": SERVER, "config": str(config)}


def default_config():
    root = Path(os.environ["CODEX_HOME"]) if os.environ.get("CODEX_HOME") else Path.home() / ".codex"
    return root / "config.toml"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=default_config())
    parser.add_argument("--install-manifest", type=Path)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--apply", action="store_true")
    modes.add_argument("--rollback", action="store_true")
    args = parser.parse_args()
    try:
        if args.rollback:
            result = rollback(args.config)
        else:
            if args.install_manifest is None:
                parser.error("--install-manifest is required for preview/apply")
            result = (apply if args.apply else preview)(args.config, args.install_manifest)
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, KeyError, tomllib.TOMLDecodeError) as error:
        parser.exit(1, f"MCP registration failed: {error}\n")


if __name__ == "__main__":
    main()
