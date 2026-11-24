# .MTP: maps

`MAP1.MTP` to `MAP15.MTP`. Maps 1–14 share one layout (the loader is
1000:2da8, which opens `MAP<n>.MTP` for the map number it is given). Map 15 is
different and is loaded elsewhere (see the end).

| Size | Maps | Cells |
|---|---|---|
| 4,637 | 1, 2, 11, 14 | 64x64 |
| 1,565 | 3–10 | 32x32 |
| 605 | 12, 13 | 8x8 |
| 768 | 15 | another layout |

## Layout (maps 1–14)

The loader reads the fields one after another:

| Offset | Size | Where it goes | What |
|---|---|---|---|
| 0x000 | 1 | a local the loader never uses | unknown. 0 in every map. |
| 0x001 | 1 | | block column where the map starts in its area (0 in every map) |
| 0x002 | 1 | | block row where the map starts (0 in every map) |
| 0x003 | 1 | | width in cells (a multiple of 8) |
| 0x004 | 1 | | height in cells (a multiple of 8) |
| 0x005 | 0x80 | 2c6c:a461 | 8 people's names, 16 bytes each, NUL-ended: the townspeople (docs/townspeople.md) |
| 0x085 | 0x100 | 2c6c:a561 | 16 place names, 16 bytes each, one per entrance below (in MAP1, the town's eight places in the order of the .BLD table's places 0–7; the rest are leftovers) |
| 0x185 | 0x20 | 3892:4564 | 16 entrance x positions (words) |
| 0x1a5 | 0x20 | 3892:4596 | 16 entrance y positions (words) |
| 0x1c5 | 0x20 | 3892:39b4 | 16 x positions of a second table, one per entrance (in MAP1 the same values). 19b8:0d58 puts the party here after a recruit (docs/scripts.md); 1e31:03ab also reads it, not read yet. |
| 0x1e5 | 0x20 | 3892:39d4 | 16 y positions of the second table |
| 0x205 | 0x10 | 3892:4602 | the place script behind each entrance: a number in the .BLD table (formats/bld.md). Entering place e runs `table[e]`, except e = 0x12 and 0x16 and up, which are used as they are (17dc:0008). MAP1: 0–7, then 4 2 7 5 6 1 3 0. |
| 0x215 | 8 | 3892:3768 | 8 bytes, a shuffle: the townspeople's next entrance after entrance n is `table[n & 7]`; a new game places each at `table[random & 7]` (docs/townspeople.md). |
| 0x21d | width × height | 2c6c:101d | the cells, one byte each, **in 8x8 blocks** |

### The cells

The cells are stored as 64-byte blocks of 8x8 cells. Within a block they run
row by row; the blocks follow left to right, then top to bottom. Cell (x, y)
is at

    ((y / 8) * (width / 8) + x / 8) * 64 + (y % 8) * 8 + x % 8

**Each byte is a tile number** into the map's tile sheet (see icn.md; every
shipped map uses tiles 0–249 only). Cells carry no other bits. Everything the
game knows about a cell comes from its tile number:

- **Blocking.** 1000:1c12, the step check, refuses a step onto any tile
  numbered DG:0150 or more. That limit is 0x55 normally and 0x21 on map 14
  (1000:2da8 sets it).
- **Places.** A step that lands exactly on an entrance position (one of the
  first 12 entries) names the place from the 0x100 table and offers to go in
  (17dc:0008).
- **Special tiles.** With map 14 loaded, a number of tile values call their
  own handlers in segment 1b5d, for example 0x28 opens the `MAP.ICN`/`MAP15`
  screen and 0x7e–0x80, 0xb6/0xb7 and 0xf5 and up have their own. Those are
  not read yet.

### Positions

A position is a pair of words. The low 7 bits count **half-cells** (8 pixels
on the original screen) across a 64x64-cell area. Bits 8–11 of x and bits
12–15 of y seem to give the area in the world. The step code masks with 0x0f7f
and 0xf07f, but nothing more is read yet. The party moves half a cell at a
time.

## How the program uses the cells: the block world

The map is not drawn from the file directly. The world is a set of areas, each
a grid of 8x8 blocks (64x64 cells). The program keeps nine area grids at
P:0564 (pointers at DG:0170), with area 4 in the middle.

- An area grid entry of 0x80 or more is a **fixed block**: entry − 0x80 indexes
  the block store at 2c6c:0c1d, 64 bytes per block. The `.MTP` cells are read
  into the store from block 16 on (2c6c:101d), and 1000:2da8 writes 0x90,
  0x91, … into the area grid, from (block column, block row) across the map.
  - Quirk: for the first row the loop also starts at the block column, which
    would misplace the row if a map didn't start at column 0. None does.
- An entry under 0x80 is a **terrain type**. 287f:11bb compares it with its
  neighbours and picks one of 16 transition blocks by a 4-bit mask (west 8,
  south 4, east 2, north 1). 287f:13d9 then shifts the tile numbers under
  0x40 into the terrain's set of 16, and may mirror the block. The wilderness
  is built this way. It is not read in detail yet.

The town maps fill their area with fixed blocks, so what you see on them is
the file's cells exactly.

## The view

287f:1314 copies the 3x3 blocks around the party into a 24x24-cell window, and
287f:18ef draws it. The view is 13 whole tiles across and 12 down, plus one
half tile on one edge of each. That is 216x200 pixels at the right of the
screen (it starts 104 pixels in), with the party's half-cell 12 half-cells
from its top-left corner. So the view's top-left corner is at half-cell
(x − 12, y − 12):

- x odd: the left column is the right half of its tile, then 13 whole tiles;
- x even: 13 whole tiles, then the left half of the next;
- y odd: the top row is the bottom half of its tiles, then 12 whole rows;
- y even: 12 whole rows, then the top half of the next.

## Where the game starts

New-game setup (1000:4dc7) loads **map 1** into area 4 and puts the party at
**x 0x0c45, y 0xc019**: half-cell (69, 25), so between cells 34 and 35
across, cell 12 and a half down. That x is the Citadel's entrance (MAP1
entrance 1, at y 0xc014). The party starts five half-cells (two and a half
cells) south of the Citadel door. The view's top-left corner is then
half-cell (57, 13).

We haven't yet compared this against the original running in an emulator.

## Map 15

`MAP15.MTP` (768 bytes) has no header of this kind. 1b5d:055a loads it with `MAP.ICN`
and reads it into the block store at 2c6c:101d. It
is not read further yet. The reader rejects it.

## Cross-checks

- **Elthial's CrescentHawksTools (MIT).** It agrees on the header size (0x21d
  bytes before the cells, its `StandardHeaderLength`), 16x16 tiles, 8x8 blocks
  and the tile sheet per map. It treats the metadata as unresolved; the
  layout above comes from the loader. It **disagrees** on cell order: it
  gives maps 3–10 a different order from maps 1, 2 and 11–14. The program
  has one loader and one block layout for all of them, and MAP3 drawn in
  block order shows clean roads and buildings, while row order breaks them
  into pieces. Its doc says "a `0x1D` header"; its code says 0x21D.
- **velteyn/UnBattletech and waynepiekarski/battletech-maps (both GPL-3.0)**
  were not consulted for this.
