# Saved games (GameN)

The command menu's items 5 and 6 load and save a game (docs/town_loop.md): `1000:32b3` and
`1000:35d3`. A saved game is one file in the game's folder, named `Game` and the slot's digit
(DG:0154, the digit at DG:0158 = '1' + the slot), slots 0-5 from menu 0x28, so `Game1` to
`Game6`. The menu's item 6 cancels.

## The file

3913 bytes, little-endian, no header beyond the first byte:

| Offset | Size | What |
|---|---|---|
| 0 | 1 | 0x0c, the format's mark; a load refuses any other |
| 1 | 0xf44 | F:c614-d557, as it is in memory |
| 0xf45 | 2 | the party's x (1c6c:a44b) |
| 0xf47 | 2 | the party's y (1c6c:a44d) |

The block from F:c614, by what is known of it (offsets in the block are address - 0xc614):

| F: | Size | What |
|---|---|---|
| c614 | 16 x 0x11 | the character records (docs/formats/characters.md) |
| c724 | 8 x 0x7d | the 'Mech records (docs/formats/mech.md) |
| cb0c | 0x800 | the seen map (docs/wilderness.md, "The seen map") |
| d30c | 0x64 | the script flags (docs/scripts.md) |
| d370 | 4 | C-Bills |
| d374 | 3 x 4 | the three investments |
| d380 | 0x10 | not known |
| d390 | 8 x 0x1a | the townspeople's records (docs/townspeople.md) |
| d450, d451 | 1 + 1 | the care's kits (docs/combat.md) |
| d452 | 4 | byte 0 of 'Mech records 0-3 while held at the garage |
| d456 | 1 | the next character name number |
| d457 | 64 x 4 | the map marks: codes, then areas (d497), x (d4d7), y (d517) |
| d557 | 1 | the marks' next slot |

So a save holds the party, its 'Mechs, the seen map, the flags (the story so far), the money, the
townspeople and the marks, and the place; not the random numbers, the menus' last items, the
combat settings, or anything the load rebuilds below.

## The save: 1000:35d3

1. With F:d34e set (the cache's second level) it refuses: the message box, DG:06d2, a key.
2. Window 3 cleared, DG:0673, and menu 0x28 (`1656:0b5e`): item 6 ends it.
3. The disk check: `1000:28cc(3)`, then INFOCOM.CMP (DG:06a1) opened and closed, the disk
   prompt (`1000:2913`) until it opens.
4. The file created (`287f:33d0`, modes 0x8101 and 0x180), the four writes above (`287f:366a`),
   and closed. A failed create or a short write shows DG:06ad in window 3 and waits for a key.
5. `1e31:032f(0)`, the panel (`1000:4cac(1)`), `1000:28cc(1)`.

## The load: 1000:32b3

1. Window 3 cleared, DG:05c7, menu 0x28; item 6 ends it. The disk check as above (DG:05f5).
2. The file opened; if it cannot be: DG:0640, then the colour (DG:37fe) set to 2, the file's
   name, DG:0658, a key, and step 7. The colour stays 2 afterwards.
3. The first byte read; if it is not 0x0c: DG:0601, a key, and on to step 6 with nothing loaded
   (the file is left open).
4. Area 0xcc's map 1 and area 0x34's 0 (27e8:00fc, 0064); the block and the position read.
5. Then rebuilt from what was read:
   - the 33 bytes at F:45de (the cache doors' used items) 0;
   - the word at 02f8 of the segment named at DG:53e6 = flag 4f (F:d35b), sign-extended;
   - area 0xcc's map 11 when flag 04 (F:d310) is set, area 0x34's map 12 when flag 32
     (F:d33e) is set (docs/wilderness.md);
   - with flag 3a (F:d346, the cache) clear, the nine-map load around the position: its area,
     taken as the high byte of x | y (the same as the area for every position the game makes:
     x's high byte is 0-0f, y's a multiple of 0x10), the terrain (`287f:104e`), then `2da8` for
     each of the nine areas from area - 0x11 (rows of 0x10, no wrap at the edges) that has a map,
     into grid 3 row + column, and `287f:1da8`; with it set, the cache's own load (`1b5d:0004`);
6. For the 'Mech figures 0-3: the sprite base 0x92, or 0 when the record's byte 0 is 'L' (the
   same test as an encounter's, docs/combat.md); the frame (F:409a), the heading (F:3920) and
   the direction (F:396c) 0; the far pointer at DG:01f6 + 4i = 27e8:0270. F:374a (the double
   agent found) 0. Outside the cache, with tile set 2 loaded (F:3988), `1000:4621` loads
   BTTLTECH.ICN back.
7. `1e31:032f(0)`, the panel (`1000:4cac(1)`), `1000:28cc(1)`.

The random numbers are not saved: a game loaded goes on with the session's. Each map the load
reads places its townspeople again (`2da8`, eight numbers each), as any map load does, so the
townspeople's records read from the file are replaced and the numbers move on.

## In the rebuild

The save and the load are rebuilt (`Town::save_game`, `Town::load_game`, src/game/save.cpp),
from the command menu, from the start-up's question (`1000:50c8`: a new game, `4dc7`, then
`32b3`, then the main loop), and in the cache (`Town::enter_cache`).

**Original bug, kept: a game saved in the cache loses its 'Mechs.** Going into
the cache puts each of 'Mech records 0-3's first byte aside at F:3248 and sets it to ff
(`1b5d:0004`); leaving puts them back. F:3248 is not in the file, so a game saved inside holds
ff there, and the load, which goes into the cache again with the same routine, puts the ff
aside: the party comes out without its 'Mechs. A fix would need bytes the file does not have,
and the file must stay the original's.

Planned fix (low priority, not built): behind a fix switch, the save also writes a small sidecar
file next to GameN (for example `GameN.chr`) holding the four parked bytes, and the load puts them
back from it when it is there. GameN itself stays byte for byte the original's, and with the
switch off nothing changes.

**Where the files go.** In the player's data folder by default (where the imported text is,
docs/import.md), or wherever `--save-dir DIR` says. The names are the original's, `Game1` to
`Game6`, and the bytes are the original's, so pointing `--save-dir` at the game's own folder
lets the rebuild and the original read each other's saves. Each is read or written once, however
often the sitting is played again (docs/resuming.md).

**A short file** is read as far as it goes: the original does not check the reads' counts, so
whatever the file leaves out stays as it was in memory, and the rebuild does the same.

**The block.** The rebuild writes the file exactly as laid out above. The parts it holds as its
own members (`save::kHeld`, src/game/save.h: the records, the seen map, the flags, the money,
the townspeople's bytes 0-9, the kits, the held 'Mechs, the name number, the marks) are written
from those members over a shadow of the whole block (`Town::save_shadow_`), which keeps every
other byte (F:d380-d38f, the townspeople's bytes 0x0a-0x19) as the last load read it. In a new
game the shadow is all 0: every byte of F:c614-d557 is 0 in the program image, and a new game
(`1000:4dc7`) sets none of the unheld bytes except the seen map's preset, which the rebuild
builds itself (`Town::preset_seen`). Nothing of the original's state is copied into the
rebuild.

**The texts** are DG:0673, DG:06ad and DG:06d2 (`save.which`, `save.failed`,
`save.refused`), and the load's DG:05c7, DG:0601, DG:0640 and DG:0658 (`load.which`,
`load.not_a_save`, `load.not_found`, `load.not_found_end`), imported (docs/text.md). The file
name between the last two is the file's own name. The failure is printed in colour 4 (2 with the CGA
display, DG:4fba = 0; only EGA is rebuilt).

**Not rebuilt:** the disk check (`1000:28cc`, INFOCOM.CMP and the disk prompt): there are no
disks.
