# The land outside the towns

The world is 16 x 16 *areas*, each 64 x 64 cells: 8 x 8 *blocks* of 8 x 8 cells. A position's
x bits 8-11 and y bits 12-15 name its area (formats/mtp.md); in this document an area is the byte
`0xYX`, so the town of map 1 (the new game's start, x 0x0c45, y 0xc019) is area `0xcc`. A town's
map covers some of its area's blocks (map 1 covers all 64). Every other block of the world is a
*terrain block*, which the program makes up as the party comes near, from one height per area and
a table of random numbers. The same place always gets the same terrain.

The program keeps the nine areas around the party's, in the asm display library's data (P, 2c6c):

| P: | Size | What |
|---|---|---|
| 0324 | 9 x 64 | each block's *neighbour mask* (below), grid by grid |
| 0564 | 9 x 64 | the nine *grids*: one entry per block, row by row; grid k is the area 16 (k / 3 - 1) + (k % 3 - 1) from the party's, so grid 4 is the party's own |
| 07a4 | 9 | the window's nine blocks, mask OR entry (287f:13d9's result) |
| 07ad | 24 x 24 | the *cell window*: the 3 x 3 blocks around the party's block, one tile number per cell |
| 09ed | 6 | the corner words: the view's corner index, its column, its row x 24 (`287f:1df8`; the map drawer `287f:18ef` sets only the first) |
| 09f3 | 6 | the map-load queue: three grids (ff: none), then their areas (below) |
| 09f9 | 2 | the noise cursor: where the generator's noise index was left |
| 09fb | 256 | the noise table |
| 0afb | 16 | 0 |
| 0b0b | 256 | the world's heights, one per area, row by row (`tables.world_heights`) |
| 0c0b | 18 | 0 |
| 0c1d | 64 x 64 | the block store: 16 terrain templates (`tables.terrain_templates`), then the loaded map's blocks (1000:2da8 reads its cells to 0x101d) |

A grid entry below 0x80 is a terrain type (0x00, 0x10, 0x20, 0x30 or 0x40); 0x80 and up name a
block in the store, 0x80 + n for block n, so the map's blocks are 0x90 onwards.

## The noise table

Start-up (1000:50c8) draws the first 256 random numbers into P:09fb, before anything else draws
one, from the fixed image state, so the terrain is the same for every player (docs/random.md).
Nothing writes it again.
The rebuild computes it the same way (`Terrain::start_up_noise`); the rig compares it with the
original's memory in every case (`state noise`).

## The generator: 287f:104e

`287f:104e(area)` makes all nine grids, then their masks (287f:1886). For grid k it takes the
heights of four areas: its own (the north-west corner), the next east, the next south and the
next south-east, `world[a]`, `world[a + 1]`, `world[a + 16]`, `world[a + 17]` for
`a = area - 0x11 + 16 (k / 3) + k % 3`. So an area's corner heights are shared with its
neighbours: the land runs on across area edges.

The index is not wrapped. At the world's west and east edges the next column is the next row's
first or the last row's; above the first row the program reads the noise table's end, below the
last row 18 bytes of 0 and then the templates. (Bug 31 is the same at a map load's columns.)

**`287f:0bfb`** fills a 9 x 9 lattice of heights (P:02d3; the ninth row and column are the next
areas' first) by midpoint displacement. Every point starts empty (0xff) but the corners.

1. **The edges** (287f:0d07 across, 287f:0d79 down): top, bottom, left, right, in that order.
   An edge is halved again and again: the midpoint of an empty point between two made ones is
   their average plus a *displacement*. The halves wait on a stack and the second half is done
   first. Each edge's noise starts at entry *sum of its two corners* of the noise table and moves
   one entry for each point made: two areas meeting along an edge make the same points on it.
2. **The inside** (287f:0df8) goes on from where the right edge's noise ended. Each square gets
   its centre, the average of its four corners with no displacement; then the midpoints of its
   top, left, bottom and right sides, if still empty, each its two ends' average displaced; then
   its four quarters, the last (south-east) quarter first.

The displacement over a half of *c* lattice cells: the next noise byte AND (4c - 1), less 2c,
added in 8 bits; a result of 0x80 or more (past the top, or below zero) becomes 0. So a half of
4 cells moves by -8..7, of 2 by -4..3, of 1 by -2..1.

**Bug 37** (`fix_terrain_noise`, off): the inside's right sides use c / 8 (that is, 0) for their
displacement. The mask is then 0xff and nothing is taken off: the whole noise byte is added, and
the point usually comes out 0 or high. The left sides divide the down distance by 8, which is in
ninths of the lattice (9 per row) and gives the right c; the right side divides the across
distance, already in cells. The fix uses c. Turning it on changes every outdoor area in the
world (not only a rare case), so it is a mod-level switch; the rig runs with it off.

The area's types are its lattice's first 8 x 8 points AND 0xf0. The world's heights are 0-0x50;
the shipped table gives types 0x00-0x30 in the areas around the towns.

## The map's blocks: 1000:2da8

The map load writes the map's blocks into its grid (2da8's first argument; the new game uses grid
4): `0x90 + n` for the map's block n, at the block column and row its header gives, `width / 8`
by `height / 8` of them. The rest of the grid keeps its terrain. It then rebuilds the masks
(`287f:1da8`, the far call of 1886). The new game (1000:4dc7) runs `104e(0xcc)`,
`2da8(4, 1)` and `1da8`.

There is one block store and one set of map tables, whatever the grid. A load into any grid
reads the map's cells into P:101d, numbered from 0x90 as always, and its header's tables over the
last map's: the entrances, the places, the blocking tile (DG:0150), the tile sheet. So every grid's
map entries now show the map loaded last, and the town the party stands in takes the new map's
places and townspeople. Unless DG:014c is set (only the area viewer, 1000:3d40, sets it), the load
also places the eight townspeople again with eight random numbers, at the new map's entrances
(none out on maps above 10; docs/townspeople.md). Rebuilt (`Town::load_town_map`).

Each area's map comes from a 256-entry table (at the segment in DG:53d0, offset 0x30 + area;
flags change two entries first, 1000:3419-3435: area 0xcc map 11, area 0x34 map 12). A
load into all nine areas (1000:3469, 1000:3edc) loads each grid's map in turn. None of the areas
around 0xcc has a map. The table is game state, not a constant: the raid's third story line makes area
0xcc map 11 (27e8:00fc) for the rest of the game, so a party coming back finds the ruined town,
whose north wall is broken (map 1's is whole, and blocks a 'Mech's step along the whole edge).
The rebuild keeps the table in the town (`area_maps_`); the raid writes it, and so do the
follow-ons into VIEWDISK (area 0x34 becomes map 12, the HUT; docs/scripts.md); the area loads
read it. Not rebuilt yet: the nine-map load of a saved game.

## The masks: 287f:1886 and 287f:11bb

`287f:11bb` gives each block of a grid a 4-bit mask of the neighbours with the same entry: north
1, east 2, south 4, west 8. A block's neighbour across the grid's edge is in the next grid. A map
block's mask is instead its number in the store (entry - 0x80), except that entry 0x80 on a
grid's top or bottom row (not a corner) is compared like a terrain type.

The grids sit one after another in memory, and 11bb finds a neighbour across an edge by a fixed
offset: west of column 0 is 0x39 bytes back (column 7 of the grid before), north of row 0 is
0x88 bytes back (row 7 of the grid three before). For the edges of the 3 x 3 grids as a whole
these land in other memory (the masks, the cell window). The window only uses the blocks next to
grid 4 (below), whose neighbours are all real; the rebuild compares the other edges' neighbours as
different.

## The window: 287f:1314 and 287f:13d9

`287f:1314(x, y)` fills the cell window with the 3 x 3 blocks around the block holding the
position (its block column `(x & 0xff) >> 4`, row `(y & 0xff) >> 4`), one `287f:13d9` call each,
and keeps each call's result (mask OR entry) at P:07a4. A block one past grid 4's edge is taken
from the next grid. The map drawer (287f:18ef) and the figures read the window, not the map, so
the cells past a map's edge are terrain.

13d9 copies one block into the window:

- **A map block** (entry 0x80 or more): its 64 bytes from the store, as they are.
- **Type 0x10**: `tables.plain_block`, whatever the mask.
- **Types 0x00, 0x20, 0x30, 0x40 and up**: the template for its mask, each template byte
  below 0x40 turned into `(byte & 0x0f) + offset`, the offset 0x00, 0x10, 0x20, 0x30 for those
  types; bytes of 0x40 or more stay as they are. The template is turned by the block's place,
  `(row + column) & 3` in the area: 0 as it is, 1 upside down, 2 mirrored, 3 both. A turned
  block uses the template of the turned mask, and its bytes below 0x40 are turned too, through
  `tables.mirror_north_south` (upside down) and `tables.mirror_east_west` (mirrored). It is
  built in P:20dd and copied from there.

The rebuild's `Terrain::window` is checked against the original's memory: every rig case takes
the window where the original last built its own (P:02cf, 02d1), the one chr holds if it was
built there (`chr --window`), and compares the nine values and the 576 cells (`check.py`,
`window.py`), and the memory after the window from P:09f3 to the block store (the queue, the
noise cursor, the noise table and the heights). Everything that reads the window in the original reads
it in the rebuild: the map view, the party's and the 'Mechs' water cells, the townspeople's steps
and the fight's paths, lines of sight and figure cells (`window_cell`, `Terrain::cell`). The
rebuild builds it for the place it is asked about, once per block, rather than at the original's
moments; the original builds it at every place those readers use (combat.md 1.2.1). Indices
before the window read the block values, the grids and the masks, as in the original's memory
(combat.md, "An index outside the window").

A reader that indexes past the window's 576 cells reads on into the memory after it, in the
order of the table at the top: the corner words, the queue, the noise cursor, the noise table, 16
zero bytes, the heights, 18 zero bytes and the block store (the templates, then the map's
blocks). The rebuild gives the same bytes (`Terrain::window_cell`). The corner index is the one
for the window's place; P:09ef and 09f1 are 0 until the party's first step, then the ones for the
window's place. In the original they are wherever `1df8` last ran, which is not always there, so
the rig leaves those two words out.

13d9 also works out a second value from the block's place, `((row + column) << 2) & 0x10`, and
loads it, but never uses it.

The step check (1000:1c12) reads P:07a4: a block of type 0 whose four neighbours are all type 0
(0x0f) is deep water. 1000:4d57 and 203b:000a read its bit 0x80, a map block (combat.md 2.15).
The step check reads its cells from the window too, so outdoors the party walks on the terrain
(docs/movement.md).

## Stepping into the next area: 287f:158c, 163b, 16e3, 17c5

Each position step (`world.h` rebuilds the position part) that takes the low byte past 0 or 0x7f
moves into the next area, and the grids with it, so grid 4 stays the party's own:

| Step | The grids move | The new grids |
|---|---|---|
| up (158c) | down a row (P:0564..06e3 to P:05a4..07a3, copied from the end) | 0, 1, 2 |
| down (163b) | up a row | 6, 7, 8 |
| left (16e3) | right a column (row by row: 1 to 2, then 0 to 1) | 0, 3, 6 |
| right (17c5) | left a column | 2, 5, 8 |

The three new grids are made from the heights at `0x0afa + area` (that is, `world[area - 0x11]`
for the top-left one) and on, as 104e makes them (18d8 copies the corner heights, 0bfb fills
the lattice). Then the masks are rebuilt (1886), and the queue P:09f3 gets the three grids and
P:09f6..09f8 their areas. The rebuild gives a step into the next area exactly the grids and masks
of making all nine around the new area (`Terrain::step_area`, the unit test
`area_step_matches_a_fresh_area`).

After each axis's move, 1000:218f works through the queue: each grid not ff whose area has a
map in the table (the copy in memory, which the save-game load changes) gets it (`2da8`, `1da8`),
and the entry becomes ff either way. A new game starts with the queue 0, 0, 0, so the first step
looks at area 0, which has no map. Rebuilt (`Terrain::queued_grids`, `Town::run_map_loads`); the
rig compares the queue in every case (below).

A step into the next area doesn't touch the cell window (P:07ad) or its block values: they stay
as the last `1314` built them, in whatever grid was 4 then, until the next `1314`. A fight's
walk to a figure in the next area and back (`1000:17bb`, docs/combat.md 2.12) shows it: the
window last built there, with the town's map blocks, is still in memory once the grids have
moved back. The rebuild holds the window across the step (`Terrain::hold_window`).

`1000:17bb` doesn't work through the queue; it is left for the next `218f`, whose loop takes
the entries that are not ff, so a fight's walks can leave a map load for the party's next step.
Rebuilt that way; not met yet, since no area next to the ones walked so far has a map.

## The seen map (1000:010b)

F:cb0c-d30b, 0x800 bytes, is a bitmap of where the party has been: for each area row (y bits
12-15) 0x80 bytes, in 8 rows of 16 bytes (y bits 4-6), one byte per area column (x bits 8-11).
It sits between the 'Mech records and the flags, so a saved game holds it (docs/formats/save.md).

After each key (not on a pass without one), the main loop (`1000:010b..01af`, in Ghidra
FUN_1000_00bb) marks the party's place, after the command menu and before the tick:

- The byte is `0x80 (y >> 12) + 0x10 ((y >> 4) & 7) + (x >> 8)`; it gets the bits of the
  table DG:029a for x's bits 4-6 (`tables.seen_bits`, three bits wide, so neighbours overlap).
- The byte a row above gets the same bits unless y is 0, and the byte a row below unless y is
  0xf07f or more.
- With flag 31 (F:d33d, the terrain mapper FROB sells) set and flag 3a (the tile-event map)
  clear, instead the eight bytes of the party's column in its area (`0x80 (y >> 12) + (x >> 8)`
  and the 7 rows below it) become ff.

x's bit 7 and y's bits 7-11 take no part, so places a half area apart share a bit.

Who else writes it: the new game ORs 0x1f into rows 2-7 of area 0xcc's byte (`1000:4ec5`, so the
start's town shows as seen) and clears nothing, so a second game keeps the first one's seen map;
going into the cache clears area 0xcc's column (`1000:7f46`, docs/cache.md). The only reader is
the map viewer's drawer (`1000:438e`, in 1000:3fae).

Two edges reach outside it: in area row 0, with y 1-0x0f, the row above is F:cafc + x >> 8, the
end of 'Mech record 7; in area row 15, with y 0xf070-0xf07e, the row below is the flags. The
rebuild skips such a write and logs it.

Rebuilt (`Town::mark_seen`); the rig compares all 0x800 bytes in every case (`state seen`).

## Not rebuilt yet

- The nine-map load (1000:3469, 1000:3edc), the two entries the save-game load changes,
  1000:42c8, and 24d3:12db's `104e(0x89)`.

What has been read of these so far:

- **The area-to-map table** is a plain table in the program image, 37e8:0030, 256 bytes (one map
  number per area, 0 for none), imported as `tables.area_maps`. The save-game load (1000:32b3) changes two entries first, in
  memory: area 0xcc becomes map 11 when F:d310 is set, area 0x34 map 12 when F:d33e is set. Area
  0x7d is map 3.
- **The nine-map load** (1000:32b3 at 3469, 1000:3d40 at 3edc): `104e(area)`, then for each
  grid k whose area `area - 0x11 + 16 (k / 3) + k % 3` lies in 0..255 and has a map,
  `2da8(k, map)`; then `1da8`. Unlike 104e, the index is bounded. Both skip it when F:d346 is set
  (another path, 0b5d:0004 / 1b5d:0327).
- **1000:3d40** is the overhead map, moved with the arrow keys two areas at a time, drawn by
  **1000:3fae**: three rows of five areas, each made with 104e and the steps right, its maps
  laid over it and drawn small. On leaving it the nine-map load runs at the party's position,
  placing no townspeople. Rebuilt; docs/overhead_map.md.
- **The step up (287f:158c)**, at an area's north edge: y becomes the area above's row 0x7f,
  the grids move down a row (P:0564..06e3 to P:05a4..07a3, 0x180 bytes), the top row's three
  grids are made with 18d8 and 0bfb (heights at `0x0afa + area`, that is `world[area - 0x11]`),
  and the masks rebuilt (1886). It then sets P:09f3 = 0x100, P:09f5 = 2 and P:09f6..09f8 to the
  new row's three areas: a queue of map loads. 1000:218f works through it after each axis's move:
  for each queued grid (P:09f3..09f5, ff = none) whose area has a map, `2da8(grid, map)` and
  `1da8`; the entry is then set to ff. 218f ends by building the window (`1314`) and its corner
  (`1df8`) at the new position. The
  other three steps (163b down, 16e3 left, 17c5 right) are alike. 1000:17bb and 1000:1817 call
  them to walk the position to a place or by a distance; 1000:186f moves the position without
  them.
