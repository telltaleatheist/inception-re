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

## Pictures and tile sheets, the .cmp and .icn files

src/formats/amiga_picture.h. Every one starts with the picture's own palette:

    +0   16 words, big-endian: colours 0-15, each 0x0RGB (4 bits a component)
    +32  word, big-endian: the length of what follows
    +34  that many bytes

The loader (Amiga battletech 0:1b3f8) reads the palette, the length and the bytes, nothing
more. What the bytes are depends on the file:

- **Full-screen pictures and the compressed tile sheets** (every .cmp; `bttltech.icn`,
  `destruct.icn`, `starleag.icn`, `animate.icn`): the DOS container after its length
  ([cmp.md](cmp.md)), a type byte and the run-length stream, with the 0 code's 16-bit count
  big-endian. The stream unpacks to 32,000 bytes: four bit planes of 8,000, plane 0 first.
  Within a plane, type 1 is row order (byte y × 40 + x/8) and type 2 column order (byte
  x/8 × 200 + y), the two orders of DOS. In a tile sheet, tile n's row y is the 2 bytes at
  n × 32 + y × 2 of each plane, so a plane holds 250 tiles of 16 × 16.
- **`map.icn`**: no type byte and no stream. 170 tiles of 128 bytes, stored as they are: tile
  n, row y, plane p is the 2 bytes at n × 128 + y × 8 + p × 2. Tile n is DOS `MAP.ICN`'s tile
  n + 80; DOS's tiles 0-79 are blank and the reader leaves them blank.

In each byte bit 7 is the leftmost pixel, and plane p gives bit p of the colour number. The
reader writes the DOS working forms (cmp.md, [icn.md](icn.md)): 32,000 bytes, two pixels a
byte, the left pixel in the high nibble; the colour numbers index the file's own palette.

Checked against DOS 1.03 offline, with each Amiga colour matched to its most frequent DOS
colour: `map.icn` is DOS's tiles 80-249 pixel for pixel; the other files agree as
docs/versions.md lists (the town sheets and the title screen are redrawn). All 84 pictures in
the six Amiga copies read.

## The maps, `maps`

src/formats/amiga_map.h. DOS's MAP1-MAP15.MTP ([mtp.md](mtp.md)) one after another, 33,046
bytes. The loader (Amiga battletech 0:21d12) seeks to the map's entry in a table of 15
big-endian longs at 1:1b8e (span `maps.offsets`) and reads the fields one after another, as
DOS does. Map n runs from its offset to the next map's; map 15, to the file's end. The only
differences from DOS:

- In maps 1-14 the four tables of 16 position words at 0x185-0x204 (the entrances and the
  second table) are big-endian, the 68000's order. The reader swaps them back.
- Map 2's first townsperson has a name of its own. In DOS, that slot repeats the name of map 1's
  first townsperson, which the Amiga's map 1 still has. With `amiga_rules` off the reader
  copies that slot over, so the map is DOS's (docs/versions.md, "Amiga rules").

Checked offline in four of the copies: with the name copied, all 15 maps equal DOS 1.03's byte for
byte; without it, 14 of 15 do.

