"""Run bounded search regressions through the installed native MCP server.

Each case owns a fresh image and launch generation. Failed cases keep the image,
trace, state and both-screen capture; card flush is not assumed after force stop.
"""
import argparse
import asyncio
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import shutil
import traceback

from elf_state import ViewModelPending, build_descriptor, read_browser_state, require_inflight
from mcp_client import connect


ROWS = ("1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM \b")
OFFSETS = (0, 0, 10, 20)
INITIAL = ['A', 'B', 'stress', 'alpha.nds', 'space-dash one.nds', 'space-dash two.nds']


class SearchCase:
    def __init__(self, client, descriptor, output, *, press_frames=6):
        self.client, self.descriptor, self.output = client, descriptor, output
        self.revision = None
        self.press_frames = press_frames
        self.walk_measurements = []

    async def call(self, name, **arguments):
        return await self.client.call(name, arguments)

    async def debug(self, operation, **arguments):
        return await self.call('debug', operation=operation,
                               known_capability_revision=self.revision, arguments=arguments)

    async def step(self, count=8):
        result = await self.call('step', count=count)
        if result.get('status') != 'completed':
            raise AssertionError(f'Unexpected interrupted frame advance: {result}')

    async def memory(self, address, length):
        result = await self.call('read_memory', memory_type='main', address=address - 0x02000000, length=length)
        return bytes.fromhex(result['hex'])

    async def state(self):
        # All reads belong to one frozen boundary. Cache adjacent DWARF fields
        # together to avoid a round trip for every pointer and integer.
        pages = {}
        async def read(address, length):
            data = bytearray()
            while length:
                base = address & ~1023
                if base not in pages:
                    pages[base] = await self.memory(base, 1024)
                count = min(length, 1024 - (address - base))
                data.extend(pages[base][address - base:address - base + count])
                address += count
                length -= count
            return bytes(data)
        return await read_browser_state(self.descriptor, read, frozen=True)

    async def capture(self, name):
        state = await self.state()
        (self.output / f'{name}.json').write_text(json.dumps(state, indent=2) + '\n')
        await self.call('screenshot', save_path=str(self.output / f'{name}.png'))
        return state

    async def touch(self, x, y, after=8):
        try:
            await self.debug('hold_touch', x=x, y=y)
            await self.step(self.press_frames)
        finally:
            await self.debug('release_touch')
        await self.step(after)

    async def key(self, key, after=8):
        try:
            await self.debug('set_input', buttons=[key])
            await self.step(self.press_frames)
        finally:
            await self.debug('set_input', buttons=[])
        await self.step(after)

    async def text(self, text):
        for char in text.upper():
            row = next(i for i, keys in enumerate(ROWS) if char in keys)
            await self.touch(28 + OFFSETS[row] + ROWS[row].index(char) * 20, 97 + row * 18)

    async def expect(self, query, paths, *, recursive=False, sheet='Search', ordered=False, folder=None):
        state = await self.state()
        assert state['query'] == query, state
        assert state['include_subfolders'] == recursive, state
        assert (state['paths'] == paths if ordered else sorted(state['paths']) == sorted(paths)), state
        assert state['browser_state'] == sheet, state
        if folder is not None:
            assert state['folder_path'] and state['folder_path'].split(':', 1)[-1] == folder, state
            if sheet == 'Search':
                assert state['path_label'] and state['path_label']['text'] == state['folder_path'], state
        if paths:
            assert 0 <= state['selected_index'] < len(paths), state
        else:
            assert state['selected_index'] in (-1, 0), state
        return state

    async def clear(self):
        await self.key('y')
        await self.expect('', INITIAL)

    async def settled(self, limit=3000):
        for elapsed in range(0, limit + 1, 20):
            try:
                state = await self.state()
            except ViewModelPending:
                if elapsed == limit:
                    raise AssertionError('Browser view model did not finish rebuilding')
                await self.step(20)
                continue
            if state['task'] is None:
                self.walk_measurements.append({'query': state['query'],
                    'settle_frames_lower_bound': max(0, elapsed - 20), 'settle_frames_upper_bound': elapsed})
                (self.output / 'walk-frames.json').write_text(json.dumps(self.walk_measurements, indent=2))
                return state
            if elapsed < limit:
                await self.step(20)
        raise AssertionError(f'Recursive search did not settle within {limit} frames')

    async def begin_walk(self):
        await self.key('x', after=1)
        for _ in range(8):
            state = await self.state()
            if state['include_subfolders'] and state['task']:
                if state['task']['state'] == 'Running':
                    return state
            await self.step(1)
        raise AssertionError('A Running recursive walk was not witnessed within eight frames')

    async def inflight_key(self, key, query):
        before = await self.state()
        assert before['query'] == query and before['task'] and before['task']['state'] == 'Running', before
        ids = []
        try:
            for handler in ('apply_search', 'search_input'):
                pc = self.descriptor.handlers[handler]
                bp = await self.call('set_breakpoint', kind='exec', memory_type='arm9',
                                     start=pc, end=pc, pause_on_hit=True)
                ids.append(bp['id'])
            await self.debug('set_input', buttons=[key])
            for _ in range(12):
                await self.call('step', count=1)
                pc = (await self.call('get_state', cpu='arm9'))['state']['cpu.pc']
                if pc == self.descriptor.handlers['apply_search']:
                    raise AssertionError('Search was replaced before the requested key was handled')
                if pc == self.descriptor.handlers['search_input']:
                    boundary = await self.state()
                    if boundary['triggered_keys'] & self.descriptor.enums['InputKey'][key.upper()]:
                        require_inflight(self.descriptor, before, boundary, handler='search_input',
                                         pc=pc, key=key.upper(), replacement_entries=0)
                        (self.output / f'inflight-{key}.json').write_text(
                            json.dumps({'before': before, 'boundary': boundary, 'pc': pc}, indent=2))
                        break
                    # Leave the current breakpoint instruction before allowing another hit.
                    await self.call('step', count=1, unit='instructions')
            else:
                raise AssertionError('Input-handler boundary was never witnessed')
        finally:
            await self.debug('set_input', buttons=[])
            for identity in ids:
                await self.call('clear_breakpoint', id=identity)
        await self.step(8)

    async def navigate(self, name):
        state = await self.state()
        assert state['browser_state'] == 'Browser', state
        target = state['paths'].index(name)
        await self.key('down')
        # HorizontalGrid columns contain a fixed number of rows. Observe that
        # count through real focus movement rather than assuming pixel geometry.
        for _ in range(state['count']):
            if (await self.state())['selected_index'] == 0:
                break
            before = (await self.state())['selected_index']
            await self.key('left')
            after = (await self.state())['selected_index']
            if after == before:
                await self.key('up')
        assert (await self.state())['selected_index'] == 0
        if target:
            await self.key('right')
            rows = (await self.state())['selected_index']
            assert rows > 0, 'Grid column movement did not select a new item'
            await self.key('left')
            for _ in range(target // rows):
                await self.key('right')
            for _ in range(target % rows):
                await self.key('down')
        state = await self.state()
        assert state['selected_index'] == target, state
        await self.key('a', after=100)
        state = await self.state()
        assert state['browser_state'] == 'Browser' and state['query'] == '', state
        assert not state['include_subfolders'], state

    async def set_sort(self, descending, *, inflight=False, pending_paths=None):
        await self.key('b', after=30)
        await self.touch(240, 16, after=30)
        state = await self.state()
        assert state['browser_state'] == 'DisplaySettings', state
        x = 118 if descending else 86
        if inflight:
            before = await self.state()
            assert before['task'] and before['task']['state'] == 'Running', before
            ids = []
            try:
                for handler in ('change_display', 'apply_search'):
                    pc = self.descriptor.handlers[handler]
                    bp = await self.call('set_breakpoint', kind='exec', memory_type='arm9',
                                         start=pc, end=pc, pause_on_hit=True)
                    ids.append(bp['id'])
                await self.debug('hold_touch', x=x, y=118)
                await self.step(self.press_frames)
                await self.debug('release_touch')
                for _ in range(12):
                    await self.call('step', count=1)
                    pc = (await self.call('get_state', cpu='arm9'))['state']['cpu.pc']
                    if pc == self.descriptor.handlers['apply_search']:
                        raise AssertionError('Search replaced before the display-change handler')
                    if pc == self.descriptor.handlers['change_display']:
                        boundary = await self.state()
                        require_inflight(self.descriptor, before, boundary,
                                         handler='change_display', pc=pc, replacement_entries=0)
                        (self.output / 'inflight-sort.json').write_text(
                            json.dumps({'before': before, 'boundary': boundary, 'pc': pc}, indent=2))
                        break
                else:
                    raise AssertionError('Sort input did not reach the display-change handler')
            finally:
                await self.debug('release_touch')
                for identity in ids:
                    await self.call('clear_breakpoint', id=identity)
            await self.step(8)
        else:
            await self.touch(x, 118, after=1)
        if pending_paths is not None:
            await self.step(8)
            pending = await self.expect('CAP', pending_paths, recursive=True,
                                        ordered=True, sheet='DisplaySettings')
            assert pending['task'] and pending['task']['state'] == 'Running', pending
            (self.output / 'pending-sort.json').write_text(json.dumps(pending, indent=2))
        await self.key('b', after=30)
        await self.touch(207, 16, after=30)

    async def inflight_touch(self, x, y, *, query, recursive):
        before = await self.state()
        assert before['task'] and before['task']['state'] == 'Running', before
        pc = self.descriptor.handlers['apply_search']
        bp = await self.call('set_breakpoint', kind='exec', memory_type='arm9', start=pc, end=pc, pause_on_hit=True)
        try:
            await self.debug('hold_touch', x=x, y=y)
            await self.step(self.press_frames)
            await self.debug('release_touch')
            for _ in range(8):
                await self.call('step', count=1)
                cpu = (await self.call('get_state', cpu='arm9'))['state']
                if cpu['cpu.pc'] == pc:
                    boundary = await self.state()
                    # This is the first ApplySearch since before, before CancelSearchTask.
                    require_inflight(self.descriptor, before, boundary, handler='apply_search',
                                     pc=pc, replacement_entries=0)
                    argument = (await self.memory(cpu['cpu.r1'], 32)).split(b'\0')[0].decode()
                    assert argument == query and bool(cpu['cpu.r2']) == recursive, (argument, cpu)
                    (self.output / f'inflight-touch-{query}-{recursive}.json').write_text(
                        json.dumps({'before': before, 'boundary': boundary, 'pc': pc,
                                    'next_query': argument, 'next_recursive': recursive}, indent=2))
                    break
            else:
                raise AssertionError('Touch never reached its ApplySearch boundary')
        finally:
            await self.debug('release_touch')
            await self.call('clear_breakpoint', id=bp['id'])
        await self.step(8)


async def current_folder(case):
    await case.text('ALPHA')
    await case.expect('ALPHA', ['alpha.nds'])
    await case.clear()
    await case.text('space-dash ')
    await case.expect('SPACE-DASH ', ['space-dash one.nds', 'space-dash two.nds'])
    await case.text('\b')
    await case.expect('SPACE-DASH', ['space-dash one.nds', 'space-dash two.nds'])
    await case.capture('spaces-and-hyphen')
    await case.clear()
    await case.text('\b\b')
    await case.expect('', INITIAL)
    await case.key('x')
    await case.expect('', INITIAL, recursive=True)
    await case.clear()


async def empty_and_limit(case):
    await case.text('Q' * 32)
    await case.expect('Q' * 31, [])
    await case.capture('query-limit')
    await case.text('\b')
    await case.expect('Q' * 30, [])
    await case.key('b')
    await case.expect('Q' * 30, [], sheet='Browser')
    for key in ('left', 'right'):
        await case.key(key)
        await case.expect('Q' * 30, [], sheet='Browser')
    await case.key('a')
    await case.expect('Q' * 30, [])
    await case.key('b', after=30)
    for key in ('l', 'r'):
        await case.key(key)
        await case.expect('Q' * 30, [], sheet='Browser')
    await case.touch(207, 16, after=30)
    await case.expect('Q' * 30, [])
    await case.clear()


async def recursive(case):
    await case.text('GAME')
    await case.expect('GAME', [])
    await case.begin_walk()
    await case.settled()
    assert case.walk_measurements[-1]['settle_frames_lower_bound'] >= 32, 'Stress fixture has insufficient input margin'
    state = await case.expect('GAME', ['A/game.nds', 'B/game.nds'], recursive=True)
    assert {file['parent'] for file in state['files']} == {'A', 'B'}, state
    await case.capture('duplicate-basenames')
    await case.clear()
    await case.text('ONE')
    await case.begin_walk()
    await case.settled()
    await case.expect('ONE', ['A/nested one.nds', 'space-dash one.nds'], recursive=True)


async def cancellation(case):
    await case.text('GAME')
    await case.begin_walk()
    await case.inflight_key('y', 'GAME')
    await case.expect('', INITIAL)
    await case.step(600)
    await case.expect('', INITIAL)
    await case.text('ONE')
    await case.begin_walk()
    await case.inflight_key('b', 'ONE')
    await case.settled()
    await case.expect('ONE', ['A/nested one.nds', 'space-dash one.nds'], recursive=True, sheet='Browser')
    await case.touch(207, 16, after=30)
    await case.expect('ONE', ['A/nested one.nds', 'space-dash one.nds'], recursive=True)
    await case.clear()


async def replacement(case):
    await case.text('GAME')
    await case.begin_walk()
    await case.inflight_touch(28, 115, query='GAMEQ', recursive=True)
    await case.settled()
    await case.expect('GAMEQ', ['A/game.nds', 'B/game.nds'], recursive=True)
    await case.clear()
    await case.text('ONE')
    await case.begin_walk()
    await case.inflight_key('x', 'ONE')
    await case.expect('ONE', ['space-dash one.nds'])
    await case.step(600)
    await case.expect('ONE', ['space-dash one.nds'])


async def scope_hotkey(case):
    await case.key('x')
    await case.expect('', INITIAL, recursive=True)
    await case.key('x')
    await case.expect('', INITIAL)
    await case.debug('set_input', buttons=['x'])
    try:
        await case.step(60)
        await case.expect('', INITIAL, recursive=True)
    finally:
        await case.debug('set_input', buttons=[])
    await case.step(8)
    # Choosing the on-screen X still types X, independently of physical X.
    await case.touch(68, 151)
    await case.settled()
    await case.expect('X', ['stress/zzz-cap/cap-exact.nds'], recursive=True)
    await case.key('x')
    await case.expect('X', [])
    await case.key('a')
    await case.expect('XX', [])
    await case.key('x')
    await case.settled()
    await case.expect('XX', [], recursive=True)
    await case.text('\b\b')
    await case.expect('', INITIAL, recursive=True)
    await case.key('b', after=30)
    await case.touch(207, 16, after=30)
    await case.expect('', INITIAL, recursive=True)
    await case.clear()
    # Every keyboard edge stays inside the keyboard after removing the icon.
    for keys, expected in ((('up',) * 4, '1'), (('left',), '0'),
                           (('right',), '1'), (('down',) * 4, 'Z')):
        for key in keys:
            await case.key(key)
        await case.key('a')
        assert (await case.state())['query'] == expected
        await case.clear()


async def fuzzy(case):
    await case.text('ALHPA')
    await case.expect('ALHPA', ['alpha.nds'], ordered=True, folder='/games')
    await case.begin_walk()
    await case.settled()
    await case.expect('ALHPA', ['stress/alhpa demo.nds', 'alpha.nds'], recursive=True, ordered=True)
    await case.capture('literal-before-approximate')
    await case.set_sort(True)
    await case.settled()
    await case.expect('ALHPA', ['stress/alhpa demo.nds', 'alpha.nds'], recursive=True, ordered=True)


def cap_paths(descending=False):
    approximate = [f'stress/aaa-cap/cpa-{index:03}.nds' for index in range(260)]
    approximate.sort(reverse=descending)
    return ['stress/zzz-cap/cap-exact.nds'] + approximate[:255]


async def capped_ranking(case):
    await case.text('CAP')
    await case.begin_walk()
    await case.settled()
    await case.expect('CAP', cap_paths(), recursive=True, ordered=True)
    await case.capture('late-literal-retained')
    retained_descending = [cap_paths()[0]] + list(reversed(cap_paths()[1:]))
    await case.set_sort(True, pending_paths=retained_descending)
    await case.settled()
    await case.expect('CAP', cap_paths(True), recursive=True, ordered=True)
    # Restart a walk, then change its ordering before waiting for completion.
    await case.key('x')
    await case.begin_walk()
    await case.set_sort(False, inflight=True)
    await case.settled()
    await case.expect('CAP', cap_paths(), recursive=True, ordered=True)


LONG_COMPONENTS = [f'path_segment_{index:02}_' + 'x' * 18 for index in range(9)]


async def folder_path(case):
    await case.expect('', INITIAL, folder='/games')
    await case.key('b', after=30)
    await case.navigate('A')
    await case.touch(207, 16, after=30)
    await case.expect('', ['game.nds', 'nested one.nds'], folder='/games/A')
    await case.capture('nested-folder')
    await case.key('b', after=30)
    await case.touch(16, 16, after=100)
    await case.navigate('stress')
    await case.navigate('longpath')
    expected = '/games/stress/longpath'
    for component in LONG_COMPONENTS:
        await case.navigate(component)
        expected += '/' + component
    assert len(expected.encode()) > 255
    await case.touch(207, 16, after=30)
    await case.expect('', ['x.nds'], folder=expected)
    await case.capture('long-path-start')
    await case.step(600)
    scrolling = await case.capture('long-path-scroll')
    label = scrolling['path_label']
    assert label['text'] == scrolling['folder_path'], scrolling
    assert label['string_width'] > label['width'] and label['offset'] > 0, scrolling
    await case.text('X')
    await case.expect('X', ['x.nds'], folder=expected)
    await case.key('x')
    await case.settled()
    edited = await case.expect('X', [], recursive=True, folder=expected)
    assert edited['path_label']['offset'] >= label['offset'], edited
    # The existing marquee advances one pixel every three frames, stopping one
    # pixel beyond the final viewport. Reach that stop without cycling its wait.
    end_offset = edited['path_label']['string_width'] - edited['path_label']['width'] + 1
    frames = (end_offset - edited['path_label']['offset']) * 3
    assert frames >= 0, edited
    while frames:
        count = min(frames, 600)
        await case.step(count)
        frames -= count
    ending = await case.capture('long-path-end')
    assert ending['path_label']['offset'] == end_offset, ending
    assert ending['path_label']['text'] == ending['folder_path'], ending
    await case.key('b', after=30)
    await case.touch(207, 16, after=30)
    await case.expect('X', [], recursive=True, folder=expected)
    await case.key('y')
    await case.key('b', after=30)
    for _ in range(len(LONG_COMPONENTS) + 3):
        await case.touch(16, 16, after=100)
    await case.touch(207, 16, after=30)
    state = await case.state()
    assert state['folder_path'].split(':', 1)[-1] == '/', state
    await case.capture('root-folder')


CASES = {'current-folder': current_folder, 'empty-and-limit': empty_and_limit,
         'recursive': recursive, 'cancellation': cancellation, 'replacement': replacement,
         'scope-hotkey': scope_hotkey, 'fuzzy': fuzzy, 'capped-ranking': capped_ranking,
         'folder-path': folder_path}


async def run(args):
    install = json.loads(args.install_manifest.read_text())
    args.output.mkdir(parents=True, exist_ok=False)
    # A concurrent build must never replace the ROM belonging to this descriptor.
    artifacts = args.output / 'inputs'
    artifacts.mkdir()
    shutil.copyfile(__file__, artifacts / 'runner.py')
    shutil.copyfile(args.install_manifest, artifacts / 'install.json')
    for name, source in (('LAUNCHER.nds', args.rom), ('arm9.elf', args.elf),
                         ('card.img', args.fixture / 'card.img'),
                         ('identity.json', args.fixture / 'identity.json')):
        shutil.copyfile(source, artifacts / name)
    args.rom = artifacts / 'LAUNCHER.nds'
    descriptor = build_descriptor(artifacts / 'arm9.elf', args.rom, artifacts / 'identity.json')
    fixture_identity = json.loads((artifacts / 'identity.json').read_text())
    lock = Path(__file__).resolve().parents[2] / 'tools/emulator/versions.json'
    assert fixture_identity['lock_sha256'] == hashlib.sha256(lock.read_bytes()).hexdigest(), 'Fixture emulator lock differs from this checkout'
    assert hashlib.sha256((artifacts / 'card.img').read_bytes()).hexdigest() == fixture_identity['image_sha256']
    (args.output / 'descriptor.json').write_text(json.dumps(asdict(descriptor), indent=2))
    report = {'identity': descriptor.identity, 'fixture': fixture_identity,
              'runner_sha256': hashlib.sha256((artifacts / 'runner.py').read_bytes()).hexdigest(),
              'install': install,
              'runtime_sha256': {name: hashlib.sha256(Path(install[name]).read_bytes()).hexdigest()
                                 for name in ('core_executable', 'bridge_executable', 'desmume_executable')},
              'press_frames': args.press_frames, 'cases': []}
    for name in args.case or CASES:
        output = args.output / name
        image = args.output / f'{name}.img'
        shutil.copyfile(artifacts / 'card.img', image)
        entry = {'name': name, 'status': 'running', 'image_flush': 'unconfirmed'}
        report['cases'].append(entry)
        generation = None
        try:
            async with connect(install, output) as client:
                case = SearchCase(client, descriptor, output, press_frames=args.press_frames)
                try:
                    await case.call('bootstrap')
                    plan = await case.call('launch_plan', content_path=str(args.rom), system='nds', nds_storage_image=str(image))
                    generation = (await client.call('launch', plan['preferred_launcher']['args']))['runtime_instance']['launch_id']
                    case.revision = (await case.call('status'))['capability_revision']
                    await case.call('debug', operation='describe')
                    await case.step(120)
                    await case.expect('', INITIAL, sheet='Browser')
                    await case.touch(207, 16, after=30)
                    await case.expect('', INITIAL)
                    if args.failure_probe:
                        raise AssertionError('Requested CI evidence publication probe')
                    await CASES[name](case)
                    await case.capture('final')
                    entry['status'] = 'passed'
                except BaseException:
                    try:
                        await case.call('pause')
                        await case.capture('failure')
                        for cpu in ('arm9', 'arm7'):
                            state = await case.call('get_state', cpu=cpu)
                            (output / f'failure-{cpu}.json').write_text(json.dumps(state, indent=2) + '\n')
                    except Exception as capture_error:
                        entry['capture_error'] = str(capture_error)
                    raise
                finally:
                    if generation:
                        stopped = await case.call('stop', launch_id=generation)
                        entry['stopped'] = stopped['stopped'] and stopped['processes']['completed']
                        if not entry['stopped']:
                            raise AssertionError('Owned generation did not stop')
        except BaseException as error:
            cause = error
            while isinstance(cause, BaseExceptionGroup) and len(cause.exceptions) == 1:
                cause = cause.exceptions[0]
            entry.update(status='failed', error=f'{type(cause).__name__}: {cause}')
            (args.output / f'{name}-failure.txt').write_text(traceback.format_exc())
            if not isinstance(error, Exception):
                entry['status'] = 'interrupted'
                raise
        finally:
            (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(f"{name}: {entry['status']}", flush=True)
    if any(entry['status'] != 'passed' for entry in report['cases']):
        raise SystemExit(1)


def main():
    if not __debug__:
        raise RuntimeError('Run search assertions without Python optimization (-O)')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install-manifest', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--case', action='append', choices=CASES)
    parser.add_argument('--press-frames', type=int, default=6)
    parser.add_argument('--failure-probe', action='store_true',
                        help='Deliberately fail after boot to verify capture and owned-generation cleanup')
    args = parser.parse_args()
    if not 1 <= args.press_frames <= 120:
        parser.error('--press-frames must be between 1 and 120')
    for key in ('rom', 'elf', 'fixture', 'output'):
        setattr(args, key, getattr(args, key).resolve())
    asyncio.run(run(args))


if __name__ == '__main__':
    main()
