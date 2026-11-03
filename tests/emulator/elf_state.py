"""Derive a narrow browser observer from matching ARM9 debug information."""
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct

from elftools.elf.elffile import ELFFile


class OracleError(RuntimeError):
    pass


class ViewModelPending(OracleError):
    """A frozen CPU boundary interrupted view-model replacement."""


class _MissingMember(OracleError, KeyError):
    pass


@dataclass(frozen=True)
class Field:
    offset: int
    size: int


@dataclass(frozen=True)
class Descriptor:
    process_address: int
    app_vptr: int
    task_vptr: int
    fields: dict
    enums: dict
    handlers: dict
    identity: dict


def verify_identity(elf_path, rom_path, identity):
    if not isinstance(identity, dict):
        identity = json.loads(Path(identity).read_text(encoding='utf-8'))
    for key, path in (('elf_sha256', elf_path), ('launcher_sha256', rom_path)):
        if hashlib.sha256(Path(path).read_bytes()).hexdigest() != identity.get(key):
            raise OracleError(f'ROM/ELF identity mismatch: {key}')
    return {key: identity[key] for key in ('elf_sha256', 'launcher_sha256')}


def _name(die):
    value = die.attributes.get('DW_AT_name')
    return value.value.decode('utf-8') if value and isinstance(value.value, bytes) else None


def _function_addresses(elf, symbols):
    return {symbol['st_value'] for symbol in symbols
            if symbol['st_info']['type'] == 'STT_FUNC' and symbol['st_value']
            and isinstance(symbol['st_shndx'], int)
            and elf.get_section(symbol['st_shndx'])['sh_flags'] & 4}


class _Dwarf:
    def __init__(self, dwarf):
        wanted = {'ProcessManager', 'App', 'RomBrowserController', 'RomBrowserViewModel',
                  'FileInfoManager', 'FileInfo', 'TaskBase', 'QueueTaskBase', 'SharedPtrBase',
                  'InputProvider', 'InputRepeater', 'RomBrowserStateMachine', 'TaskState',
                  'InputKey', 'RomBrowserState', 'SearchBottomSheetView', 'LabelView', 'Label2DView'}
        self.types = {}
        for cu in dwarf.iter_CUs():
            for die in cu.iter_DIEs():
                if 'DW_AT_byte_size' not in die.attributes:
                    continue
                name = _name(die)
                if name in wanted:
                    self.types.setdefault(name, die)
                if name and name.startswith('FuncTask<') and 'ApplySearch' in name:
                    self.types.setdefault('SearchFuncTask', die)
                if wanted <= self.types.keys() and 'SearchFuncTask' in self.types:
                    return
        missing = wanted - self.types.keys()
        if missing or 'SearchFuncTask' not in self.types:
            raise OracleError(f'Unresolved debug types: {sorted(missing)} / search task')

    def unwrap(self, die):
        seen = set()
        while die.tag in ('DW_TAG_typedef', 'DW_TAG_const_type', 'DW_TAG_volatile_type',
                          'DW_TAG_restrict_type'):
            if die.offset in seen or 'DW_AT_type' not in die.attributes:
                raise OracleError('Unresolved recursive debug type')
            seen.add(die.offset)
            die = die.get_DIE_from_attribute('DW_AT_type')
        if 'DW_AT_declaration' in die.attributes:
            die = self.types.get(_name(die), die)
        return die

    def size(self, die):
        die = self.unwrap(die)
        if 'DW_AT_byte_size' in die.attributes:
            return die.attributes['DW_AT_byte_size'].value
        if die.tag == 'DW_TAG_array_type':
            bounds = [c for c in die.iter_children() if c.tag == 'DW_TAG_subrange_type']
            if len(bounds) == 1 and 'DW_AT_upper_bound' in bounds[0].attributes:
                return (bounds[0].attributes['DW_AT_upper_bound'].value + 1) * self.size(
                    die.get_DIE_from_attribute('DW_AT_type'))
        raise OracleError(f'Unresolved byte size for {_name(die)}')

    @staticmethod
    def location(die):
        location = die.attributes.get('DW_AT_data_member_location')
        if location is None or not isinstance(location.value, int):
            raise OracleError('Unresolved or non-constant member location')
        return location.value

    def member(self, die, name):
        matches = []
        die = self.unwrap(die)
        for child in die.iter_children():
            if child.tag == 'DW_TAG_member' and _name(child) == name:
                matches.append((self.location(child), self.unwrap(child.get_DIE_from_attribute('DW_AT_type'))))
            elif child.tag == 'DW_TAG_inheritance':
                try:
                    offset, type_die = self.member(child.get_DIE_from_attribute('DW_AT_type'), name)
                    matches.append((self.location(child) + offset, type_die))
                except KeyError:
                    pass
        if not matches:
            raise _MissingMember(f'Unresolved member {_name(die)}.{name}')
        unique = {(offset, type_die.offset): (offset, type_die) for offset, type_die in matches}
        if len(unique) != 1:
            raise OracleError(f'Ambiguous member {_name(die)}.{name}')
        return next(iter(unique.values()))

    def path(self, type_name, *names):
        die = self.types[type_name]
        total = 0
        for name in names:
            offset, die = self.member(die, name)
            total += offset
        return total, die

    def pointer_storage(self, die):
        leaves = []
        def walk(current, offset, stack):
            current = self.unwrap(current)
            if current.offset in stack:
                raise OracleError('Recursive pointer wrapper')
            if current.tag == 'DW_TAG_pointer_type':
                if self.size(current) != 4:
                    raise OracleError('Observer requires 32-bit target pointers')
                leaves.append(offset)
                return
            if current.tag not in ('DW_TAG_class_type', 'DW_TAG_structure_type'):
                return
            for child in current.iter_children():
                if child.tag in ('DW_TAG_member', 'DW_TAG_inheritance') and 'DW_AT_data_member_location' in child.attributes:
                    walk(child.get_DIE_from_attribute('DW_AT_type'), offset + self.location(child), stack | {current.offset})
        walk(die, 0, set())
        leaves = sorted(set(leaves))
        if len(leaves) != 1:
            raise OracleError(f'Ambiguous pointer wrapper {_name(die)}: {leaves}')
        return leaves[0]

    def field(self, type_name, *names, pointer=False, shared=False):
        offset, die = self.path(type_name, *names)
        if shared:
            inner, die = self.member(die, '_object')
            offset += inner
            return Field(offset, self.size(die))
        if pointer:
            return Field(offset + self.pointer_storage(die), 4)
        return Field(offset, self.size(die))


def build_descriptor(elf_path, rom_path, identity):
    identity = verify_identity(elf_path, rom_path, identity)
    try:
        with Path(elf_path).open('rb') as stream:
            elf = ELFFile(stream)
            if elf.elfclass != 32 or not elf.little_endian or elf['e_machine'] != 'EM_ARM' or not elf.has_dwarf_info():
                raise OracleError('Expected little-endian ARM32 ELF with debug information')
            symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
            dwarf = _Dwarf(elf.get_dwarf_info())
            fields = {}
            for label, type_name, path, pointer, shared in (
                ('process_app', 'ProcessManager', ('_curProcess',), True, False),
                ('app_controller', 'App', ('_romBrowserController',), False, False),
                ('app_keys', 'App', ('_inputRepeater', '_triggeredKeys'), False, False),
                ('app_dialog', 'App', ('_dialogPresenter', '_currentDialog'), False, True),
                ('controller_query', 'RomBrowserController', ('_searchQuery',), False, False),
                ('controller_recursive', 'RomBrowserController', ('_searchIncludeSubFolders',), False, False),
                ('controller_path', 'RomBrowserController', ('_currentFolderPath',), True, False),
                ('controller_vm', 'RomBrowserController', ('_romBrowserViewModel',), False, True),
                ('controller_state', 'RomBrowserController', ('_stateMachine', '_curState'), False, False),
                ('controller_task', 'RomBrowserController', ('_searchTask', '_task'), True, False),
                ('controller_queue', 'RomBrowserController', ('_searchTask', '_taskQueue'), True, False),
                ('viewmodel_manager', 'RomBrowserViewModel', ('_fileInfoManager',), True, False),
                ('viewmodel_selected', 'RomBrowserViewModel', ('_selectedItem',), False, False),
                ('manager_items', 'FileInfoManager', ('_items',), True, False),
                ('manager_count', 'FileInfoManager', ('_itemCount',), False, False),
                ('file_name', 'FileInfo', ('_name',), True, False),
                ('file_parent', 'FileInfo', ('_parentPath',), True, False),
                ('search_path_label', 'SearchBottomSheetView', ('_pathLabel',), False, True),
                ('label_text', 'Label2DView', ('_textBuffer',), True, False),
                ('label_capacity', 'Label2DView', ('_maxStringLength',), False, False),
                ('label_marquee_offset', 'Label2DView', ('_marqueeOffset',), False, False),
                ('label_string_width', 'Label2DView', ('_newStringWidth',), False, False),
                ('label_width', 'Label2DView', ('_width',), False, False),
                ('task_state', 'TaskBase', ('_state',), False, False),
                ('task_query', 'SearchFuncTask', ('_function', '__queryCopy'), False, True),
            ):
                fields[label] = dwarf.field(type_name, *path, pointer=pointer, shared=shared)
            _, query_wrapper = dwarf.path('SearchFuncTask', '_function', '__queryCopy')
            template = [c for c in query_wrapper.iter_children() if c.tag == 'DW_TAG_template_type_param' and _name(c) == 'T']
            if len(template) != 1:
                raise OracleError('Unresolved captured query type')
            array = template[0].get_DIE_from_attribute('DW_AT_type')
            offset, array = dwarf.member(array, '_M_elems')
            fields['query_chars'] = Field(offset, dwarf.size(array))
            functions = _function_addresses(elf, symbols.values())
            def vptr(symbol):
                section = elf.get_section(symbol['st_shndx'])
                offset = symbol['st_value'] - section['sh_addr']
                table = section.data()[offset:offset + symbol['st_size']]
                entries = [symbol['st_value'] + i for i in range(0, len(table), 4)
                           if struct.unpack_from('<I', table, i)[0] in functions]
                if not entries:
                    raise OracleError('Unresolved virtual table address point')
                return min(entries)
            tasks = [s for name, s in symbols.items() if name.startswith('_ZTV8FuncTask') and 'RomBrowserController11ApplySearch' in name]
            if len(tasks) != 1:
                raise OracleError('Ambiguous search task vtable')
            enums = {name: {_name(c): c.attributes['DW_AT_const_value'].value
                            for c in dwarf.types[name].iter_children() if c.tag == 'DW_TAG_enumerator'}
                     for name in ('TaskState', 'RomBrowserState', 'InputKey')}
            handlers = {label: symbols[name]['st_value'] for label, name in (
                ('search_input', '_ZN21SearchBottomSheetView11HandleInputERK13InputProviderR12FocusManager'),
                ('change_display', '_ZN20RomBrowserController30HandleChangeDisplayModeTriggerEv'),
                ('apply_search', '_ZN20RomBrowserController11ApplySearchEPKcb'))}
            return Descriptor(symbols['gProcessManager']['st_value'], vptr(symbols['_ZTV3App']),
                              vptr(tasks[0]), fields, enums, handlers, identity)
    except (KeyError, AttributeError, ValueError) as error:
        raise OracleError(f'Unresolved ELF observation path: {error}') from error


async def read_browser_state(descriptor, read, *, frozen):
    if not frozen:
        raise OracleError('Browser state requires a frozen guest')
    async def memory(address, length):
        if not isinstance(address, int) or address < 0x02000000 or address + length > 0x02400000 or length < 0:
            raise OracleError(f'Observation address outside main RAM: {address:#x}+{length}')
        value = await read(address, length)
        if not isinstance(value, bytes) or len(value) != length:
            raise OracleError('Incomplete memory observation')
        return value
    async def number(address, size, signed=False):
        return int.from_bytes(await memory(address, size), 'little', signed=signed)
    async def value(base, name, signed=False):
        field = descriptor.fields[name]
        return await number(base + field.offset, field.size, signed)
    async def string(address, bound):
        result = bytearray()
        for offset in range(0, bound, 64):
            block = await memory(address + offset, min(64, bound-offset, 0x02400000-address-offset))
            end = block.find(b'\0')
            if end >= 0:
                result.extend(block[:end])
                try:
                    return result.decode('utf-8')
                except UnicodeDecodeError as error:
                    raise OracleError('Invalid UTF-8 in guest string') from error
            result.extend(block)
        raise OracleError('Unterminated guest string')
    def enum(name, raw):
        values = [key for key, item in descriptor.enums[name].items() if item == raw]
        if len(values) != 1:
            raise OracleError(f'Unknown {name} value: {raw}')
        return values[0]
    app = await value(descriptor.process_address, 'process_app')
    if await number(app, 4) != descriptor.app_vptr:
        raise OracleError('Current process is not the matching App')
    controller = app + descriptor.fields['app_controller'].offset
    vm = await value(controller, 'controller_vm')
    if not vm:
        raise ViewModelPending('Browser view model is being rebuilt')
    manager = await value(vm, 'viewmodel_manager')
    count = await value(manager, 'manager_count')
    if count > 4096:
        raise OracleError(f'Invalid file count: {count}')
    items = await value(manager, 'manager_items')
    files = []
    for index in range(count):
        item = await number(items + index*4, 4)
        name = await string(await value(item, 'file_name'), 256)
        parent = await value(item, 'file_parent')
        parent = await string(parent, 1024) if parent else None
        files.append({'name': name, 'parent': parent})
    query_field = descriptor.fields['controller_query']
    query = await string(controller + query_field.offset, query_field.size)
    recursive = await value(controller, 'controller_recursive')
    if recursive not in (0, 1):
        raise OracleError('Invalid recursive-search flag')
    task_address = await value(controller, 'controller_task')
    task = None
    if task_address:
        if await number(task_address, 4) != descriptor.task_vptr:
            raise OracleError('Search task has an unexpected dynamic type')
        query_address = await value(task_address, 'task_query')
        task_query = descriptor.fields['query_chars']
        task = {'address': task_address, 'vptr': descriptor.task_vptr,
                'queue': await value(controller, 'controller_queue'),
                'query_address': query_address,
                'query': await string(query_address + task_query.offset, task_query.size),
                'state': enum('TaskState', await value(task_address, 'task_state'))}
    folder_path = await value(controller, 'controller_path')
    folder_path = await string(folder_path, 0x02400000 - folder_path) if folder_path else None
    browser_state = enum('RomBrowserState', await value(controller, 'controller_state'))
    path_label = None
    if browser_state == 'Search':
        dialog = await value(app, 'app_dialog')
        if dialog:
            label = await value(dialog, 'search_path_label')
            capacity = await value(label, 'label_capacity')
            text_address = await value(label, 'label_text')
            raw = await memory(text_address, (capacity + 1) * 2)
            end = next((index for index in range(0, len(raw), 2)
                        if raw[index:index + 2] == b'\0\0'), None)
            if end is None:
                raise OracleError('Unterminated UTF-16 path label')
            try:
                text = raw[:end].decode('utf-16-le')
            except UnicodeDecodeError as error:
                raise OracleError('Invalid UTF-16 path label') from error
            path_label = {'text': text, 'capacity': capacity,
                          'offset': await value(label, 'label_marquee_offset', signed=True),
                          'string_width': await value(label, 'label_string_width'),
                          'width': await value(label, 'label_width')}
    return {'app_address': app, 'controller_address': controller, 'query': query,
            'folder_path': folder_path, 'path_label': path_label,
            'include_subfolders': bool(recursive), 'files': files,
            'paths': [(item['parent'].rstrip('/') + '/' if item['parent'] else '') + item['name'] for item in files],
            'count': count, 'selected_index': await value(vm, 'viewmodel_selected', signed=True),
            'browser_state': browser_state,
            'triggered_keys': await value(app, 'app_keys'), 'task': task}


def require_inflight(descriptor, before, boundary, *, handler, pc, key=None, replacement_entries=None):
    """Check a pre-action stop; callers monitor ApplySearch throughout this window."""
    if replacement_entries is None:
        raise OracleError('Task lifetime monitoring is required')
    if replacement_entries:
        raise OracleError('Intervening search replacement invalidates the original task witness')
    if pc != descriptor.handlers[handler]:
        raise OracleError('Witness is outside an input-handler entry boundary')
    for state in (before, boundary):
        if not state.get('task') or state['task']['state'] != 'Running':
            raise OracleError('Original recursive task is not Running')
        if not state['include_subfolders'] or state['task']['query'] != state['query']:
            raise OracleError('Task query does not identify the active recursive walk')
    if (before['controller_address'], before['query'], before['task']) != (
            boundary['controller_address'], boundary['query'], boundary['task']):
        raise OracleError('Original task identity changed before input handling')
    if key is not None and not boundary['triggered_keys'] & descriptor.enums['InputKey'][key]:
        raise OracleError('Requested key was not triggered at the input-handler boundary')
