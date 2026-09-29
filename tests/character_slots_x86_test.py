"""Exercise the patched native x86 blocks and compiled caves, without a client.

First run CharacterSlotsHarness.cpp to create the snapshots. Requires unicorn.
Usage: python character_slots_x86_test.py <snapshot-directory>
"""
import json
from pathlib import Path
import struct
import sys
import unittest

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *

DIRECTORY = Path(sys.argv.pop(1))
META = json.loads((DIRECTORY / "snapshot.json").read_text())
NATIVE = (DIRECTORY / "native.bin").read_bytes()
HOOKS = (DIRECTORY / "hooks.bin").read_bytes()
DELTA = META["nativeBase"] - 0x400000
FRAME, STACK, LOGIN, UI, RECORDS, RANKS, CONTEXT = (
    0x30002000, 0x30008000, 0x30010000, 0x30020000,
    0x30030000, 0x30040000, 0x30050000)
STRIDE = 0x2AC


class NativeSlotsTest(unittest.TestCase):
    def setUp(self):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        for base, data in ((META["nativeBase"], NATIVE), (META["hookBase"], HOOKS)):
            self.uc.mem_map(base, (len(data) + 4095) & ~4095)
            self.uc.mem_write(base, data)
        self.uc.mem_map(0x30000000, 0x60000)
        self.uc.mem_map(0xBE0000, 0x10000)  # absolute v83 singleton pointers
        self.uc.reg_write(UC_X86_REG_ESP, STACK)
        self.uc.reg_write(UC_X86_REG_EBP, FRAME)
        self.put(LOGIN + 0x194, RECORDS)
        self.put(LOGIN + 0x198, RANKS)
        self.put(UI + 0x6C, LOGIN)
        self.put(0xBE7918, CONTEXT)

    def put(self, address, value):
        self.uc.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))

    def get(self, address):
        return struct.unpack("<I", self.uc.mem_read(address, 4))[0]

    def run_to(self, start, ends, intercept=None):
        stops = {address + DELTA for address in ends}
        hit = []

        def hook(uc, address, size, _):
            if address in stops:
                hit.append(address - DELTA)
                uc.emu_stop()
            elif intercept:
                intercept(address - DELTA)

        handle = self.uc.hook_add(UC_HOOK_CODE, hook)
        try:
            self.uc.emu_start(start + DELTA, 0, count=10000)
        finally:
            self.uc.hook_del(handle)
        self.assertEqual(len(hit), 1, "native block did not reach its continuation")
        return hit[0]

    def bind_records(self, records=RECORDS):
        self.put(FRAME + 8, records)
        self.uc.reg_write(UC_X86_REG_ECX, records)
        self.uc.reg_write(UC_X86_REG_ESI, UI)
        self.uc.mem_write(UI + 0x70, b'\xA5' * 0x60)
        self.run_to(0x604647, [0x60465B])
        self.assertEqual(self.uc.mem_read(UI + 0x70, 0x60), b'\xA5' * 0x60,
                         "pointer initialization overwrote native buttons")
        for index in range(21):
            self.assertEqual(self.get(META["records"] + index * 4), records + index * STRIDE)

    def decode_records(self, count, records=RECORDS):
        self.put(LOGIN + 0x194, records)
        self.uc.reg_write(UC_X86_REG_ESI, LOGIN)
        self.uc.reg_write(UC_X86_REG_EDI, 0)
        self.uc.reg_write(UC_X86_REG_EBX, count)
        for offset in (-0x10, -0x14, -0x18):
            self.put(FRAME + offset, 0)
        visited = []

        def decode(address):
            if address == 0x5F9A80:  # opaque character payload decoder
                index = self.get(FRAME - 0x10)
                visited.append(index)
                self.put(records + index * STRIDE, 1000 + index)
                self.uc.reg_write(UC_X86_REG_EIP, 0x5F9B0F + DELTA)

        self.run_to(0x5F9A77, [0x5F9B29], decode)
        self.assertEqual(visited, list(range(count)))
        self.assertEqual(self.get(FRAME - 0x10), 20)

    def test_shop_purchase_and_expansion_receipt(self):
        for count in (14, 15, 16, 19, 20, 21):
            self.uc.reg_write(UC_X86_REG_ESI, UI)
            self.put(UI + 0x490, count)
            self.assertEqual(self.run_to(0x46C751, [0x46C75E, 0x46C7F2]),
                             0x46C7F2 if count < 20 else 0x46C75E)
            self.uc.reg_write(UC_X86_REG_ECX, count)
            self.assertEqual(self.run_to(0x46DF7C, [0x46DF81, 0x46DF92]),
                             0x46DF92 if count < 20 else 0x46DF81)
            self.uc.reg_write(UC_X86_REG_EAX, count)
            self.assertEqual(self.run_to(0x47AC37, [0x47AC3C, 0x47AC7E]),
                             0x47AC3C if count <= 20 else 0x47AC7E)
        self.run_to(0x8F1B3D, [0x8F1B3F])
        self.assertEqual(self.get(self.uc.reg_read(UC_X86_REG_ESP)), 20)

    def test_array_allocations_on_login_and_stage_reset(self):
        for start, end, reg in ((0x5F49AD, 0x5F49E0, UC_X86_REG_EBX),
                                (0x5F56E2, 0x5F5715, UC_X86_REG_ESI)):
            seen = []
            self.uc.reg_write(reg, LOGIN)

            def allocation(address):
                if address in (0x5FDD2D, 0x5FDDA6, 0x5FDC2A):
                    esp = self.uc.reg_read(UC_X86_REG_ESP)
                    seen.append((self.uc.reg_read(UC_X86_REG_ECX) - LOGIN, self.get(esp + 4)))
                    self.uc.reg_write(UC_X86_REG_EIP, self.get(esp))
                    self.uc.reg_write(UC_X86_REG_ESP, esp + 12)

            self.run_to(start, [end], allocation)
            self.assertEqual(seen, [(0x194, 21), (0x198, 21), (0x19C, 21)])

    def test_character_list_decode_visits_twenty_records(self):
        for count in (0, 3, 15, 16, 20):
            self.uc.mem_write(RECORDS, b'\xA5' * (STRIDE * 21))
            self.decode_records(count)
            self.assertEqual([self.get(RECORDS + i * STRIDE) for i in range(20)],
                             [1000 + i if i < count else 0 for i in range(20)])
            # The packet has only 20 logical slots. UI binding must empty the
            # extra drawing record; decoding alone cannot initialize it.
            self.assertEqual(self.get(RECORDS + 20 * STRIDE), 0xA5A5A5A5)
            self.bind_records()
            self.assertEqual(self.get(RECORDS + 20 * STRIDE), 0)

    def test_last_page_after_reused_heap_and_stage_reentry(self):
        def helpers(address):
            if address not in (0xA61040, 0x5FD9E2, 0x428712):
                return
            esp = self.uc.reg_read(UC_X86_REG_ESP)
            if address == 0xA61040:  # CRT memset used by the real constructor
                dst, value, count = (self.get(esp + n) for n in (4, 8, 12))
                self.uc.mem_write(dst, bytes([value & 255]) * count)
                self.uc.reg_write(UC_X86_REG_EAX, dst)
            # Empty-cell UI cleanup is opaque; the actual renderer's character
            # ID branch and purchased-slot/page checks execute unchanged.
            self.uc.reg_write(UC_X86_REG_EIP, self.get(esp))
            self.uc.reg_write(UC_X86_REG_ESP, esp + (8 if address == 0x428712 else 4))

        for iteration, (fill, count) in enumerate(((0, 15), (0xA5, 15), (0x3B, 20))):
            with self.subTest(heap_fill=fill, character_count=count):
                records = RECORDS + (iteration % 2) * 0x4000
                self.uc.mem_write(records, bytes([fill]) * (STRIDE * 21))
                for index in range(21):
                    self.uc.reg_write(UC_X86_REG_ECX, records + index * STRIDE)
                    self.run_to(0x601103, [0x60112B], helpers)
                # Native construction initializes owned subobjects, NOT IDs.
                self.assertEqual(self.get(records + 20 * STRIDE), fill * 0x01010101)
                self.decode_records(count, records)
                before = bytes(self.uc.mem_read(records, STRIDE * 21))
                self.bind_records(records)
                after = bytes(self.uc.mem_read(records, STRIDE * 21))
                self.assertEqual(after[:20 * STRIDE], before[:20 * STRIDE])
                self.assertEqual(after[20 * STRIDE + 4:], before[20 * STRIDE + 4:],
                                 "padding clear damaged constructed subobjects")
                self.put(UI + 0x12C, 20)
                self.put(UI + 0x130, 6)
                for cell in range(3):
                    self.put(FRAME + 8, cell)
                    self.uc.reg_write(UC_X86_REG_EAX, records + (18 + cell) * STRIDE)
                    self.uc.reg_write(UC_X86_REG_ECX, UI)
                    branch = self.run_to(0x606BBC, [0x606E99, 0x606C08, 0x606E93], helpers)
                    expected = 0x606E93 if cell == 2 else (0x606E99 if count == 20 else 0x606C08)
                    self.assertEqual(branch, expected,
                                     "renderer treated padding as a character instead of hiding it")

    def test_create_full_list_and_insert_search(self):
        self.uc.reg_write(UC_X86_REG_ESI, LOGIN)
        self.uc.reg_write(UC_X86_REG_EBX, 0)
        self.put(RECORDS + 14 * STRIDE, 1014)
        self.assertEqual(self.run_to(0x5F7E9A, [0x5F7EA8, 0x5F7EAD]), 0x5F7EAD)
        self.put(RECORDS + 19 * STRIDE, 1019)
        self.assertEqual(self.run_to(0x5F7E9A, [0x5F7EA8, 0x5F7EAD]), 0x5F7EA8)
        for occupied in (15, 19, 20):
            for index in range(21):
                self.put(RECORDS + index * STRIDE, index + 1 if index < occupied else 0)
            self.put(FRAME - 0x10, -1)
            self.uc.reg_write(UC_X86_REG_EDI, 0)
            self.run_to(0x5FA2E2, [0x5FA2FE])
            self.assertEqual(self.get(FRAME - 0x10), occupied if occupied < 20 else 0xFFFFFFFF)

    def test_selection_and_all_table_readers(self):
        self.bind_records()
        for index in range(20):
            self.put(RECORDS + index * STRIDE, index + 1)
        for index in (0, 14, 15, 19):
            expected = RECORDS + index * STRIDE
            for start, end in ((0x605C06, 0x605C0B), (0x605E82, 0x605E87)):
                self.uc.reg_write(UC_X86_REG_EAX, index)
                self.run_to(start, [end])
                self.assertEqual(self.uc.reg_read(UC_X86_REG_EAX), expected)
            self.uc.reg_write(UC_X86_REG_EAX, index)
            self.run_to(0x604FE3, [0x604FEA])
            self.assertEqual(self.uc.reg_read(UC_X86_REG_ECX), expected)
            self.put(FRAME + 8, index)
            self.uc.reg_write(UC_X86_REG_ECX, UI)
            self.assertEqual(self.run_to(0x6059A1, [0x6059B8, 0x6059C4]), 0x6059B8)
        self.put(FRAME + 8, 20)
        self.uc.reg_write(UC_X86_REG_ECX, UI)
        self.assertEqual(self.run_to(0x6059A1, [0x6059B8, 0x6059C4]), 0x6059C4)
        # Drawing the last page reads 18, 19 and a safe, empty padding record.
        for index in (18, 19, 20):
            self.uc.reg_write(UC_X86_REG_EAX, index)
            self.run_to(0x605986, [0x60598C])
            self.assertEqual(self.get(self.uc.reg_read(UC_X86_REG_ESP)), RECORDS + index * STRIDE)
        # Previous-page navigation scans backwards from the empty third cell.
        self.uc.reg_write(UC_X86_REG_EAX, 18)
        self.uc.reg_write(UC_X86_REG_ECX, 2)
        self.run_to(0x6051DF, [0x6051F2])
        self.assertEqual(self.uc.reg_read(UC_X86_REG_EDI), RECORDS + 19 * STRIDE)
        self.assertEqual(self.uc.reg_read(UC_X86_REG_ECX), 1)

    def test_delete_compacts_all_twenty_records_and_ranks(self):
        for deleted in (0, 14, 15, 19):
            for index in range(21):
                self.put(RECORDS + index * STRIDE, index + 1 if index < 20 else 0)
                self.uc.mem_write(RANKS + index * 16, bytes([index + 1]) * 16 if index < 20 else bytes(16))
            self.put(FRAME - 0x10, deleted + 1)
            self.put(CONTEXT + 0x2098, 20)
            self.uc.reg_write(UC_X86_REG_ESI, LOGIN)
            self.uc.reg_write(UC_X86_REG_EDX, 0)

            def helpers(address):
                # Only data-copy helpers are stubbed; native search, offsets,
                # loop bounds, memmove sizes, and final clears execute as x86.
                sizes = {0x5F9FCA: 4, 0x451541: 4, 0xA61550: 0, 0xA61040: 0}
                if address not in sizes:
                    return
                esp = self.uc.reg_read(UC_X86_REG_ESP)
                if address in (0x5F9FCA, 0x451541):
                    dst, src = self.uc.reg_read(UC_X86_REG_ECX), self.get(esp + 4)
                    count = 0xE7 if address == 0x5F9FCA else STRIDE - 0xE7
                    self.uc.mem_write(dst, bytes(self.uc.mem_read(src, count)))
                else:
                    dst, src, count = self.get(esp + 4), self.get(esp + 8), self.get(esp + 12)
                    self.uc.mem_write(dst, bytes(self.uc.mem_read(src, count)) if address == 0xA61550
                                      else bytes([src & 255]) * count)
                self.uc.reg_write(UC_X86_REG_EIP, self.get(esp))
                self.uc.reg_write(UC_X86_REG_ESP, esp + 4 + sizes[address])

            self.run_to(0x5F9E01, [0x5F9ED8], helpers)
            expected = [i + 1 for i in range(20) if i != deleted] + [0, 0]
            self.assertEqual([self.get(RECORDS + i * STRIDE) for i in range(21)], expected)
            self.assertEqual([self.uc.mem_read(RANKS + i * 16, 1)[0] for i in range(21)], expected)
            self.assertEqual(self.get(CONTEXT + 0x2098), 19)


unittest.main(verbosity=2)
