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

    +0   16 words, big-endian: colours 0-15, each 0x0RGB ("Colours" below)
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
byte, the left pixel in the high nibble; the colour numbers index the file's own palette, until
`AmigaGameSource` turns them into DOS's numbers ("Colours").

Checked against DOS 1.03 offline, with each Amiga colour matched to its most frequent DOS
colour: `map.icn` is DOS's tiles 80-249 pixel for pixel; the other files agree as
docs/versions.md lists (the town sheets and the title screen are redrawn). The 12 files read in
each of the seven copies ("The copy as a source" below says which reads are tests).

## The maps, `maps`

src/formats/amiga_map.h. DOS's MAP1-MAP15.MTP ([mtp.md](mtp.md)) one after another, 33,046
bytes. The loader (Amiga battletech 0:21d12) seeks to the map's entry in a table of 15
big-endian longs at 1:1b8e (span `maps.offsets`) and reads the fields one after another, as
DOS does. Map n runs from its offset to the next map's; map 15, to the file's end. The only
differences from DOS:

- In maps 1-14 the four tables of 16 position words at 0x185-0x204 (the entrances and the
  second table) are big-endian, the 68000's order. The reader swaps them back.
- Map 2's first townsperson has a name of its own. In DOS, that slot repeats the name of map 1's
  first townsperson, which the Amiga's map 1 still has. The reader always copies that slot
  over, so the map is DOS's (docs/versions.md, "Amiga rules").

Checked offline in all seven copies: with the name copied, all 15 maps equal DOS 1.03's byte for
byte; without it, 14 of 15 do.

## The place pictures, `animation.anm`

src/formats/amiga_anm.h. DOS's O0-O21.ANM ([anm.md](anm.md)) one after another. The loader
(Amiga battletech 0:23f7a) seeks to the place's entry in a table of 22 big-endian longs at
1:1c40 (span `anm.offsets`) and reads at most 22,000 bytes. Picture n runs from its offset to
the next one's; picture 21, to the file's end. Each is in DOS's own form: frame list, delays,
speed, then frames of XOR runs (0:1ad54 unpacks them with a size of 3,872 bytes, two pixels a
byte, the 0 code's count big-endian as in DOS), and the delay of frame k is found and scaled as
in DOS (0:12084). So formats/anm.h reads them as they are, and the source turns the colour
numbers of the frames into DOS's ("Colours").

Compared with DOS 1.03 offline (each Amiga colour matched to its most frequent DOS colour):
every picture has its DOS frame count except O4, O5, O6, O9 and O15. The last frames agree with
DOS's from 24% to 100%: O17, O19 and O21, one frame each, 100% with the colours one-to-one; O15's
last frame 79% (it has a seventh frame; the sixth, DOS's last, 100%, but with several Amiga
colours to one DOS colour). So some are retouched. Timing differs
in seven: O4, O5, O6, O9, O10, O13 and O15 have their own frame lists, delays or speed (O17-O19
differ only in entry 0 of the list, which never takes effect).

A table of 22 longs at 1:1c98 follows the offsets; the loader reads its first word as a frame
number at which to start a sound during the animation (with more cases in the code for places 1,
3, 4 and 8). DOS plays no sound there. Not read yet.

## The windows and the menus

Spans `windows` (1:1530, 11 records of 16 bytes) and `menus` (1:15c2, 41 records of 16 bytes):
DOS's tables at 385b:0000 and 385b:0092 ([../scripts.md](../scripts.md), "Menus") in the same
layout, the words big-endian. As in DOS, the last two window records overlap the first menu
records, so menus 0 and 1's highlight colours (word 5) are also windows 9 and 10's starting
columns. Windows 0-8 are DOS's byte for byte. All that differs is the menus' highlight colour,
the Amiga's own colour number in all 40 non-empty records (3, which is DOS's 14: "Colours"), and
with it those two starting columns (3 where DOS has 14: a place on screen, so DOS's must stay).
The two uses come from the same bytes but are handed out apart: the menus from the `menus`
bytes as stored (`amiga_menus`) or with the colours renumbered (`tables.menus` below), and `tables.windows` from a copy of
the windows' bytes with word 6 of records 9 and 10 set to DOS's 14.
`amiga_menus` reads the menus as DOS's records; the scripts' menu instructions need their item
counts to be laid out.

## The demo, `demofile`

The same 1,023 bytes as DOS's DEMOFILE (docs/startup.md, "The demo") except the movement keys:
the Amiga records them as the number pad's digits 8, 2, 4, 6 and 3 where DOS has its own key
codes. `amiga_demo_keys` puts DOS's codes back; no other byte holds those values, and the
result is DOS's file byte for byte in all seven copies.

## The copy as a source

src/formats/amiga_source.h, `AmigaGameSource`: the copy's files by name, the program read and
checked once, and each piece through the readers above, always with the DOS edits.

Compared offline with `DosGameSource` in all seven copies (a byte changed in the copy's file made
each of these comparisons fail):

- Scripts: 11 are DOS's byte for byte and 15 differ only in 16 bytes that no instruction walked
  from offset 0 covers (two in TRAINING, one in each of the others).
  Besides, five text colours come out as 7 where DOS has 14, in scripts 1 and 14 ("Colours").
- Maps (15) and the demo: DOS's byte for byte.

The art can't be compared byte for byte: the colour numbers are the Amiga's, and much of it is
redrawn. Five pieces are DOS's drawing exactly once the colours are matched one-to-one (every
Amiga colour one DOS colour and the other way round), in all seven copies:

| Piece | Decoder it exercises | One-byte changes | Seen | Refused | Unnoticed |
|---|---|---|---|---|---|
| `infocom.cmp` | the run-length stream, column order | 150 | 120 | 30 | 0 |
| `map.icn`, tiles 80-249 | the uncompressed tiles | 150 | 150 | 0 | 0 |
| O17 | the XOR runs, one frame | 150 | 135 | 15 | 0 |
| O19 | the same | 150 | 147 | 2 | 1 |
| O21 | the same | 150 | 136 | 14 | 0 |

(Each change is one byte of the piece's stored data, at a random place, changed at random: seen if
the piece is then no longer exact, refused if the reader throws.) These are tests of the readers.
(O19's one is the length of its last run made longer: a frame ends at 3872 bytes even inside a run,
in DOS's 287f:23ec too, so the pixels are the same.)
(The changes were made in the plain copy; the art files are the same in all seven.) Nothing of the
same strength covers the row-order pictures (type 1: the screen frame and the tiny land sheet), the
compressed tile sheets or the place pictures of more than one frame: they have no exact piece, so
a wrong pixel in them would not show. The other shares in
docs/versions.md are similarity figures (each Amiga colour matched to its most frequent DOS
colour, several Amiga colours often to one DOS colour): they say the art is DOS's drawing
recoloured, not that it was read right.

There is no speaker tune (`title_tune()` is empty), and no title music was found ("The sound
file").

## Colours

src/formats/amiga_colours.h. The Amiga's colour numbers aren't DOS's. Where the same thing has a
number in both, they are tied together the same way everywhere:

- the overhead map's cell colours, 500 of the 750 bytes of DOS's table (its first two thirds;
  below): each Amiga number to one DOS number and the other way round, in both tables, and the
  same way in the two;
- the menus' highlight and the picked yes/no answer: 3 where DOS has 14;
- the colour the stats screen pulses: 2 where DOS has 4.

Colour 1 is in none of these; it is the one left over on both sides, so it goes with 1. The
result, DOS's number for Amiga 0 to 15: 0, 1, 4, 14, 9, 5, 6, 12, 3, 2, 10, 8, 13, 11, 7, 15.
Colours named by the game's own code (the dot, the bars, the text windows) are DOS's numbers, so
the readers turn every Amiga number into DOS's, and each palette into the same order: the pixels
come out as the Amiga shows them whatever the numbering, and the game's named colours get the
Amiga colour that does the same job. This doesn't make the art DOS's art: the Amiga's own
numbers in its pictures don't map one to one onto DOS's ("What was compared").

Texts: the arguments of the colour codes 0x02 and 0x06 are renumbered like the pictures (docs/text.md).
Scripts' texts too. The 424 texts of the scripts hold only colours 15 and, five times, 14, which the
Amiga's scripts share with DOS's: on the Amiga 14 is another colour than DOS's 14, so it becomes 7.

**What a colour looks like.** The program sets the screen's colours (0:1c4a0) from 16 words by
keeping the low three bits of each component and doubling them: a component is 0, 2 ... 14 on the
Amiga's 0-15 scale. No word in the files has a component over 7. So its white is 0xee.

**Which palette is on screen.** A picture file's 16 words are only read into a buffer (1:70a0) by
the loader (0:1b3f8); the program sets the screen's colours (each place: docs/screen_colours.md) from:

- 1:1c1e, the game's palette, after the title and after a load outside the cache (0:2271e, 0:218e2): the towns, the
  wilderness, the fights, the places and the text. `tables.palette`. It differs from the palettes
  of BTTLTECH.ICN and BTSTATS.CMP in one colour (Amiga 9).
- 1:07a0, the stats screen's, the same 16 words as the game's, set every 16th pass (0:02c4a) and on leaving, with
  its colour 2 alternating between the two words at 1:07c0. `tables.stats_palette` and
  `tables.stats_pulse`: DOS's pulse has four steps (docs/town_loop.md), so the two alternate over
  them.
- The loaded file's, in a fade, for INFOCOM.CMP and BTTITLE.CMP (0:23798, 0:245b4).
- STARLEAG.ICN's, on going into the cache and after ENDMECH.CMP (0:ae12, 0:b000, special 45 at
  0:17e60). So both levels of the cache, MAP.ICN too, are shown with it. The picture ENDMECH.CMP is
  drawn while the cache's colours are on, which are the same 16 words as its own:
  `tables.endmech_palette`, taken from ENDMECH.CMP.
- All black (1:1a10) at start-up and before the cache's colours.

The other files' palettes are never set. `AmigaGameSource::picture_palette` gives the two start-up
pictures' and the cache's tiles' own; everything else has the game's.

Open:

- In the Amiga cache the whole screen is under STARLEAG.ICN's palette, not only the tiles: the
  frame, the text and the figures. Only the tiles have it here; the rest keep the game's colours.
  Proposed: docs/screen_colours.md.
- XORs work on colour numbers: the menus' highlight bar (docs/text.md) and the boxes over the map
  (docs/combat.md 2.6). On DOS's numbers they needn't give what they would on the Amiga's. The
  Amiga's code for both is not read, so what it shows there is unknown.
- The overhead map's third table is DOS's for the cache's level 1, and the Amiga has no such table:
  0:bac0 swaps the level 0 table in on both levels. So `tables.overhead_colours` has that table
  twice, and level 1 looks as on the Amiga. A tile number past 249 would read on into the next table,
  which isn't the Amiga's on level 1, but no cell of maps 14 and 15 has one, in DOS's files or the
  Amiga's, nor has the block store's tail (the puzzle's changes stay in 97-f0).

## The texts

`AmigaProgramData` (src/formats/amiga_program_data.h) hands out the DOS import table's texts,
all 538, by the same IDs. src/formats/amiga_texts.cpp says where each is in the program
(hunk:offset, nearly all in the code hunk) and holds no text; each is read as DOS's printer reads
one (docs/text.md). The program is checked text by text: each must hash to the DOS table's hash,
or, for the six below, to its own.

- 525 are DOS's letter for letter.
- 6 differ only in colour numbers, the arguments of codes 0x02 and 0x06: the two crew labels, the
  two hit marks of a shot, and the yes/no question's picked answers (the Amiga's fg 12 on bg 3 where
  DOS has 0 on 14). Renumbered ("Colours") they are still not DOS's: the labels are 10 (DOS 2), the
  marks 4 (DOS 13) and the answers' letters 13 (DOS 0), on 14 as in DOS. Colour follows the copy.
- 6 differ in the case of some letters: the Amiga capitalises words that DOS doesn't. The
  table lists the positions, and the reader flips them back, so the player reads DOS's text.
- 1 (`orders.points_left`) is three strings the Amiga prints one after another; joined, they are
  DOS's text.

Checked in all seven copies: 532 texts equal DOS's and 6 differ only in colour numbers.

## The tables

`AmigaProgramData` also hands out 114 of the DOS import table's 120 tables by the same IDs, in
DOS's form (src/formats/amiga_tables.cpp says where each is), and four palettes as colours
("Colours"). 110 hash to the DOS table's own hash once in that form; 4 have a hash of their own.

The forms:

- Words and longs are big-endian.
- The weapons are records of 18 bytes, DOS's 17 and one more.
- The 'Mech templates are DOS's at 1:02d6, 0x7d bytes each. Template 6's name differs in the case
  of one letter; the reader flips it back.
- DOS's far pointers are longs holding an offset in the data hunk: the encounter templates
  (1:08a6), the frame styles (1:1898) and the two pose tables (1:0b5e, 1:114a).
- The 48 pose scripts they point at (from 1:106c) are DOS's in DOS's order, without the 0 DOS pads
  each odd one with. So a script's DOS address follows from the lengths before it.
- `tables.walking_keys` has the Amiga's own code for east; the reader puts DOS's back, because the
  game's keys are DOS's codes.
- The four actuator tables have 7 entries where DOS has 8. The index is a die's 3 to 6, so entry 7
  is never read; the reader writes DOS's 0 in three, and in `leg_actuator_keep` the next table's
  first byte is the same as DOS's entry 7.

Four tables are the Amiga's own:

| Table | Differs at | Why it can't change play |
|---|---|---|
| `rules.armour_prices` | entry 0 | special 9 (24d3:09fa) indexes it by the armour menus' item, and ARMOR.BLD sends item 0 to its end |
| `tables.treatment_cost` | entry 0 | 24d3:0d48 skips a treatment of 0 |
| `tables.upgrade_price` | entry 8 | special 19 reads it into F:0076, but REPAIR leaves for any case above 7 before spending it |
| `tables.overhead_colours` | the last 250 bytes | colour follows the copy; the Amiga has no third table ("Colours") |

`rules.personal_armour`'s entry 0 is not read either: 24d3:0a3c has the same index as the prices,
and the three other readers (1000:3a49, 1000:3acd, 15ab:1a8f) come after a test that the type is
not 0. The reader writes DOS's 0 there. These readers were found by the tables' literal offsets,
so a read through a computed pointer wouldn't show.

Some short tables have their bytes in several places in the data hunk; their addresses were taken
from the order of the tables around them ("by neighbours"), not from the code that reads them:
`alley_offset_x/y`, `overhead_block_x/y`, `overhead_blocks_across/down`, `path_quarters`,
`person_water`, `person_quarters`, `step_x/y`, `los_step_x/y`, `los_area_x/y`, `mech_cell_odd_x/y`.
Each place holds the same bytes, and the data hunk's hash is fixed, so a wrong choice would give the
same table; only its address here, and the claim that the Amiga reads it there, would be wrong.

`sound_lists` and `music_note_hz` have no counterpart: the Amiga has no speaker lists ("The sound
file").

**The font is not in the copy.** The program draws text through graphics.library's Text (0:1c706,
the stub at 0:2848e), in rastports it sets up with InitRastPort (0:1bf9e). It has no call to
SetFont or to graphics.library's OpenFont, no font name, and none of DOS's glyphs in any of the bit
orders tried (as stored, mirrored, transposed, inverted); the disks have no fonts folder. So it
shows the system's default font, which is in the machine's ROM, at the size the disk's
`system-configuration` asks for (8 in all seven copies).

What was searched: both hunks as stored, for DOS's glyphs whole in those four orders (glyphs of
more than three different rows); every use of SetFont and OpenFont by library vector; the library,
device and font names; the absolute address of the two rastports' Font field. What would still
escape: a font drawn differently from DOS's; glyphs wider, taller, interleaved or in bit planes; a
compressed one; one in a picture file; the Font field set through a pointer; and the parts of the
disks outside the game's folder, of which only the folder names and `system-configuration` were
looked at. How it draws the characters below 32 that
DOS's font has as pictures (the arrows, the path marks) was not read. The rebuild shows an Amiga
copy with its own font (docs/formats/font.md, "Our own font").

## The sound file

`snd` (65,532 bytes, the same in all seven copies) is 19 sounds back to back, filling it exactly.
Each is a big-endian word, a big-endian long and that many 8-bit signed samples. The program finds
sound n - 1 by a table of 19 longs, 1:2540. The word is a rate in Hz, which the loader (0:26ce2)
turns into a number it keeps and the player doesn't use; the period played is from a table of 19
words, 1:2590 (samples per second: the Amiga's clock, 3,579,545 for NTSC and 3,546,895 for PAL,
over the period). Word x period is 3.57 to 3.65
million for 18 of the sounds, so there the two agree; effect 16's is 3.22 million, played about a
tenth faster than its word says.

**Playing effect n** (0:265ea, the counterpart of DOS's `1000:19bf`; nothing when 1:0018 is 0, the
sound switch). The sample is loaded the first time. It plays once, at volume 63 of 64, on the next
of the Amiga's four channels in turn (0:272a2), so sounds overlap. Two effects vary the period by
1:7bb8, which the shot sets (0:15570) and clears (0:15a9a): effect 2 by -45, 0
or +45 for 0x10, 0x11 and 0x12, effect 4 by +900 whenever it isn't 0. What it holds (the weapon,
by those values) was not traced. Then, unless 1:1c3e is set (the place pictures' code sets and clears it), the program
waits 35 ticks after effect 3, 20 after 2, 6 and 7, none after 4, 14 and 17-19, until the sample is
done after 8, and 15 ticks after the rest. DOS waits for the sound to end.

**In the rebuild** sounds 1-18 are imported as recordings (`amiga_sound`), each at the NTSC clock
over its period. The game's waits are DOS's (docs/sound.md). Not rebuilt: the two effects' varied
periods, and sounds during the place pictures.

**No title music.** The program has a sequence player for three channels (0:2465c, 0:246ae), with
one caller (at 0:1c880), but its start flag (1:25b8) is 0 in the file, its pointers (1:88ba, 1:88d6) are
empty, and nothing was found that sets either by absolute address. No data of WWOODBT.SIF
(runs of 12 bytes and up) is in either hunk. So it is taken as unused, and the Amiga's title as
silent. A setting through a pointer was not excluded.
