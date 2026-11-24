# The Amiga version's files

The Amiga version 2.3 (docs/versions.md) keeps the DOS game's data in a few files of its own
in its `Battletech` folder. This page is how each is laid out and where the program keeps
what the import reads. `src/formats/amiga_program.cpp` and `amiga_bld.cpp` read them.

Addresses are given as hunk:offset in the program (hunk 0 is the code, hunk 1 the data), in
hex, so they hold whatever address the system loads the program at.

## The program, `battletech`

An AmigaDOS load file of three hunks:

| Hunk | Kind | Size in memory | Stored |
|---|---|---|---|
| 0 | code | 165,104 bytes | all of it |
| 1 | data | 35,432 bytes | the first 9,740 bytes; the rest starts as zeros |
| 2 | BSS | 4 bytes | none |

The file is the usual layout: a header hunk (0x3F3) with the hunk table, then each hunk's
contents (0x3E9 code, 0x3EA data, 0x3EB BSS), its 32-bit relocations (0x3EC) and an end
marker (0x3F2). `read_amiga_hunks` reads the hunks as stored and steps over relocations,
symbols and debug hunks; the spans the import reads hold no pointers, so nothing needs
relocating.

**Builds.** Three builds are known (docs/versions.md): the plain one and those on the [a4]
and [h Madwand] disks. Each of the other two differs from the plain build in one byte of the
code hunk (at 0:5898 and 0:d5c9). The data hunk is the same in all three, so the reader
recognises version 2.3 by the SHA-256 of the data hunk as stored, and one span table serves
every build.

**Spans** (`amiga_spans()`):

| ID | Where | Length | What |
|---|---|---|---|
| `bld.offsets` | 1:07d6 | 104 | where each place's script starts in `builds.bld`: 26 big-endian longs, by DOS place number |
| `text.pair_first` | 1:148a | 16 | the letter-pair tables' first letters |
| `text.pair_second` | 1:149a | 128 | their second letters |

## The place scripts, `builds.bld`

The 26 DOS place scripts (formats/bld.md) one after another, in DOS place order. The program
(0:5fba) loads place n by seeking to the n-th `bld.offsets` entry, reading a 16-bit
big-endian length and that many bytes into its 9,000-byte script buffer, and deciphering the
buffer with the DOS cipher, `((b + 0x29) mod 256) XOR 0xE9`.

**The text is packed.** The script bytecode is DOS's (the same opcodes and operands, jump
words little-endian as in DOS), but each Print's text is shorter: the printer (0:a81e) reads a
text byte with its top bit set as two letters,

    first  = text.pair_first[(b & 0x7F) >> 3]
    second = text.pair_second[b & 0x7F]

so 128 common pairs take one byte each. The printer's control codes that take an argument
(2, 6, 9 and 0x13, docs/text.md) read that argument byte as it is. Packing saves about a third
of each script (55,861 bytes in all, against the DOS files' 85,379), and because the text is
shorter every jump after it lands at another offset.

**Laid out in DOS form.** `relay_amiga_script` decodes the script from start to end (every
DOS and Amiga script decodes cleanly that way, their unreached parts included), copies each
instruction with its text written out, and moves every jump word (the conditional jumps,
goto, and the menu and switch tables) to where its target went. The result is the DOS script,
so the place text IDs (`bld.<file>.<offset>`) are DOS's.

Compared with DOS 1.03, laid out this way:

- 24 of the 26 scripts are DOS's byte for byte, apart from a byte or two no instruction
  reaches (filler at the end of most scripts, two bytes in an unreached part of TRAINING),
  which keep the Amiga's values.
- **JAIL**: the sound instruction at Amiga offset 2401 plays effect 18 where DOS plays 17.
- **THEATER**: the Amiga charges the player once more (a `pay` instruction at Amiga offset
  240 that DOS doesn't have), plays effect 12 for one of DOS's effect-2 sounds (offset 542) and
  leaves out four of DOS's sound instructions later in the script. Its text is DOS's.

docs/versions.md, "Amiga rules", says how the rebuild plays these.
