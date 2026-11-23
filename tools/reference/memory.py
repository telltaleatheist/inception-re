#!/usr/bin/env python3
"""Read the original's memory from a capture (tools/capture/snap.asm).

    memory.py CAPTURE.bin SPEC...

A SPEC is SEG:OFF[:LEN] in hex, where SEG may be `ds` (the game's data
segment when it last asked for a key) or `[OFF]` (the segment whose number
is the word at DS:OFF, the way the game reaches its far data, e.g. [538a]).
Prints the bytes in hex. As a module: Memory(path).read(seg, off, n).
"""
import struct
import sys

PICTURE_START = 24 + 768
EGA_PICTURE = 32000


class Memory:
    def __init__(self, path):
        data = open(path, "rb").read()
        at = PICTURE_START + EGA_PICTURE
        if data[at:at + 4] != b"MEMS":
            raise ValueError(f"{path}: no memory in this capture")
        self.ds = struct.unpack_from("<H", data, at + 4)[0]
        self.mem = data[at + 6:]

    def segment(self, spec):
        if spec == "ds":
            return self.ds
        if spec.startswith("["):
            return self.word(self.ds, int(spec.strip("[]"), 16))
        return int(spec, 16)

    def read(self, seg, off, n=1):
        base = seg * 16 + off
        return self.mem[base:base + n]

    def byte(self, seg, off):
        return self.mem[seg * 16 + off]

    def word(self, seg, off):
        return struct.unpack_from("<H", self.mem, seg * 16 + off)[0]

    def disk_prompt(self):
        """Whether the game was in its disk prompt (BTECH.EXE 1000:2913), the
        loader's answer to a file it could not read: the far return address
        of the prompt's key wait (1000:29de) is on the game's stack, in
        DGROUP, which is loaded 0x36db paragraphs above segment 1000."""
        mark = struct.pack("<HH", 0x29DE, (self.ds - 0x36DB) & 0xFFFF)
        return mark in self.read(self.ds, 0, 0x10000)

    def in_demo_read(self):
        """Whether the capture is inside the demo's key read (BTECH.EXE
        273d:0259): the far return address of its question to DOS
        (287f:3bdc), 273d:02e6, is on the game's stack. The demo's other
        captures are at the main loop's poll (273d:002f, return 273d:003b)."""
        mark = struct.pack("<HH", 0x02E6, (self.ds - 0x36DB + 0x173D) & 0xFFFF)
        return mark in self.read(self.ds, 0, 0x10000)


def main():
    m = Memory(sys.argv[1])
    for spec in sys.argv[2:]:
        parts = spec.split(":")
        seg = m.segment(parts[0])
        off = int(parts[1], 16)
        n = int(parts[2], 16) if len(parts) > 2 else 1
        print(f"{seg:04x}:{off:04x}", m.read(seg, off, n).hex(" "))


if __name__ == "__main__":
    main()
