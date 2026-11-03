import unittest

from mcp.types import CallToolResult, TextContent

from mcp_client import decode_result


class ResultTests(unittest.TestCase):
    def test_structured_payload_is_the_semantic_result(self):
        result = CallToolResult(content=[], structuredContent={"connected": True})
        self.assertEqual(decode_result("status", result), {"connected": True})

    def test_json_text_payload_is_supported(self):
        result = CallToolResult(content=[TextContent(type="text", text='{"stopped":true}')])
        self.assertEqual(decode_result("stop", result), {"stopped": True})

    def test_tool_error_cannot_become_a_successful_assertion(self):
        result = CallToolResult(isError=True, content=[TextContent(type="text", text="transport lost")])
        with self.assertRaisesRegex(RuntimeError, "transport lost"):
            decode_result("step", result)

    def test_success_without_a_semantic_payload_is_rejected(self):
        result = CallToolResult(content=[TextContent(type="text", text="no JSON result")])
        with self.assertRaisesRegex(RuntimeError, "JSON"):
            decode_result("status", result)
