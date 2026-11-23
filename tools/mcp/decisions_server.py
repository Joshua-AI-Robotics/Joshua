import argparse
import asyncio
import os

from mcp import types
from mcp.server.lowlevel import Server
from mcp.server.stdio import stdio_server
from openai import AsyncOpenAI

from tools.mcp.decisions_request import DecisionsRequests
from tools.mcp.snapshot_observer import SnapshotObserver


def make_server(requests):
    app = Server("joshua-decisions")
    tool = types.Tool(
        name="request_decision",
        description=(
            "Send the operator's whole Joshua request verbatim. Decisions chooses the "
            "route. Display the reply. Start sequence at 1 per connection, increment "
            "for each new turn, and reuse the sequence and text to retry."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "text": {"type": "string", "minLength": 1, "maxLength": 2000},
                "sequence": {"type": "integer", "minimum": 1},
            },
            "required": ["text", "sequence"],
            "additionalProperties": False,
        },
        annotations=types.ToolAnnotations(
            readOnlyHint=True,
            destructiveHint=False,
            openWorldHint=True,
            idempotentHint=True,
        ),
    )

    @app.list_tools()
    async def list_tools():
        return [tool]

    @app.call_tool()
    async def call_tool(name, arguments):
        if name != tool.name:
            raise ValueError("Unknown tool.")
        report = await requests.request(**arguments)
        return types.CallToolResult(
            content=[types.TextContent(type="text", text=report["reply"])],
            structuredContent=report,
            isError=report["outcome"] in {"failed", "timeout"},
        )

    return app


async def serve(args):
    observer = SnapshotObserver(args.observation_dir)
    try:
        async with AsyncOpenAI(
            api_key=os.environ["OPENAI_API_KEY"],
            base_url="https://api.openai.com/v1",
            timeout=18,
            max_retries=0,
        ) as api:
            app = make_server(DecisionsRequests(api, observer))
            async with stdio_server() as (reader, writer):
                await app.run(reader, writer, app.create_initialization_options())
    finally:
        await observer.aclose()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--observation-dir")
    asyncio.run(serve(parser.parse_args()))
