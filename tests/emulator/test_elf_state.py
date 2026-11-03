"""Read-only state decoding rejects stale identities and misleading task witnesses."""
import asyncio
from pathlib import Path
import struct
import tempfile
import unittest
from types import SimpleNamespace

from elf_state import Descriptor, Field, OracleError, ViewModelPending, _Dwarf, _function_addresses, read_browser_state, require_inflight, verify_identity


def fixture():
    fields = {name: Field(offset, size) for name, offset, size in (
        ('process_app', 4, 4), ('app_controller', 32, 128), ('app_keys', 12, 2),
        ('controller_query', 0, 32), ('controller_recursive', 32, 1),
        ('controller_vm', 36, 4), ('controller_state', 40, 1),
        ('controller_task', 44, 4), ('controller_queue', 48, 4),
        ('controller_path', 52, 4),
        ('app_dialog', 160, 4), ('search_path_label', 0, 4),
        ('label_text', 0, 4), ('label_capacity', 4, 4),
        ('label_marquee_offset', 8, 4), ('label_string_width', 12, 4),
        ('label_width', 16, 4),
        ('viewmodel_manager', 0, 4), ('viewmodel_selected', 4, 4),
        ('manager_items', 0, 4), ('manager_count', 4, 4),
        ('file_name', 0, 4), ('file_parent', 4, 4),
        ('task_state', 4, 1), ('task_query', 8, 4), ('query_chars', 0, 32),
    )}
    d = Descriptor(0x02000000, 0x02009000, 0x02009040, fields,
                   {'TaskState': {'NotStarted': 0, 'Running': 1, 'Completed': 2},
                    'RomBrowserState': {'Browser': 1, 'Search': 7}, 'InputKey': {'B': 2}},
                   {'search_input': 0x02008000, 'apply_search': 0x02008100}, {})
    data = bytearray(0x10000)
    def put(address, value, size=4):
        data[address-0x02000000:address-0x02000000+size] = value.to_bytes(size, 'little', signed=value < 0)
    put(0x02000004, 0x02001000)
    put(0x02001000, 0x02009000)
    put(0x0200100c, 2, 2)
    data[0x1020:0x1025] = b'game\0'
    put(0x02001040, 1, 1)
    put(0x02001044, 0x02002000)
    put(0x02001048, 7, 1)
    put(0x0200104c, 0x02005000)
    put(0x02001050, 0x02007000)
    put(0x02001054, 0x02006400)
    data[0x6400:0x640b] = b'fat:/games\0'
    put(0x020010a0, 0x02006800)
    put(0x02006800, 0x02006900)
    put(0x02006900, 0x02006c00)
    put(0x02006904, 512)
    put(0x02006908, 177)
    put(0x0200690c, 2000)
    put(0x02006910, 216)
    label = 'fat:/games\0'.encode('utf-16-le')
    data[0x6c00:0x6c00 + len(label)] = label
    put(0x02002000, 0x02003000)
    put(0x02002004, 0)
    put(0x02003000, 0x02004000)
    put(0x02003004, 2)
    for index, parent in enumerate((b'A\0', b'B\0')):
        file = 0x02006000 + index*32
        put(0x02004000+index*4, file)
        put(file, 0x02006100)
        put(file+4, 0x02006200+index*32)
        offset = 0x6200+index*32
        data[offset:offset+len(parent)] = parent
    data[0x6100:0x6109] = b'game.nds\0'
    put(0x02005000, 0x02009040)
    put(0x02005004, 1, 1)
    put(0x02005008, 0x02006300)
    data[0x6300:0x6305] = b'game\0'
    async def read(address, length):
        return bytes(data[address-0x02000000:address-0x02000000+length])
    return d, data, read


class OracleTests(unittest.TestCase):
    def test_null_view_model_is_an_explicit_publication_boundary(self):
        d, data, read = fixture()
        struct.pack_into('<I', data, 0x1020 + d.fields['controller_vm'].offset, 0)
        with self.assertRaises(ViewModelPending):
            asyncio.run(read_browser_state(d, read, frozen=True))

    def test_search_path_label_decodes_actual_utf16_text_and_marquee(self):
        d, _, read = fixture()
        state = asyncio.run(read_browser_state(d, read, frozen=True))
        self.assertEqual(state['path_label'], {'text': 'fat:/games', 'capacity': 512,
                         'offset': 177, 'string_width': 2000, 'width': 216})

    def test_path_label_is_not_dereferenced_outside_search_or_without_dialog(self):
        for address, value in ((0x1048, 1), (0x10a0, 0)):
            d, data, read = fixture()
            data[address:address + (1 if address == 0x1048 else 4)] = value.to_bytes(
                1 if address == 0x1048 else 4, 'little')
            data[0x6800:0x6804] = (0x04000000).to_bytes(4, 'little')
            state = asyncio.run(read_browser_state(d, read, frozen=True))
            self.assertIsNone(state['path_label'])

    def test_loaded_folder_path_is_owned_and_can_exceed_launch_capacity(self):
        d, data, read = fixture()
        path = 'fat:/games/' + 'path-' * 70 + '保存'
        encoded = path.encode() + b'\0'
        data[0x6400:0x6400 + len(encoded)] = encoded
        state = asyncio.run(read_browser_state(d, read, frozen=True))
        self.assertEqual(state['folder_path'], path)
        data[0x1054:0x1058] = b'\0' * 4
        state = asyncio.run(read_browser_state(d, read, frozen=True))
        self.assertIsNone(state['folder_path'])

    def test_absolute_zero_function_symbol_is_not_a_vtable_entry(self):
        symbols = [dict(st_info={'type': 'STT_FUNC'}, st_value=value, st_shndx=section)
                   for value, section in ((0, 'SHN_ABS'), (0, 'SHN_UNDEF'), (0x02008000, 1))]
        elf = SimpleNamespace(get_section=lambda _: {'sh_flags': 4})
        self.assertEqual(_function_addresses(elf, symbols), {0x02008000})

    def test_known_recursive_results_and_original_task_decode(self):
        d, _, read = fixture()
        state = asyncio.run(read_browser_state(d, read, frozen=True))
        self.assertEqual(state['query'], 'game')
        self.assertEqual(state['paths'], ['A/game.nds', 'B/game.nds'])
        self.assertEqual(state['selected_index'], 0)
        self.assertEqual(state['browser_state'], 'Search')
        self.assertEqual(state['task']['state'], 'Running')
        self.assertEqual(state['task']['query'], 'game')

    def test_running_guest_and_invalid_pointer_fail(self):
        d, data, read = fixture()
        with self.assertRaisesRegex(OracleError, 'frozen'):
            asyncio.run(read_browser_state(d, read, frozen=False))
        data[4:8] = struct.pack('<I', 0x04000000)
        with self.assertRaisesRegex(OracleError, 'main RAM'):
            asyncio.run(read_browser_state(d, read, frozen=True))

    def test_unknown_task_type_and_state_fail(self):
        for address, value, size in ((0x5000, 0x02009080, 4), (0x5004, 255, 1)):
            d, data, read = fixture()
            data[address:address+size] = value.to_bytes(size, 'little')
            with self.assertRaises(OracleError):
                asyncio.run(read_browser_state(d, read, frozen=True))

    def test_inflight_requires_original_running_task_and_monitored_lifetime(self):
        d, _, read = fixture()
        before = asyncio.run(read_browser_state(d, read, frozen=True))
        require_inflight(d, before, before, handler='search_input', pc=d.handlers['search_input'], key='B', replacement_entries=[])
        for change in ({'state': 'NotStarted'}, {'state': 'Completed'}, {'address': 0x02005040}, {'query_address': 0x02006400}, {'query': 'replacement'}):
            boundary = {**before, 'task': {**before['task'], **change}}
            with self.assertRaises(OracleError):
                require_inflight(d, before, boundary, handler='search_input', pc=d.handlers['search_input'], key='B', replacement_entries=[])
        with self.assertRaisesRegex(OracleError, 'monitor'):
            require_inflight(d, before, before, handler='search_input', pc=d.handlers['search_input'], key='B')
        with self.assertRaisesRegex(OracleError, 'replacement'):
            require_inflight(d, before, before, handler='search_input', pc=d.handlers['search_input'], key='B', replacement_entries=[d.handlers['apply_search']])
        with self.assertRaisesRegex(OracleError, 'boundary'):
            require_inflight(d, before, before, handler='search_input', pc=d.handlers['apply_search'], key='B', replacement_entries=[])
        with self.assertRaisesRegex(OracleError, 'triggered'):
            require_inflight(d, before, {**before, 'triggered_keys': 0}, handler='search_input',
                             pc=d.handlers['search_input'], key='B', replacement_entries=[])

    def test_empty_results_and_no_search_task_are_distinct(self):
        d, data, read = fixture()
        data[0x3004:0x3008] = bytes(4)
        data[0x104c:0x1050] = bytes(4)
        data[0x2004:0x2008] = b'\xff'*4
        state = asyncio.run(read_browser_state(d, read, frozen=True))
        self.assertEqual(state['paths'], [])
        self.assertEqual(state['count'], 0)
        self.assertEqual(state['selected_index'], -1)
        self.assertIsNone(state['task'])

    def test_invalid_count_and_unterminated_query_fail(self):
        for change in ('count', 'query'):
            d, data, read = fixture()
            if change == 'count':
                data[0x3004:0x3008] = struct.pack('<I', 4097)
            else:
                data[0x1020:0x1040] = b'x'*32
            with self.assertRaises(OracleError):
                asyncio.run(read_browser_state(d, read, frozen=True))

    def test_unresolved_debug_member_cannot_supply_a_guessed_offset(self):
        die = SimpleNamespace(tag='DW_TAG_class_type', offset=1,
            attributes={'DW_AT_name': SimpleNamespace(value=b'App')}, iter_children=lambda: iter(()))
        dwarf = _Dwarf.__new__(_Dwarf)
        dwarf.types = {'App': die}
        with self.assertRaisesRegex(OracleError, 'Unresolved member'):
            dwarf.field('App', '_missing')

    def test_ambiguous_debug_members_and_pointer_wrappers_fail(self):
        pointer = SimpleNamespace(tag='DW_TAG_pointer_type', offset=2,
                                  attributes={'DW_AT_byte_size': SimpleNamespace(value=4)})
        def member(offset):
            return SimpleNamespace(tag='DW_TAG_member', attributes={
                'DW_AT_name': SimpleNamespace(value=b'_pointer'),
                'DW_AT_data_member_location': SimpleNamespace(value=offset)},
                get_DIE_from_attribute=lambda _: pointer)
        die = SimpleNamespace(tag='DW_TAG_class_type', offset=1,
            attributes={'DW_AT_name': SimpleNamespace(value=b'Wrapper')},
            iter_children=lambda: iter((member(0), member(4))))
        dwarf = _Dwarf.__new__(_Dwarf)
        dwarf.types = {'Wrapper': die}
        with self.assertRaisesRegex(OracleError, 'Ambiguous member'):
            dwarf.field('Wrapper', '_pointer')
        with self.assertRaisesRegex(OracleError, 'Ambiguous pointer wrapper'):
            dwarf.pointer_storage(die)

    def test_identity_mismatch_fails_before_elf_parsing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/'rom').write_bytes(b'rom')
            (root/'elf').write_bytes(b'elf')
            with self.assertRaisesRegex(OracleError, 'identity'):
                verify_identity(root/'elf', root/'rom', {'elf_sha256': '0'*64, 'launcher_sha256': '0'*64})


if __name__ == '__main__':
    unittest.main()
