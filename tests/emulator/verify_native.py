"""Record native MCP identity and an optional bounded Pico boot/input capture."""

import argparse
import asyncio
import json
from pathlib import Path

from mcp_client import connect


async def verify(args):
    install = json.loads(args.install_manifest.read_text(encoding="utf-8"))
    async with connect(install, args.output) as client:
        print(f"Verified MCP identity and storage schema: {install['version']}", flush=True)
        if not args.rom:
            return
        await client.call("bootstrap", {})
        plan = await client.call("launch_plan", {"content_path": str(args.rom.resolve()), "system": "nds",
                                                "nds_storage_image": str(args.image.resolve())})
        if not plan.get("ready_to_launch"):
            raise RuntimeError(f"Launch prerequisites not satisfied: {plan}")
        generation = None
        try:
            launched = await client.call("launch", plan["preferred_launcher"]["args"])
            generation = launched.get("runtime_instance", {}).get("launch_id")
            status = await client.call("status", {})
            generation = generation or status.get("runtime_instance", {}).get("launch_id")
            if not generation or not status.get("connected"):
                raise RuntimeError("Native launch did not establish an owned connected generation")
            await client.call("pause", {})
            if args.frames:
                await client.call("step", {"count": args.frames, "unit": "frames"})
            await client.call("screenshot", {"save_path": str(args.output.resolve() / "before.png")})
            if args.press:
                await client.call("tap", {"buttons": [args.press], "press_frames": 2, "after_frames": 2})
                await client.call("screenshot", {"save_path": str(args.output.resolve() / "after.png")})
            await client.call("status", {})
            print(f"Captured native boot/input evidence in {args.output}", flush=True)
        finally:
            if generation:
                await client.call("stop", {"launch_id": generation})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-manifest", type=Path, required=True)
    parser.add_argument("--rom", type=Path)
    parser.add_argument("--image", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--press")
    args = parser.parse_args()
    if bool(args.rom) != bool(args.image):
        parser.error("--rom and --image must be provided together")
    if args.frames < 0:
        parser.error("--frames cannot be negative")
    asyncio.run(verify(args))


if __name__ == "__main__":
    main()
