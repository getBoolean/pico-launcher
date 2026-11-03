"""Pinned SDK transport and evidence for the native emulator checks."""

from contextlib import asynccontextmanager
from datetime import timedelta
import json
import os
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


def decode_result(name, result):
    if result.isError:
        message = " ".join(c.text for c in result.content if c.type == "text")
        raise RuntimeError(f"{name}: {message}")
    if result.structuredContent is not None:
        return result.structuredContent
    for content in result.content:
        if content.type == "text":
            try:
                return json.loads(content.text)
            except json.JSONDecodeError:
                continue
    raise RuntimeError(f"{name}: no JSON result payload")


class EmulatorClient:
    def __init__(self, session, output):
        self.session = session
        self.output = output
        self.trace = []

    async def call(self, name, arguments):
        action = {"tool": name, "arguments": arguments, "status": "pending"}
        self.trace.append(action)
        self.save_trace()
        try:
            value = decode_result(name, await self.session.call_tool(name, arguments))
            action.update(status="complete", result=value)
            return value
        except Exception as error:
            action.update(status="failed", error=str(error))
            raise
        finally:
            self.save_trace()

    def save_trace(self):
        (self.output / "trace.json").write_text(json.dumps(self.trace, indent=2), encoding="utf-8")


@asynccontextmanager
async def connect(install, output):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ, EMUCAP_REPO_ROOT=install["core_root"],
               EMUCAP_DESMUME_BIN=install["desmume_executable"],
               EMUCAP_NDS_BRIDGE_BIN=install["bridge_executable"],
               EMUCAP_EMU_HOME=str(output / "runtime"), EMUCAP_PORT="0")
    env.pop("EMUCAP_BROKER", None)
    params = StdioServerParameters(command=install["core_executable"], args=[], env=env)
    with (output / "stderr.log").open("w", encoding="utf-8") as errors:
        async with stdio_client(params, errlog=errors) as (read, write):
            async with ClientSession(read, write, read_timeout_seconds=timedelta(seconds=120)) as session:
                initialized = await session.initialize()
                catalog = await session.list_tools()
                (output / "initialize.json").write_text(initialized.model_dump_json(indent=2), encoding="utf-8")
                (output / "tools.json").write_text(catalog.model_dump_json(indent=2), encoding="utf-8")
                if initialized.serverInfo.version != install["version"]:
                    raise RuntimeError("MCP server version differs from installed package")
                launch = next(t for t in catalog.tools if t.name == "launch")
                if "nds_storage_image" not in launch.inputSchema["properties"]:
                    raise RuntimeError("MCP launch schema lacks nds_storage_image")
                client = EmulatorClient(session, output)
                status = await client.call("status", {})
                build = status.get("server_build", "")
                if len(build) < 7 or not install["core_source_revision"].startswith(build):
                    raise RuntimeError("MCP build differs from installed package source")
                yield client
