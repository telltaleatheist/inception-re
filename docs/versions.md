# The game's versions

The rebuild plays the same game from a DOS 1.03 or an Amiga 2.3 copy. A Commodore 64 or Apple II copy
is recognised and not playable: its towns are its own, so it can't give the same game (below).
This page surveys the
copies we know of: how each is packed, what files it holds, which of them match the DOS
formats the rebuild already reads, and where each version keeps its tiles, pictures, maps,
dialogue, interface text, tables and sound. It was written by reading the files statically
(no copy was run). It holds no text or bytes of the game: files are named, sizes and
layouts are described, and contents are paraphrased.

What "fill" means below: the DOS importer fills a set of asset IDs (the tile sheets, the
pictures, the place pictures, the maps), text IDs (data/text/interface.json's interface
texts, the place scripts' text) and tables (docs/formats/tables.md, the 'Mech and combat
tables in docs/formats/mech.md). A version can play the same game once its files fill the
same set.

## The copies

| Copy | Container | Version | Variants |
|---|---|---|---|
| DOS | a zip of a folder, 79 files | 1.03 | one: byte for byte our reference copy (all 79 files) |
| Amiga | a zip of 7 zips, each one .adf (901,120 bytes, an 880K floppy) | 2.3 | the plain disk and six others: [a], [a2], [a3], [a4], [h Madwand], [h SSC] |
| Amiga (archive item) | a zip with one .adf and the archive's pictures and metadata | 2.3 | the same disk image as the plain Amiga one (same SHA-256) |
| C64 | a zip with two .d64 (174,848 bytes each, 35 tracks) and an NFO | not stated by the game's files | one pair of disks, from a cracked release |
| Apple II | a zip of 5 zips, each one .dsk (143,360 bytes, a 140K floppy in DOS order) | not stated | disk 1 plain and [cr]; disk 2 plain, [a] and [cr] |
| Apple II (archive item) | a zip with one .dsk and the archive's pictures and metadata | | the same image as Apple II disk 1 plain (same SHA-256) |

So there are four distinct games: DOS, Amiga, C64 and Apple II. The archive items add no
new version.

## DOS 1.03 (the reference)

Covered by docs/original.md and docs/formats/. Everything the rebuild uses comes from here:
the 12 pictures and tile sheets (.CMP, .ICN), 22 place pictures (O0–O21.ANM), 26 place
scripts (.BLD), 15 maps (MAP1–15.MTP), the demo (DEMOFILE), and the program BTECH.EXE, which
holds the interface text, the font, the palettes, the tables and the sound sequences.

## Amiga 2.3

**Container.** An Amiga DOS floppy, the original file system (OFS: 512-byte blocks, the
root directory in block 880, each data block with a 24-byte header). Its volume name
gives the version. The game is in a folder `Battletech`, beside a few system files the disk
needs to boot (a start-up script and two small system commands).

**Files.** The game folder holds:

| Amiga file | Size | DOS counterpart | Form |
|---|---|---|---|
| `infocom.cmp`, `bttitle.cmp`, `BTBORDER.CMP`, `tinyland.cmp`, `mechshap.cmp`, `btstats.cmp`, `endmech.cmp` | 0.3–24K | the seven .CMP | Amiga form of the DOS picture (below) |
| `bttltech.icn`, `destruct.icn`, `starleag.icn`, `map.icn`, `animate.icn` | 3–28K | the five .ICN | Amiga form of the DOS tile sheet (below) |
| `animation.anm` | 134,378 | O0–O21.ANM (22 files, 129,536 bytes together) | one bundle of the 22 place pictures in DOS form (docs/formats/amiga.md) |
| `builds.bld` | 55,861 | the 26 .BLD (85,379 bytes together) | the 26 scripts in one file, DOS's cipher, the text packed in letter pairs (docs/formats/amiga.md) |
| `maps` | 33,046 | MAP1–MAP15.MTP | the 15 DOS maps one after the other, the same total size (below) |
| `demofile` | 1,023 | DEMOFILE | same size; about two thirds of the bytes differ, so recorded differently or in another order; not read yet |
| `snd` | 65,532 | none (DOS plays the speaker from tables in BTECH.EXE) | 19 sampled sounds, 8-bit signed (docs/formats/amiga.md, "The sound file") |
| `battletech` | 204,784 | BTECH.EXE | a 68000 program (AmigaDOS load file: three hunks of 165,104, 35,432 and 4 bytes) |

**The pictures and tile sheets.** Each Amiga .cmp and .icn is the DOS container with a
palette in front:

    +0   16 words, big-endian: the picture's own palette, one 0x0RGB Amiga colour each
         (3 bits a component: docs/formats/amiga.md, "Colours")
    +32  the DOS .CMP container: length (big-endian here), type byte, run-length stream

The run-length code is the DOS one (docs/formats/cmp.md), with the 0 code's 16-bit count
big-endian. Every stream unpacks to exactly 32,000 bytes and ends at the file's end, as the
DOS ones do. The 32,000 bytes are four bit planes of 8,000 bytes, one after the other (the
Amiga's own display form), not the DOS two-pixels-a-byte form. Type 1 is row order within
each plane; type 2 is column order, as in DOS. `map.icn` is the exception: no type byte and
no compression, just 170 tiles of 128 bytes (21,760), each row its four planes' 2 bytes in
turn. Its tiles are DOS `MAP.ICN`'s tiles 80–249 (DOS's first 80 are blank), pixel for pixel.
docs/formats/amiga.md has the details; src/formats/amiga_picture.h reads them.

How close the art is to DOS, as the share of pixels that agree once each Amiga colour is
matched to its most frequent DOS colour: the Infocom logo 100%, the screen frame 99.98% (14
pixels differ), the tiny land sheet 99.5%, the animated tiles 99.1%, the stats screen 98.2%, the
'Mech shapes 96%, the star league sheet 96% and the ending 94%. Only the logo, and
`map.icn`, are exact, so only they test the reader; the others are similarity figures
(docs/formats/amiga.md, "The copy as a source"). So these are the DOS pictures in the Amiga's
colours. The title screen (38%) is redrawn. The town and destroyed-town tile sheets
(`bttltech.icn` 64%, `destruct.icn` 56%) keep the DOS tile numbering and layout (250 tiles of
16x16, tile n at plane offset n x 32) but are shaded with more colours, so several Amiga
colours stand for one DOS colour: the Amiga's own tile art.

**The maps.** `maps` is MAP1–MAP15.MTP concatenated, in DOS order and sizes. The only
differences: in maps 1–14 the four tables of 16 words at 0x185–0x204 (the entrances'
positions) are big-endian, and one people's name field in map 2 differs. Every cell byte is
the same as DOS. So the maps fill fully. The program finds each map by a table of 15
offsets (docs/formats/amiga.md, "The maps").

**Dialogue and text.** The interface text is in the program file, uncompressed: 145 of the
151 interface texts in data/text/interface.json appear in `battletech` letter for letter, and
the rest are there in pieces or with another case (docs/formats/amiga.md, "The texts"). The place scripts'
dialogue is in `builds.bld` (docs/formats/amiga.md): DOS's scripts, enciphered the same way,
with their text packed in letter pairs from two tables in the program. Laid out again in DOS
form they are DOS's scripts (after the edits under "Amiga rules" below), every text the same but for
three colours. So the Amiga fills every place text ID.

**Every text that differs from DOS's** once imported. All nine have DOS's length and letters, and
differ in one thing: the colour number after a foreground colour code (0x06, docs/text.md), once
each, or twice in the two mayor texts. They keep the colour the Amiga has (its number turned into
DOS's, docs/formats/amiga.md "Colours"), as the look follows the copy. None comes from a layout
difference, and since the code takes no cell, no line break and no wait changes.

| Text ID | Colour in DOS | Colour in the Amiga, as DOS's number | Why |
|---|---|---|---|
| bld.citadel.0e71 | 14 | 7 | the Amiga's script has DOS's number 14, which is another colour there |
| bld.mayor.0309 | 14, twice | 7, twice | the same |
| bld.mayor.0550 | 14, twice | 7, twice | the same |
| crew.pilot_label | 2 | 10 | the Amiga program's own colour |
| crew.rider_label | 2 | 10 | the same |
| shot.mech_hit | 13 | 4 | the same |
| shot.person_hit | 13 | 4 | the same |
| ui.answer_no_picked | 0 | 13 | the same |
| ui.answer_yes_picked | 0 | 13 | the same |

**Tables, font, palettes, sound sequences.** In the program's data hunk, as in BTECH.EXE's
data segment. The 68000 program gets its own version-checked span table, like
formats::OriginalExe, mapping each DOS span ID to its Amiga address (docs/formats/amiga.md).

**Saved games.** `Game1`–`Game6` on the [a] to [h SSC] disks are players' saves (the plain
disk has none). Each is 3,929 bytes, 16 more than a DOS save (docs/formats/save.md), with the
same first-byte mark; the extra 16 bytes are not read yet.

**The variants.** All seven disks carry the same game data, byte for byte: every .cmp, .icn,
`animation.anm`, `builds.bld`, `maps`, `snd` and `demofile`. They differ in:

- the program `battletech`: the same on five disks; [a4] and [h Madwand] each have a changed
  copy (same size), probably a patched start-up;
- players' saves (`Game1`–`Game6`) on six of them;
- extra files from whoever made the copy: extra start-up files on [a4], an intro and three
  small extra files on [h Madwand], a longer start-up script with a text file on
  [h SSC], a directory cache on [a2], and different icons.

So one data layout serves every Amiga copy, and one span table its three programs: the changed
bytes of the other two are in none of the spans.

**What the Amiga can fill.** Maps: all. Pictures and tile sheets: all twelve, in the Amiga's
own colours and, for the title and the town tiles, the Amiga's own art. Interface text:
every ID. Place scripts and their dialogue: all 26, every text (above).
Place pictures, the demo, the palettes and the tables: all read (docs/formats/amiga.md) but the
speaker's sound tables, which it hasn't got: it has recorded sounds. The font: not in the copy,
so the rebuild's own is used (docs/formats/font.md).

## Amiga rules

Every copy plays one game, DOS 1.03's. Where the Amiga's game differs, its readers put DOS's
back, always: there is no option to play the Amiga's way. Version-specific code lives only in
the readers (src/formats and the sources); the game, the renderer and the platform never ask
which version a copy is. This table records what the Amiga copy differs in and how the reader
corrects it, each script edit matched by place and by its instruction's offset in the Amiga
script:

| Where | Amiga | DOS (what the reader gives) |
|---|---|---|
| JAIL, the sound at offset 2401 | effect 18 | effect 17 |
| THEATER, the `pay` at offset 240 | charges the player | no charge there (the instruction is left out) |
| THEATER, the sound at offset 542 | effect 12 | effect 2 |
| THEATER, before the print at offset 441 | nothing | a sound, effect 12 |
| THEATER, before the key wait at offset 634 | nothing | two sounds, effects 2 then 1 |
| THEATER, before the key wait at offset 676 | nothing | a sound, effect 1 |
| map 2, the first townsperson's name | a name of its own | the name of map 1's first townsperson, copied from the Amiga's map 1 |

With these edits (`amiga_dos_edits` in src/formats/amiga_bld.cpp, `amiga_map` in
src/formats/amiga_map.cpp) every Amiga script laid out in DOS form is DOS 1.03's byte for byte,
apart from filler bytes no instruction reaches, and every map is DOS's byte for byte.

**Rules follow DOS; look and sound follow the copy.** The art can't be shipped, so what the
player sees comes from the copy they bring, through the same asset IDs and `{"COLOUR", n}`: the
Amiga's pictures, palettes and place-picture animations (frame lists, delays and speed
included). An Amiga copy has no DOS speaker data, so it plays its own sound set, the sounds some
animations start at a set frame included (docs/formats/amiga.md, "The place pictures"). Those
are the only differences between copies, and only because they can't be avoided.

**Known look differences** (docs/formats/amiga.md, "Colours"), left open until an Amiga copy
imports and plays:

- In the Amiga's cache the whole screen is under STARLEAG.ICN's palette. The rebuild gives only the
  cache's tiles those colours. Proposed: an event in game terms, "the screen's colours are now set
  S" (the game's, or the cache's), sent on entering and leaving for every copy; the renderer asks
  the asset provider for set S's 16 colours, which for DOS are the game's for both.
- The menus' highlight bar and the boxes over the map XOR colour numbers. What the Amiga shows there
  has not been read.

**Animation timing changes no game state.** A place picture's frames and delays are waits that
only show the picture (BTECH.EXE 1000:1afd, held by 273d:0006). They read no keys: keys pressed
meanwhile wait in the buffer and are read afterwards, in order. They make no random draws: the
one draw that decides whether a picture shows (1000:48b7) comes before its first frame. And they
don't move the game tick, which counts the main loop's idle passes (1000:01f4, docs/random.md);
those don't run while a picture plays. In the rebuild they are presentation events
(docs/resuming.md). So an animation that is longer or shorter on the Amiga (O4, O5, O6, O9, O10,
O13, O15) changes only how long the box animates. The one place that reads a picture's frame
list for anything else, the 'Mech that won't start (19b8:16b2), uses O0, which the Amiga leaves
as DOS has it. A sound's
length is a wait of the same kind, but DOS's are kept whichever the copy: `snd`'s sounds have other
lengths and the Amiga waits other times (docs/formats/amiga.md, "The sound file"), so neither may
set the game's wait. And DOS's effects leave a word behind that the game reads (docs/sound.md,
bug 21), which is DOS's too. Both are numbers in the game logic (`effect_rule`, src/game/sound.h).

## One source by ID

The game and the asset provider read the player's copy through one interface,
`formats::GameSource` (src/formats/game_source.h). It hands out each piece by what it is, in DOS
1.03's working form: `script(file)` (a place's script, deciphered), `map(n)` (MAPn.MTP's
bytes), `place_picture(n)` (O<n>.ANM decoded), `picture(name)` and `tile_sheet(name)` (a .CMP
or .ICN decoded, by its DOS name, which is also its asset ID), `demo()` (DEMOFILE),
`title_tune()` (WWOODBT.SIF) and `effect_sound(n)` (a recorded sound effect, for a copy that has them). Nothing missing is an empty answer; a broken file throws, as the
readers do. `describe()` names the copy for messages.

`DosGameSource` is the DOS copy: the only place a game file is looked up by name in a folder.
The game logic (src/game) and the platform (src/platform) hold a source, never a path. An Amiga
source, `AmigaGameSource` (src/formats/amiga_source.h), hands out the same forms from its own
files (the readers in docs/formats/amiga.md), with the edits above always applied; its colour
numbers turned into DOS's, and each palette it shows with them (docs/formats/amiga.md, "Colours"). The importer also goes through the source (docs/import.md, "Through the
source"): the program's texts and tables by the import table's IDs, the places' texts from the
scripts.

## Commodore 64

**Container.** Two standard 1541 disk images (35 tracks, 683 sectors), each with a normal
CBM DOS directory: 56 files on disk 1, 97 on disk 2, every file a PRG (two-byte load
address, then the bytes) whose sector chain reads cleanly. The directory names show a
cracked release; the files themselves are the game's.

**Files.** Named by short codes, the same codes as the Apple II's:

| C64 files | Count | What they are (by name, size and load address) |
|---|---|---|
| `BA`–`BZ` | 25 | place scripts, one per place (`B` + a letter), 0.2–5.4K each; there is no `BX`. Side 2 holds `BA`–`BY`; side 1 holds `BA`, `BB` and `BZ` |
| `OA.ANM`–`OV.ANM` | 22 | place pictures: letters A–V for DOS's O0–O21 |
| `MA`–`MZ` | 26 | maps, 1,024 bytes each: 32x32 cells, one byte a cell |
| `G0`–`G7` | 8 | saved games, 1,696 bytes each |
| `TB`–`TM` | 12 | each map's townspeople, entrances and place scripts, 97–199 bytes (DOS keeps them in the .MTP header) |
| `SETA`–`SETE`, `ICNA`–`ICND`, `COLA`–`COLD`, `BSET`/`BCHR`/`BCOL` | | character sets, tile definitions and colour tables (2K, 2K and 128 bytes each): the tile art |
| `TPAG`/`TCHR`/`TCOL`, `EMBM`/`EMSM`/`EMCM` | | two full-screen pictures, each as an 8,000-byte bitmap with two 1,000-byte colour maps (the C64 multicolour bitmap form): by their names, probably the title and the ending |
| `MECHS`, `DET.SCRN`, `I0`, `SPEC`, `INT`, `CBT`, `CCODE`, `CREATE`, `MAKEPLAYER`, `VIEW`, `CVIEW`, `LC`, `BEGTXT`, `SP.TXT`, `F/X`, `ZAP`, `S0`, `D`, `DEMO`, `BOOT`, `WESTWOOD` | | the 'Mech data, the combat and special code overlays, the start-up, and text pieces |

**The maps.** All 26 pages are read (formats/c64.md): maps 1–11 are the C64's own layouts of
the same places, not DOS's cells; map 14 is DOS's in 4,045 of 4,096 cells; map 12 is larger than
DOS's; map 13 has no page of its own. Names, entrances and place script numbers are in the twelve
`T` files.

**Tiles.** 256 tiles of 2x2 characters in four sheets (`SET`, `ICN`; formats/c64.md).

**Dialogue and text.** Not stored as plain text: only 2 of the 151 interface texts appear
letter for letter (in upper case, in two of the start-up text files). The place scripts
(`B` files) are compressed or encoded (about 6.6 bits of information a byte, against 4.7 for
DOS's deciphered scripts). The code that unpacks them is in the 6502 overlays, to be read
statically.

**What the C64 can fill.** Expected: its own maps (not DOS's cells, but for map 14), the place pictures (22, the
same count as DOS), the place scripts (25 of DOS's 26 by count: if the letters follow the DOS
place numbers, A for 0 to Z for 25, the missing `X` is place 0x17, one of the short ones; to be
confirmed), the title and ending pictures, the tiles in its own art. Not
expected in DOS form: the DOS tile sheets' 16x16 four-bit tiles (the C64 has its own tile
form instead), the speaker sound sequences (the C64 has its own sound chip music and effects),
and the font (the C64 uses its own character set). The interface text and tables live in
the code overlays, encoded; how much of the DOS text ID set they hold is not known yet.

## Apple II

**Container.** Two 140K disks in DOS 3.3 sector order, but not DOS 3.3: they use **RDOS
3.3**, a small disk system of its own (its signature is at the start of track 1). Its
catalog is on track 1, 32-byte entries from offset 32: a 24-character name in high-bit
ASCII, a type letter, a sector count, a load address, a length in bytes and a starting
sector (absolute, sectors 0–559). Each file is contiguous from its starting sector. Disk 1
holds 42 files, disk 2 80, and the files fill both disks.

**Files.** The same codes as the C64: `BA`–`BZ` (25 scripts, no `BX`, as on the C64),
`OA.ANM`–`OV.ANM` (21 place pictures: there is no `OB.ANM`), a 393-byte file `B`, `MA`–`MZ` (26 maps of 1,024 bytes), `G0`–`G4` (5 saves), `TB`–`TM`, `COLA`–`COLD`,
`MECHS`, `DET.SCRN`, `SPEC`, `VIEW`, `LC`, plus its own: `BTECH` (the main program, 23,808
bytes), `CBT0`–`CBT4` (combat code), `I0`–`I3` and `SET1` (tile and icon art), `TITLE` (the
title picture), `TRADEM`, `LAM`, and the boot files.

**Compared with the C64** (with the sectors in RDOS's order, formats/rdos.md): the same data, not
an encoding of it.

- Maps: 26,505 of the 26,624 cells of the 26 pages are the C64's. 21 pages are the same file;
  `MB`, `MD`, `MF`, `MH` and `MQ` differ in 14, 16, 2, 18 and 69 cells. `MV`–`MY` are the C64's exactly,
  so they are DOS's map 14 in the same 4,045 of 4,096 cells.
- Entrances, people and scripts: 8 of the 12 `T` files are the C64's. `TG` and `TI` differ in one
  header byte (`TI`'s is an entrance's x position), `TF` in one townsperson's name, `TM` by a missing
  last byte. Of 78 entrances none is at DOS's position and six are within a cell, as on the C64.
- Place scripts: 20 of the 25 `B` files are the C64's byte for byte (after its load address).
  `BF` and `BM` are the C64's and more bytes at the end (1 and 11); `BQ`, `BV` and `BZ` differ.

| Map | Cells | Apple II same as DOS | C64 same as DOS | Apple II same as C64 |
|---|---|---|---|---|
| 1 | 4,096 | 113 | 113 | 4,066 |
| 2 | 4,096 | 106 | 106 | 4,096 |
| 3 | 1,024 | 43 | 43 | 1,024 |
| 4 | 1,024 | 52 | 52 | 1,024 |
| 5 | 1,024 | 70 | 70 | 1,024 |
| 6 | 1,024 | 81 | 81 | 1,024 |
| 7 | 1,024 | 68 | 73 | 955 |
| 8 | 1,024 | 68 | 68 | 1,024 |
| 9 | 1,024 | 40 | 40 | 1,024 |
| 10 | 1,024 | 36 | 36 | 1,024 |
| 11 | 4,096 | 123 | 123 | 4,076 |
| 14 | 4,096 | 4,045 | 4,045 | 4,096 |

These counts are of equal tile numbers. The five pages that aren't the C64's were also put through
the test that allows any renumbering (formats/c64.md, "Compared with DOS"), with the same result:
no DOS page stands out.

**Tiles.** Its own hi-res art (the `I` and `SET1` files), coarser again.

**Dialogue and text.** None of the 151 interface texts appears letter for letter, in plain
or high-bit ASCII (searched with the sectors in the image's order, so a text across a sector's end
would be missed; to be searched again). The scripts are the C64's, so packed as they are.

**The variants** (sectors in RDOS's order). Disk 1 [cr] changes two boot files (`HELLO`, `BOOT0`) and
nothing else. Disk 2 [a] differs from the plain disk 2 in four players' saves (`G0`–`G3`), and disk 2
[cr] in `LC`. What `LC` is is not read.

**What the Apple II can fill.** As the C64, with its own art.

## Recognising a copy

`src/setup/versions.cpp` names any of these copies wherever the player points the setup or
`--status`: a folder, a zip, zips inside zips (any archive the kit reads, four deep), and the
disk images themselves (docs/formats/adf.md, d64.md, rdos.md), loose or inside archives. It
lists the files of every disk image it meets and hashes only those whose name and size are
in its table, `known_disks()`. The table holds each disk's file names, sizes and SHA-256
hashes, nothing else:

| Disk | Recognised by | Variants told apart by |
|---|---|---|
| Amiga 2.3 (one disk) | all 17 game data files in `Battletech` | the program `battletech`: the plain build (five disks), [a4]'s and [h Madwand]'s |
| C64, side 1 | three game files found only on side 1 | (one release known) |
| C64, side 2 | three game files found only on side 2 | |
| Apple II, disk 1 | the program and two art files | the boot file `HELLO`: plain or [cr] (which also changes `BOOT0`) |
| Apple II, disk 2 | three game files | none: [a] differs only in its saves, [cr] in `LC` |

A disk whose recognising files all match but whose variant file matches none is still named,
as changed from the copies we know. The C64 and Apple II copies are named and refused: their maps 1–11
are the C64's own, not DOS's (formats/c64.md), and a DOS or Amiga copy is needed.

DOS 1.03 and Amiga 2.3 play (docs/setup.md, "What counts as the game"); the releases before 1.0.0
play DOS 1.03 alone. For the others `--status` answers `incomplete` for it, with `data` naming it
("Commodore 64"), `missing` naming a DOS 1.03 or Amiga 2.3 copy, and a note of why
(the same the setup shows). The setup says the same instead of "no copy found", both for a scripted pick
and on the setup screen (the kit's `recognise` hook, which the kit calls only when its own walk
finds no copy). The DOS copy, in a folder or a zip, is recognised as before, by its BTECH.EXE.

The checks can't use the real table without the real disks, so `CHR_EXTRA_KNOWN_DISKS` may
name a file of extra entries, one disk a line (`src/setup/versions.h`); the status and setup
check makes up a C64 disk image and lists it there.

## Asset and text families by version

| Family | DOS | Amiga | C64 | Apple II |
|---|---|---|---|---|
| Full-screen pictures (logo, title, stats, ending, frame) | 7 .CMP | 7, DOS container with a palette, bit planes | title and ending as multicolour bitmaps; the others not seen as files | `TITLE`; others not seen as files |
| Tile sheets | 5 .ICN | 5, same container, same tile numbering | its own character-set tiles | its own hi-res tiles |
| Place pictures | O0–O21.ANM | inside `animation.anm` | OA–OV.ANM (22) | 21: no OB.ANM |
| Place scripts and dialogue | 26 .BLD, enciphered | inside `builds.bld`, enciphered, text in letter pairs | 25 `B` files, encoded | 25 `B` files, 20 of them the C64's |
| Maps | 15 .MTP | `maps`: the same 15, three tables big-endian | 26 pages of 32x32, the C64's own layouts but for map 14; names and entrances in `T` files | the C64's 26 pages but for 119 cells |
| Interface text | BTECH.EXE | the 68000 program, 145/151 plain | code overlays, encoded | code overlays, encoded |
| Font | BTECH.EXE | not in the copy: ours | the C64's own characters | the Apple's own |
| Tables ('Mechs, combat, prices) | BTECH.EXE | the program's data hunk | `MECHS` and the overlays | `MECHS` and the overlays |
| Sound | speaker sequences in BTECH.EXE | `snd`: 8-bit samples | its own sound chip routines | the speaker, its own routines |
| Demo | DEMOFILE | `demofile` (same size, another form) | `DEMO` | not seen |
| Saves | Game1–6, 3,913 bytes | Game1–6, 3,929 bytes | G0–G7, 1,696 bytes | G0–G4, 1,696 bytes |

## What can't be filled from the DOS ID set, so far

- **C64 and Apple II have their own art and screens.** Where DOS has a picture or tile with no
  counterpart (the frame, the 'Mech shapes sheet, the stats screen, the tiny-land sheet), the
  8-bit versions draw that screen differently. The proposal: those asset IDs get the version's
  own drawing where it has one, and the version's screen layout data says which IDs it uses,
  so the logic is untouched and the renderer draws what that copy has.
- **The font** on the 8-bit versions is the machine's character set, not an imported 8x8
  table. The proposal: import it the same way, as {"FONT", char} glyphs, from wherever each
  machine's program keeps it.
- **Speaker sound.** Only DOS has the speaker sequences as tables. The Amiga has samples; the
  8-bit versions have their own sound code. The proposal: per-version sound IDs, and a
  version without an importable sound plays the DOS-shaped silence-and-tone events through
  its own provider (the sound stays an option).
- **One of DOS's 26 place scripts** has no `B` file of its own on the 8-bit versions (no `BX`),
  and the Apple II has no `OB.ANM` place picture. Whether those places are merged into others,
  drawn another way or absent from those versions is the first thing to find there.

## Not going further

The C64 and the Apple II are not read further: their copies stay recognised and not playable.
The proposals above for their art, font and sound are not being built.
