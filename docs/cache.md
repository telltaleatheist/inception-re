# The cache (map 14) and the end of the game

How the party gets from ENTRANCE (map 13, area 0x66) into the Star League cache (map 14), what
each kind of floor tile does there, and how the game ends. Segment 1b5d holds all of it, 13
functions in about 3.4 KB. Nothing else in the program calls them except the two places below.
Text in this document is named by its EXE address only: the strings come from the player's
BTECH.EXE through ExeSpan IDs (docs/text.md), and the place scripts from their .BLD files.

## Going in (BTECH.EXE 1000:7f46, then 1b5d:0004)

Leaving place 0x14 (ENTRANCE) with flag 36 set (docs/scripts.md, "Entering a place", step 9),
`1000:7f46` clears area 0xcc's column of the seen map (byte 0 of the eight 16-byte rows at
F:d118, docs/wilderness.md, "The seen map") and calls `1b5d:0004`:

1. The four party 'Mech records (F:c724 + 0x7d m, m 0-3) are parked: byte 0 of each is kept at
   F:3248 + m and then set to ff, so the records read as empty.
2. Every character's 'Mech slot (byte 0x0c of the eight party records, F:c620 + 0x11 i) is set
   to 0.
3. The party is placed at 0c06,c07e, and 27e8:01b0 (a table, through `287f:00d1`) is copied
   in. Then `1000:28cc(2)`, the disk request the rebuild drops.
4. All nine area grids (P:0564, 0x240 bytes) are filled with block d0, and map 14 is loaded into
   grid 4 (`1000:2da8(4, 0xe)`). Entry d0 names block 0x50 of the block store: map 14 fills all
   64 map blocks (P:101d-201c), so block 0x50 is the 64 bytes after them, at 2c6c:201d, which
   the program image fills with tile f5 (imported as `tables.store_tail`, with block 0x51). The load queue (P:09f3, three bytes) is cleared to ff. F:4fbc
   is set to 1.
5. The tile set named at DG:2068 is loaded (`273d:063b`, `049d`) and, on EGA (F:4fba = 2),
   converted (`287f:0260`). F:3988 = 2.
6. The window is built and drawn (`287f:1da8`, `1314`, `18ef`, `273d:06c3`), then a delay of
   0x3c (`273d:0006`).
7. `1b5d:0ab6(0xc03, 0x97d, -1)`: the door at the way in opens, with its sound and frames
   (below).
8. Every character's 'Mech slot is set to 8 (on foot).
9. Place 0x17 (INSTRUCT) runs (`17dc:0008`).
10. For each door d 0-10 whose flag (F:d34f + d, flags 0x43-0x4d) is set, `1b5d:0ab6(0, 0, d)`
    opens it again without a sound or delay, so a reloaded cache keeps its open doors.

All of this runs inside the step that answered ENTRANCE's question (`1000:1c12`), which then
goes on as after any place: the rest of the step is checked against the cache's window and the
party moves one half-cell from 0c06,c07e. The walking frames (`1000:231d`) follow that step.

F:d118 is part of the seen map, which the main loop writes through an index; the rebuild clears
the same eight bytes.

## The step check (BTECH.EXE 1000:1c12)

`1000:1c12(dx, dy)` checks a party step before it is taken. While F:d346 (flag 3a) is set, which the
map load `1000:2da8` sets for map 14 and clears for any other (docs/movement.md), the characters
on foot are checked in figure order as on any map, but none is asked about an entrance. The
first whose tile is at or above the walk threshold (DG:0150) stops the step, and its tile is
dispatched by its number t, with (x, y) that character's half-cell after the step (its
formation offset, DG:0464 / 047c, plus the step plus the party's place, P:a44b / a44d). The
characters after it are not checked, and the step is refused whatever the tile did; a place the
function moves the party to (the way down, the way up) stands, since the step leaves the place in
memory as it finds it. Any other
tile at or above the threshold only blocks. All the tests are made on the same t, and the tile
numbers they name do not overlap, so at most one function runs:

| Level (F:d34e) | Tile | Function |
|---|---|---|
| 0 | 7e, 7f, 80 | `0ab6(x, -1)`, `0ab6(x - 2, -1)`, `0ab6(x - 4, -1)`: a door (its three tiles) |
| 0 | f5, x 0c01-0c04, y c054 | `01e9` |
| 0 | b6, b7 | `03aa(x, y)`: an item |
| 0 | above f5 | `0288` |
| 0 | 83, a5-a7 | `02a8` |
| 0 | 4d | `04ab(x, y)`: the way out |
| 0 | 3a, 3d | `02d2(x, y)` |
| 0 | 28 | `055a(x, y)` |
| 1 | 94 | `079c`: back to level 0 |
| 1 | 97-f0 | `0913(x, y)`: a puzzle cell |
| 1 | 8c, 8d | `0980`: the puzzle's check |

The same function refuses a step off the map's edge where the next area is block 0f (DG:0410),
as on any map. Its 'Mech check, which asks at an entrance (DG:0446 / 045a), does not look at
F:d346, but in the cache no 'Mech walks.

A function that shows a message sets F:d55c, and the main loop then writes no direction in the
upper panel box for that step.

While F:d346 is set, the figure drawer `1000:051b` draws no marks (`1000:2a93`) and sinks no
figure, person or 'Mech, whatever the tile under it.

## Items and doors

**Items (`1b5d:03aa(x, y)`).** 33 items lie on tiles b6/b7 (terminals on the map). Item i lies
on three half-cells from x DG:21ce + i, at y DG:21f0 + i (`tables.item_x`, `item_y`), and has a
slot (DG:2212 + i, 0-2, `tables.item_slot`) that is also its kind: the far-pointer table at DG:2234
(four bytes per slot) names the kind's text, DG:214d, 2151 or 2156. The step's half-cell is taken
as x `(x + 1) & 0x7f` and y `y & 0x7f`. Every item it falls on is handled in turn (the loop does
not stop at the first):

- the message box opens (`24d3:17c6`);
- an item no door has used (F:45de + i clear) asks, in DG:2187, the kind, DG:21b2, the number
  i + 1 (`273d:0053`) and DG:21b9, whether to take it (`1000:1a13(1)`, Yes first). Yes stores i in
  the slot's byte, F:d347 + slot, in place of whatever it held;
- an item already used prints DG:215d (`24d3:17ea`) and waits for a key (`273d:0259`);
- either way F:d55c is set, so the step shows no direction.

**Doors (`1b5d:0ab6(x, y, d)`).** Twelve doors, numbered 0-11. Door d's cell is x DG:247a + d,
y DG:2486 + d, and it needs three items: one per slot, numbers DG:2492 + d, DG:249e + d and
DG:24aa + d, each minus 1.

- Called from a step (d = -1), the door at (x, y) is looked up (x as `(x + 1) & 0x7e`, y as
  `y & 0x7e`). Unless it is door 11, the party must carry exactly its three items: for each slot
  that differs the message box shows DG:242c, DG:2441 or DG:2457 in turn, then the box waits
  (`273d:086a`) and a key (`273d:0259`), and the step ends; the door stays shut (its found flag
  is cleared before the opening part). A door not in the table does nothing. The way in, door
  11, needs no items.
- When the door opens: door 11 empties all three slots (F:d347-d349 = ff); any other door marks
  its three items used (F:45de + item = 1). The door's flag (F:d34f + d) is set. Called from a
  step, it plays sound 10 (`1000:19bf`). Three frames follow (from frame 2 when called with a
  door number): four tiles from DG:246e + 4 per frame are written into the map's cells (P:101d,
  four across from the door's cell, wrapping into the next block at the eighth column), and the
  window is built and drawn with the figures; from a step, each frame waits 0x14.

## The other tiles

- `1b5d:01e9` (tile f5 at x 0c01-0c04, y c054): writes six rows of three tiles from DG:20bc into
  the map at P:101d + 0xa10 (eight bytes a row): cells x 0-2, y 42-47, the wall in front of the
  ENDMECH tiles (cells 1-2, 49-50), which no other way reaches. It then builds and draws the
  window with the figures and shows DG:2075 with a key. No flag records it: the rewritten cells
  hold no f5, so it cannot run again.
- `1b5d:0288` (tiles above f5): while F:d34b is clear, place 0x16 (ENDMECH) runs.
- `1b5d:02a8` (tiles 83, a5-a7): DG:20ce with a key.
- `1b5d:02d2` (tiles 3a, 3d): where `(x + 1) & 0x7e` is 4e and `y & 0x7e` is between 0c and 10,
  DG:2113 with a key, and F:d34c = 1. Elsewhere these tiles only block, with no message, so the
  direction is still written.

Each of the three opens the message box first, prints its text with `24d3:17ea` and sets F:d55c.
- `1b5d:055a` (tile 28):
  - at x 7c, y 04: place 0x18 (FINDIT) runs, and F:d34d = 1;
  - at x 48, y 38: with F:d34a = 1 (the puzzle solved), DG:22c7 with a key. Otherwise DG:22e8
    with a key, the message box cleared (`2656:0388`), DG:2328 with a key, and the second level:
    below.
- `1b5d:04ab` (tile 4d at x 04, y 04, or x 02, y 0e), the way out:
  - F:d34c clear: DG:2240 with a key;
  - F:d34c set and F:d34a = 1 and F:d34d set: place 0x19 (WINSCENE), then DG:01a8 = 1, which
    the main loop takes as the game's end (docs/town_loop.md, step 7);
  - F:d34c set and F:d34a = 1, F:d34d clear: DG:228b with a key;
  - F:d34c set, F:d34a not 1: DG:2275 with a key.

## The second level

The way down opens the message box and sets F:d55c first; with the puzzle solved (F:d34a = 1) it
shows DG:22c7 with a key and goes nowhere. Otherwise, after its two texts, it sets DG:015a = 1
(one step a key, docs/movement.md; the rebuild always takes one) and the party at 0c04,c022; copies 27e8:01d0
(`287f:00d1`); `1000:28cc(2)`; fills the nine grids with d1; sets F:4fbc = 1; loads the tile set
at DG:2355 (converted on EGA); keeps the level's 0x300 map bytes (P:101d) at F:4614; sets F:3988
= 3; loads DG:235d (`273d:0814`); sets F:d34e = 1; writes 90-9b, three rows of four, to the block
table at P:0664 (eight a row); builds and draws the window (`287f:0ba7(1)` on CGA); and sets the
walk threshold DG:0150 to 0x8b.

`1b5d:079c` (tile 94) comes back: DG:2367 with a key; the party at 0c45,c039; 27e8:01b0 copied;
`1000:28cc(2)`; the grids filled with d0; the map bytes restored from F:4614; F:4fbc = 1; the
tile set at DG:23a5; F:3988 = 2; the block table P:0664 set to 90-cf in order; the window built
and drawn (`287f:0ba7(0)` on CGA); F:d34e = 0; the threshold back to 0x21.

The level's file, MAP15.MTP (DG:235d), is only the twelve blocks' 0x300 bytes, with no header,
and the tile set is MAP.ICN (DG:2355). It overwrites the first twelve of map 14's 64 blocks
in the store, after they are kept; the other 52 stay, and no grid entry names them while the
level is shown. The grids' other entries, d1, name block 0x51 past the store (`tables.store_tail`).
In the twelve blocks, four a row, the level is 32 cells across and 24 down.

`1b5d:0327` swaps 250 bytes at P:215d with P:2257 (level 0) or P:2351 (level 1): the overhead
map's cell colour tables, the land's and the two levels' (`tables.overhead_colours`). Its only
caller is `1000:3d40`, the overhead map, which calls it before and after while F:d346 is set
(docs/overhead_map.md).

**The puzzle.** On level 1, tiles 97-f0 are puzzle cells in pairs: `1b5d:0913` turns the cell's
tile into its pair and plays sound 15. It finds the cell in the store as the level lays it out,
four blocks a row (`((y & 0x70) << 4) + ((y & 0x0e) << 2) + ((x & 0x70) << 2) + ((x >> 1) & 7)`,
x being `(x + 1) & 0x7f`), and neither it nor `0980` builds the window again: the changed tiles
show when the window is next built. `0913` shows no message, so the step's direction is still
written. `1b5d:0980` (tiles 8c, 8d) checks: solved when none of the seven cells listed at
DG:241e (map offsets) holds an odd tile, and every even tile in 97-f0 on the map is one of those
seven. Solved: sound 16, DG:23b2, F:d34a = 1. Not solved: sound 17, DG:2409, and every even
tile in 97-f0 on the map goes down by one (reset). Either way a key, and the step ends.

The toggle `((t + 1) ^ 1) - 1` maps an odd t to t + 1 and an even t to t - 1.

## Flags used

| F: | Flag | Meaning |
|---|---|---|
| d346 | 3a | the cache's step dispatch is on (and random encounters are off) |
| d347-d349 | 3b-3d | the item carried in slots 0-2, or ff |
| d34a | 3e | the puzzle solved |
| d34b | 3f | ENDMECH's tiles off (set by ENDMECH's script) |
| d34c | 40 | 02d2's text seen: the way out answers |
| d34d | 41 | FINDIT visited |
| d34e | 42 | on the second level |
| d34f-d35a | 43-4e | door d open |
| 45de + i | - | item i used (by a door) |

## The way through

The route the main chain takes from its first step inside (`chain_entrance`) to the way out,
with no pokes. Every one of the 33 items is needed exactly once: the eleven doors 0-10 ask for
three each, all different. A door's three items are one of each kind, so the party fetches a
door's three, opens it, and goes on to the next; nothing is carried from one door to the next.

The order was found with `chr --walks` (`Town::walks`, docs/reference.md), which lists the
shortest walk from the party's place to every tile that stops a step, by the rebuild's step
check: from where the party stands, the next door is one whose three items and whose own tile
can all be reached, the nearest by the sum of those walks; its items are fetched nearest first,
each taken with Enter (Yes is the question's first answer), and the door bumped. The walks are
found again after each item and each door, from the state the chain has reached, since a door
changes the map. None of the walks steps on a tile that runs a function before its end.

The doors open in the order 1, 2, 0, 6, 10, 8, 4, 5, 3, 9, 7, in 5,670 steps in all (an earlier
estimate, from a different search, was about 7,800). Then the game is saved and loaded, and the
party walks to 02d2's text (256 steps), a screen tile (83), the wall of 01e9 (395) and the way
down (261). The way down puts the party at 0c04,c022 and the way up at 0c45,c039, whatever the
way there, so from the second level on the chain takes the same keys as `chain_cache_open`:
the puzzle, the way up, ENDMECH, FINDIT, the way out, WINSCENE and the game-over question.

## Rebuilt

Going in (`Town::enter_cache`, `src/game/cache.cpp`), the doors (`Town::open_door`) and the step
dispatch (`Town::cache_step`, called from the step with the tile `take_step` found) and the
items (`Town::take_item`) and the text tiles (`Town::open_cache_wall`, `Town::cache_hint`,
`Town::cache_message`), with the tables at DG:20bc, DG:21ce-2232 and DG:246e-24b5 and the
texts imported from the EXE (`tables.door_*`, `tables.item_*`, `tables.cache_wall`,
`cache.door_wants.0-2`, `cache.item_*`, `cache.wall_opened`, `cache.screen`, `cache.hint`).
Checked against the original on two side branches:

- `chain_cache_poked`: ENTRANCE, the way in, INSTRUCT, the first step inside, a walk to door 0,
  the door refusing a party without its items, and a terminal's item offered and taken.
- `chain_cache_open`: the same way to ENTRANCE's door, but with doors 0-10 marked open there
  (flags 43-4d, a test poke standing in for about 7,800 steps through the doors with their
  items), so going in shows them open; then 02d2's text and flag, a screen tile, and the wall
  of 01e9 opening, each on a walk through the open doors.

The second level and the puzzle (`Town::cache_stairs`, `Town::cache_way_up`,
`Town::cache_puzzle_cell`, `Town::cache_puzzle_check`), with the table at DG:241e and their texts
(`tables.puzzle_cells`, `cache.way_down*`, `cache.way_up`, `cache.puzzle_*`), are checked on
`chain_cache_open` too: the way down, a wrong cell turned and the check resetting it, the seven
cells turned and the check solving it, the way up, and the way down then refusing.

ENDMECH (`Town::cache_endmech`, place 0x16, with special 45's picture) and FINDIT (place 0x18
from `Town::cache_stairs`) are checked on `chain_cache_open` after the puzzle: each walked to,
its tile bumped, and its script read to the end. ENDMECH's script sets F:d34b itself, so the
tiles above f5 only block after it.

The way out (`Town::cache_way_out`, texts `cache.out_closed`, `cache.out_puzzle`,
`cache.out_findit`) is checked on `chain_cache_open` last: the party walks from FINDIT to the way
out at x 02, y 0e, bumps it with all three flags set, reads WINSCENE to its end, and the main
loop asks the game-over question (docs/town_loop.md, step 7). Its three refusals are not walked
to: each is a text and a key, the same as the tiles before it.

`chain_finale` (campaign tier only) is the main chain carried through the cache unpoked, by the
route above ("The way through"): each item taken and each door opened is compared, then the save
and load after the doors, the four tiles before the way down, and the rest as `chain_cache_open`
has it.
