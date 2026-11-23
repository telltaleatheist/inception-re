# The original program

The reference is the DOS release, version 1.03 (1988, Infocom, programmed by
Westwood Associates, as its notice screen says). The program asks for a display
on startup: CGA or 64K EGA, Tandy 1000, 256K EGA/VGA, or MCGA. All four
drivers are in the one executable.

## Files

The hashes are of the copy this project was studied on. They are not how
the rebuild accepts a player's copy (docs/import.md, "How it reads the
program"): a copy that differs only in bytes the rebuild never imports is
accepted.

| File | Size | SHA-256 |
|---|---|---|
| `BTECH.EXE` (as shipped, EXEPACK-packed) | 152,429 | `15a1712682cf1fe50ca9e6a68a4b31d3bef7912401915edf7bb6242be193562a` |
| `BTECH.EXE` unpacked by `tools/unexepack.py` | 260,528 | `056b40670a95c4a4e56f7dda405dc0e2f3c98fcb530ed72f476d374051abd16d` |
| `$VERIFY.EXE` | 16,281 | `b410e7135dc48523452dfd14f2288a1374f9d26f6774337a554190811e1a856d` |

Our copy's `BTECH.EXE` is dated 1998, while every other file is from 1988.
**It is not a pristine 1.03: its copy-protection check has been altered** so
that the training center's quiz always passes (docs/combat.md, "The
copy-protection check"). The reference rig's results hold for everything
except that one branch. Nothing else in it is known to differ from the
release; treat any other surprise in it as open until checked.

The data files are 26 `.BLD` (buildings and their scripts), 15 `.MTP`
(maps), 22 `.ANM` (animations), 5 `.ICN` and 7 `.CMP` (graphics),
`WWOODBT.SIF` and `DEMOFILE`. Each format gets its own document as it is
worked out.

## Unpacking

The shipped executable is compressed with Microsoft EXEPACK, an option of
Microsoft's own linker, so a disassembler sees only the unpacking stub. To
get the program the addresses in this repository refer to, run:

    python3 tools/unexepack.py BTECH.EXE BTECH.unpacked.exe

and check the result against the SHA-256 above. The unpacked file has 3,292
relocations, and its entry point is Microsoft C's startup code. Addresses
are `segment:offset` as Ghidra shows them, with the program loaded at
segment 0x1000 (Ghidra's default for DOS).

## Shape

The program is Microsoft C, 1988 (medium or large model: far calls between
segments). It has 25 segments:

| Segments | Size | What |
|---|---|---|
| 1000–284b (`CODE_0`–`CODE_16`) | about 100 KB | the game |
| 287f (`CODE_17`) | 16 KB | Microsoft C run-time library (holds the entry point), plus the game's own assembly routines for the display drivers and picture unpacking |
| 2c6c, 37e8, 3856, 3858, 385b, 3892 | about 130 KB | far data and tables |
| 46db | 29 KB | DGROUP (the near data, stack and heap; it starts with the library's copyright text) |

Ghidra finds 354 functions: 144 in the run-time library and about 210 in the
game. A count of the C function prologue (`push bp; mov bp,sp`) agrees. The
functions are large, about 500 bytes each on average. Functions reached only
through pointers may still be missing, so expect the count to grow somewhat.
For scale, MechWarrior 3 has about 3,850 game functions.

The function map is `docs/functions.md`.

## Data segments and the prefixes the docs use

The code does not hold its data segments as constants. It loads them from
words inside DGROUP (for example `mov es,[0x5528]`), and those words hold
four different segments. A data address in these docs carries the prefix of
the segment the code really reaches it through:

| Prefix | Image segment | Ghidra segment | What lives there |
|---|---|---|---|
| `DG:` | 0x36db | 46db | DGROUP itself: texts, the rule tables (weapons, hit locations, clusters), settings such as the message level DG:2e38, the town's counters DG:57fe / DG:5800 |
| `F:` | 0x2892 | 3892 | the far data: the flags F:d30c + n, C-Bills F:d370, the character records F:c614, the 'Mech records F:c724, the figure arrays (positions F:4004 / F:4036, sprites F:d55e, facings F:396c, frames F:409a), the random generator's state F:d456 |
| `P:` | 0x1c6c | 2c6c | the party position P:a44b / P:a44d, the map cell buffer P:07ad, the view origin P:09ed..09f1, picture buffers |
| `M:` | 0x285b | 385b | the menu state M:00c2 / 00c6 / 00c8 |

The image segment is the value stored in the EXE; Ghidra shows each 0x1000
higher. `27e8:oooo` (Ghidra 37e8) is a constant segment named directly in
the code: the 'Mech templates and the walking scripts. Where an older note
says "DS" for a near address, it means `DG:`.

To tell which segment an address is in, look at the word the code loads into
ES before the access, not at the address: F's and DGROUP's offsets overlap.
