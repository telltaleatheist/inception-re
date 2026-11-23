#!/usr/bin/env python3
"""The game state the rig compares, read from the original's memory.

    state.py CAPTURE.bin

Prints the same lines as `chr --state` (random numbers, C-Bills, the
townspeople, the party's place, the script flags, Jason's skills, the
character and 'Mech records and the menus' remembered items), from a
capture with memory (tools/capture/snap.asm). The addresses are
docs/townspeople.md's and docs/formats/characters.md's.
"""
import struct
import sys

from memory import Memory


def original_state(path, fight=False):
    m = Memory(path)
    far = m.word(m.ds, 0x538a)        # the segment of DGROUP:d3xx
    xs = m.word(m.ds, 0x53a4)         # figure x positions (4004)
    ys = m.word(m.ds, 0x53a6)         # figure y positions (4036)
    heads = m.word(m.ds, 0x5578)      # figure headings (3920)
    r = m.read(m.ds, 0x4fc0, 3)
    lines = ["state random %02x %02x %02x" % tuple(r),
             "state cbills %d" % struct.unpack("<i", m.read(far, 0xd370, 4))[0],
             # the townspeople's tick counters: the town's, a mission walk's
             "state counters %d %d" % (m.word(m.ds, 0x57fe), m.word(m.ds, 0x5802)),
             # 1e31:0006's wander pattern: its step count and its flip (docs/townspeople.md)
             "state wander %d %d" % (m.word(m.ds, 0x315a), m.byte(m.ds, 0x315c))]
    # While the demo plays (F:3938), the bytes of DEMOFILE whose keys have
    # been acted on (docs/startup.md "The demo"): those read (F:39f8 less
    # 10000), less the key and its pauses when the capture is inside the
    # key read, which has read them and not yet returned the key.
    if m.word(m.word(m.ds, 0x5384), 0x3938):
        n = m.word(m.word(m.ds, 0x56f4), 0x39f8) - 10000
        if m.in_demo_read():
            buffer = m.word(m.ds, 0x56f6)  # the bytes from F:00a0 + 10000
            n -= 1
            while n > 0 and m.byte(buffer, 0xa0 + 10000 + n - 1) == 0x48:
                n -= 1
        lines.insert(2, "state demo %d" % n)
    for i in range(8):
        rec = 0xd390 + 0x1a * i
        heading = struct.unpack("b", m.read(heads, 0x3920 + 0x10 + i, 1))[0]
        lines.append("state walker %d at %04x %04x target %04x %04x route %02x wait %02x heading %d" % (
            i, m.word(xs, 0x4004 + 2 * (0x10 + i)), m.word(ys, 0x4036 + 2 * (0x10 + i)),
            m.word(far, rec + 4), m.word(far, rec + 6), m.byte(far, rec + 8), m.byte(far, rec + 9), heading))
    lines.append("state party %04x %04x" % (m.word(m.word(m.ds, 0x538c), 0xa44b), m.word(m.word(m.ds, 0x538e), 0xa44d)))
    lines.append("state flags " + m.read(far, 0xd30c, 0x64).hex())          # docs/scripts.md
    lines.append("state skills " + m.read(far, 0xc614 + 4, 7).hex())       # Jason's record, bytes 4-10
    for c in range(16):                                                     # the character records
        lines.append("state character %d %s" % (c, m.read(far, 0xc614 + 0x11 * c, 0x11).hex()))
    for k in range(8):                                                      # the 'Mech records
        lines.append("state mech %d %s" % (k, m.read(far, 0xc724 + 0x7d * k, 0x7d).hex()))
    menus = m.word(m.ds, 0x56de)                                            # the menu table, 385b:0092
    lines.append("state menus " + " ".join("%d" % m.word(menus, 0x98 + 16 * i) for i in range(41)))
    lines.append("state seen " + m.read(far, 0xcb0c, 0x800).hex())          # docs/wilderness.md, "The seen map"
    p = m.word(m.ds, 0x562a)                                                # 2c6c, the display library's data
    lines.append("state noise " + m.read(p, 0x09fb, 256).hex())           # the generator's noise table
    for k in range(9):                                                      # the nine area grids (docs/wilderness.md)
        lines.append("state grid %d %s" % (k, m.read(p, 0x0564 + 64 * k, 64).hex()))
    if fight:
        lines += fight_state(m, far)
    return lines


def fight_state(m, far):
    """The fight's tables (docs/combat.md), in the form `chr --fight-state`
    prints them. They all live in the far segment F."""
    lines = []
    for i in range(24):
        lines.append("state figure %d at %04x %04x active %d destroyed %d heading %02x facing %02x frame %02x" % (
            i, m.word(far, 0x4004 + 2 * i), m.word(far, 0x4036 + 2 * i), m.word(far, 0x406a + 2 * i),
            m.word(far, 0x393c + 2 * i), m.read(far, 0x3920 + i, 1)[0], m.read(far, 0x396c + i, 1)[0],
            m.read(far, 0x409a + i, 1)[0]))
        lines.append("state orders %d %s" % (i, m.read(far, 0x32c6 + 0x30 * i, 0x30).hex()))
        lines.append("state targets %d %s" % (i, m.read(far, 0x3800 + 12 * i, 12).hex()))
        lines.append("state path %d %s" % (i, m.read(far, 0x40b4 + 0x18 * i, 0x18).hex()))
        mech = i < 4 or 12 <= i < 16
        lines.append("state drawn %d on %d terrain %02x tile %02x above %02x half %02x at %d %d parked %02x script %04x" % (
            i, m.read(far, 0x42f6 + i, 1)[0], m.read(far, 0x32ae + i, 1)[0], m.read(far, 0x3750 + i, 1)[0],
            # F:45ce and F:4554 hold the 'Mechs' only; past them is other data.
            m.read(far, 0x45ce + i, 1)[0] if mech else 0, m.read(far, 0x4554 + i, 1)[0] if mech else 0,
            m.word(far, 0x324c + 2 * i),
            m.word(far, 0x327c + 2 * i), m.read(far, 0x45b6 + i, 1)[0], m.word(m.ds, 0x01f6 + 4 * i)))
    lines.append("state heat " + " ".join("%02x" % b for b in m.read(far, 0x006e, 8)))
    lines.append("state burning " + " ".join("%02x" % b for b in m.read(far, 0xd576, 8)))
    lines.append("state fight_on %d" % (m.word(m.ds, 0x014a) != 0))
    marks = ["%02x%02x%02x%02x" % (m.read(far, 0xd457 + k, 1)[0], m.read(far, 0xd497 + k, 1)[0],
                                   m.read(far, 0xd4d7 + k, 1)[0], m.read(far, 0xd517 + k, 1)[0]) for k in range(64)]
    lines.append("state marks next %02x %s" % (m.read(far, 0xd557, 1)[0], " ".join(marks)))
    return lines


if __name__ == "__main__":
    print("\n".join(original_state(sys.argv[1], fight="--fight" in sys.argv[2:])))
