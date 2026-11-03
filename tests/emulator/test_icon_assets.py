"""Toolbar icons use palette indices as coverage into the theme's color ramp."""
from pathlib import Path
import struct
import unittest
import zlib


def chunks(path):
    data = path.read_bytes()
    offset = 8
    result = {}
    while offset < len(data):
        length = struct.unpack_from('>I', data, offset)[0]
        kind = data[offset + 4:offset + 8]
        value = data[offset + 8:offset + 8 + length]
        result[kind] = result.get(kind, b'') + value if kind == b'IDAT' else value
        offset += length + 12
    return result


def pixel_indices(image):
    width, height = struct.unpack_from('>II', image[b'IHDR'])
    stride = (width + 1) // 2
    data = zlib.decompress(image[b'IDAT'])
    previous = bytearray(stride)
    indices = []
    for y in range(height):
        start = y * (stride + 1)
        filtering = data[start]
        row = bytearray(data[start + 1:start + 1 + stride])
        for x in range(stride):
            left = row[x - 1] if x else 0
            up, corner = previous[x], previous[x - 1] if x else 0
            prediction = left + up - corner
            nearest = min((left, up, corner), key=lambda value: abs(prediction - value))
            row[x] = (row[x] + (0, left, up, (left + up) // 2, nearest)[filtering]) & 255
        indices.extend(index for value in row for index in (value >> 4, value & 15))
        previous = row
    return indices


class ToolbarIconTests(unittest.TestCase):
    def test_search_uses_the_same_full_coverage_palette_as_other_toolbar_icons(self):
        gfx = Path(__file__).resolve().parents[2] / 'arm9/gfx'
        search = chunks(gfx / 'searchIcon.png')
        self.assertEqual(struct.unpack('>IIBBBBB', search[b'IHDR']), (16, 16, 4, 3, 0, 0, 0))
        for name in ('backIcon', 'settingsIcon'):
            self.assertEqual(search[b'PLTE'], chunks(gfx / f'{name}.png')[b'PLTE'])
        self.assertEqual(len(search[b'PLTE']), 16 * 3)
        self.assertEqual(search[b'PLTE'][-3:], b'\xff\xff\xff')
        self.assertEqual(search[b'tRNS'][0], 0)
        self.assertEqual(max(pixel_indices(search)), 15)
