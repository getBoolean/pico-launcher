"""Guard failure cleanup and fresh observations across frozen boundaries."""
from pathlib import Path
import unittest

from search_cases import SearchCase
from elf_state import OracleError, ViewModelPending
from test_elf_state import fixture


class SearchHarnessTests(unittest.IsolatedAsyncioTestCase):
    async def test_settling_waits_only_for_an_explicit_view_model_transition(self):
        import tempfile
        with tempfile.TemporaryDirectory() as output:
            case = SearchCase(None, None, Path(output))
            observations = [ViewModelPending('rebuilding'), {'task': None, 'query': 'CAP'}]
            frames = []
            async def state():
                value = observations.pop(0)
                if isinstance(value, Exception):
                    raise value
                return value
            async def step(count):
                frames.append(count)
            case.state, case.step = state, step
            self.assertEqual((await case.settled(limit=20))['query'], 'CAP')
            self.assertEqual(frames, [20])
            observations[:] = [ViewModelPending('rebuilding')] * 2
            with self.assertRaisesRegex(AssertionError, 'finish rebuilding'):
                await case.settled(limit=20)
            observations[:] = [OracleError('invalid RAM')]
            with self.assertRaisesRegex(OracleError, 'invalid RAM'):
                await case.settled()

    async def test_full_folder_expectation_rejects_a_clipped_label(self):
        descriptor, data, _ = fixture()
        path = 'fat:/games/' + 'path-' * 70
        encoded = path.encode() + b'\0'
        data[0x6400:0x6400 + len(encoded)] = encoded
        encoded_label = (path[:256] + '\0').encode('utf-16-le')
        data[0x6c00:0x6c00 + len(encoded_label)] = encoded_label
        class Client:
            async def call(self, name, arguments):
                start = arguments['address']
                return {'hex': bytes(data[start:start + arguments['length']]).hex()}
        case = SearchCase(Client(), descriptor, Path('.'))
        with self.assertRaises(AssertionError):
            await case.expect('game', ['A/game.nds', 'B/game.nds'], recursive=True,
                              folder=path.split(':', 1)[-1])
        encoded_label = (path + '\0').encode('utf-16-le')
        data[0x6c00:0x6c00 + len(encoded_label)] = encoded_label
        await case.expect('game', ['A/game.nds', 'B/game.nds'], recursive=True,
                          folder=path.split(':', 1)[-1])

    async def test_ordered_expectation_rejects_reversed_relevance(self):
        descriptor, data, _ = fixture()
        class Client:
            async def call(self, name, arguments):
                start = arguments['address']
                return {'hex': bytes(data[start:start + arguments['length']]).hex()}
        case = SearchCase(Client(), descriptor, Path('.'))
        await case.expect('game', ['B/game.nds', 'A/game.nds'], recursive=True)
        with self.assertRaises(AssertionError):
            await case.expect('game', ['B/game.nds', 'A/game.nds'], recursive=True, ordered=True)
        await case.expect('game', ['A/game.nds', 'B/game.nds'], recursive=True,
                          ordered=True, folder='/games')

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
