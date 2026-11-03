"""Guard failure cleanup and fresh observations across frozen boundaries."""
from pathlib import Path
import unittest

from search_cases import SearchCase
from test_elf_state import fixture


class SearchHarnessTests(unittest.IsolatedAsyncioTestCase):
    async def test_a_new_frozen_boundary_does_not_reuse_cached_query_bytes(self):
        descriptor, data, _ = fixture()
        class Client:
            async def call(self, name, arguments):
                assert name == 'read_memory'
                start = arguments['address']
                return {'hex': bytes(data[start:start + arguments['length']]).hex()}
        case = SearchCase(Client(), descriptor, Path('.'))
        self.assertEqual((await case.state())['query'], 'game')
        data[0x1020:0x1025] = b'next\0'
        self.assertEqual((await case.state())['query'], 'next')

    async def test_lost_acquisition_acknowledgment_still_releases_each_input(self):
        for method in ('key', 'touch'):
            calls = []
            class Client:
                async def call(self, name, arguments):
                    calls.append(arguments)
                    if len(calls) == 1:
                        raise RuntimeError('acquisition response lost')
                    return {}
            case = SearchCase(Client(), None, Path('.'))
            with self.assertRaisesRegex(RuntimeError, 'response lost'):
                if method == 'key':
                    await case.key('y')
                else:
                    await case.touch(28, 109)
            self.assertEqual(len(calls), 2)
            self.assertEqual(calls[-1]['operation'], 'set_input' if method == 'key' else 'release_touch')
            self.assertEqual(calls[-1]['arguments'], {'buttons': []} if method == 'key' else {})
