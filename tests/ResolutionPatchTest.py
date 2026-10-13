"""Check actual v83 instruction operands before simulating supported heights.

No executable is modified or launched. Only standard Python modules are used.
"""
from pathlib import Path
import re
import struct
import sys

exe = Path(sys.argv[1]).read_bytes()
source = (Path(__file__).parents[1] / 'ezorsia/Client.cpp').read_bytes().decode('latin1')  # ASCII statements in legacy-encoded source
pe = struct.unpack_from('<I', exe, 0x3C)[0]
count, = struct.unpack_from('<H', exe, pe + 6)
optional_size, = struct.unpack_from('<H', exe, pe + 20)
image_base, = struct.unpack_from('<I', exe, pe + 24 + 28)
sections = pe + 24 + optional_size

def read(va, size):
    rva = va - image_base
    for i in range(count):
        virtual_size, address, raw_size, raw = struct.unpack_from('<IIII', exe, sections + i * 40 + 8)
        if address <= rva and rva + size <= address + raw_size:
            return exe[raw + rva - address:raw + rva - address + size]
    raise AssertionError(f'Address not backed by file: {va:08X}')

comparison = read(0x4D59B2, 8)
division = read(0x640618, 10)
handler = read(0xA5FC2B, 5)
assert comparison == bytes.fromhex('81 F9 58 02 00 00 7E 0E')
assert division == bytes.fromhex('B9 58 02 00 00 F7 F9 8B 45 E0')
assert handler == bytes.fromhex('B8 D0 F5 AE 00')
active = '\n'.join(line.split('//', 1)[0] for line in source.splitlines())
offset = int(re.search(r'WriteInt\(0x004D59B2 \+ (\d+), m_nGameHeight\)', active)[1])
assert offset == 2, 'Height must preserve cmp ModR/M, not only its opcode'
assert '0x0064061D' not in active, 'Do not patch the division instruction'
assert '0x00A5FC2B' not in active, 'Do not alter the SEH handler load opcode'
assert 'WriteInt(0x00640618 + 1, m_nGameHeight)' in active
for height in [600, 720, 768, 800, 900, 1024, 1050, 1080, 1440, 2160]:
    cmp_code, div_code = bytearray(comparison), bytearray(division)
    struct.pack_into('<I', cmp_code, offset, height)
    struct.pack_into('<I', div_code, 1, height)
    assert cmp_code[:2] == b'\x81\xF9' and cmp_code[6:] == comparison[6:]
    assert div_code[0] == 0xB9 and div_code[5:] == division[5:]
print('PASS resolution patches: 10 heights retain cmp, idiv, following instructions and SEH load')
