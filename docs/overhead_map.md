# The overhead map

The command menu's item 7 (docs/town_loop.md, "The command menu") shows the land around the
party as a small map, 5 areas across and 3 down, each area 8 x 8 small tiles of 8 pixels. The
arrow keys move it two areas at a time; Space leaves it. Only the patches the party has seen
show (the seen map, docs/wilderness.md). In the cache, or on a town map indoors, it shows one
screen and returns after the first key.

Rebuilt in `Town::overhead_map` (src/game/overhead.cpp). What the screen shows is handed to the
renderer as `OverheadMap` (src/game/overhead.h): tiles by ID, the town blocks as their cells,
the text and the dot. The original look is in `platform/overhead_layer.cpp`.

## The loop (1000:3d40)

1. DG:014c = 1: map loads skip the townspeople (below).
2. The position (P:a44b, P:a44d) is kept twice: as the party's place, and as the cursor.
3. In the cache (flag 3a, F:d346), `1b5d:0327` swaps in the cache's cell colours (below).
4. It is done after one pass if flag 3a is set or flag 04 (F:d310, outdoors) is clear; in the
   cache it also clears bit 7 of F:d178, a byte of the seen map (area row 12, its row 6, area
   column 12: the first small tile of that row). That change stays: it is saved with the game.
5. Each pass: `3fae(party x, party y)` draws the map at the cursor and returns a key, which
   `2656:0d1d` turns into a command key. Space ends it. Any other key: the position is set to
   the cursor, then each of four tests is applied in turn, so a diagonal key moves on both axes:
   - up (Up, PgUp, Home: keys -0x48, -0x49, -0x47, minus their scan codes): if y >= 0x2000,
     y -= 0x2000;
   - down (Down, PgDn, End): if y < 0xe000, y += 0x2000 (the high byte + 0x20);
   - left (Left, Home, End): if x >= 0x300, x -= 0x200;
   - right (Right, PgUp, PgDn): if x < 0xd00, x += 0x200 (the high byte + 2).
   The result is the new cursor. When the loop is done after one pass, the key still moves the
   cursor, which nothing then uses.
6. Leaving: the position is the party's again. Unless in the cache, the nine-map load runs
   there (docs/wilderness.md: `104e(area)`, `2da8(k, map)` for each of the nine areas, bounded
   to 0..255, that has a map), the map-load queue (P:09f3..09f5) is set to ff and the masks are
   rebuilt (`1da8`). Then, in every case: the window is built (`287f:1314`), the map and
   figures drawn (`287f:18ef`, `1000:051b`), the page's map columns copied back over the
   windows (`273d:06c3`), the panel drawn and its party box cleared (`1000:4cac(1)`), and the
   current window cleared (`2656:0388`). DG:014c = 0. In the cache the colours are swapped back.

**The townspeople skip.** `1000:2da8` with DG:014c set returns after numbering the map's
blocks in its grid: no townspeople placed, no random numbers drawn, and their figures (16-23)
left where they were. Everything before that (the map's file, tile sheet, blocking tile, the
map 2 timer call) is as in any load. So the nine-map load on leaving draws no random numbers.

## The drawing (1000:3fae)

The screen is cleared (`287f:1fbe`). The view's first area column is the cursor's minus 2,
held to 0..11; its first area row is the cursor's minus 1, held to 0..13 (kept as row x 16,
0..0xd0). The view buffer (P:244b) is 40 x 24 bytes, one per small tile.

**Outside the cache**, for each of the view's three area rows: the position is set to the row's
first area (column `col0`, its row, cell 0) and its nine grids made (`287f:104e`). Then for
each of its five areas, left to right:

1. For each of the nine grids k (3 rows of 3), whose area is `area - 0x11 + 16 (k / 3) + k % 3`
   (not wrapped at the world's edge; in 0..255) and which has a map (the area-to-map table): the
   map's blocks are written into the grid, at the block column and row DG:0a38 and DG:0a46 give
   (by map - 1), DG:0a54 blocks across and DG:0a62 down. The middle grid (k = 4) gets a running
   number, 0x90 at the start of the drawing and one more for each block, across all fifteen
   areas. The other grids get the running number as it stands, not counted on, and only in
   entries below 0x90: entries that already name a block keep it.
2. For the middle grid's map, `MAP<n>.MTP` is opened (DG:09ee, the number, DG:09f2) and read
   twice to the same place in the view's block store (P:644b + an offset that starts at 0): its
   header (0x21d bytes), then its cells, DG:0a70 (by map - 1) bytes, over the header. The offset
   then moves on by DG:0a70. The disk checks (`1000:28cc`, `1000:2913`) are the original's own
   and the rebuild has none.
3. If any map was written, the masks are rebuilt (`287f:1da8`).
4. `287f:1f04` copies the middle grid into the view buffer at (row x 8, column x 8): an entry of
   0x90 or more as it is; 0x10 becomes 0x40; any other entry, less 0x10 if above 0x1f, ORed with
   its mask.
5. The position's x gets 0x7f ORed in and the party's step right (`287f:17c5`) moves the grids
   over by an area, making the next area's column. After the fifth area that step makes a
   column past the view, which the next row's `104e` replaces.

**In the cache** none of that runs: the block store (P:101d, 0x1080 bytes: its 64 blocks as the
level left them, docs/cache.md, and the two after them, `tables.store_tail`) is copied to
P:644b, the view buffer filled with `F:d34e - 0x30` (flag 42, the level: 0xd0 or 0xd1, the
store's blocks 0x40 and 0x41), and the middle grid copied in at (8, 16) by `1f04`.

**The small tiles.** For each of the 24 x 40 bytes of the view buffer, the seen map is tested:
the byte at `rowbase x 8 + col0` plus 16 for each row and one for each 8 columns, bit
`0x80 >> (column & 7)` (each area is 8 rows of one byte). A tile not seen stays black. A seen
byte v below 0x90 is TINYLAND.CMP's piece v (32 bytes each from the far pointer DG:4588, set at
start-up by `1527:0044`, which cuts 0x42 pieces from the picture's top-left with `2656:0ae5`).
A byte of 0x90 or more is built by `1000:45c2` and `287f:1f51`: the store's block v - 0x90
(64 cells at P:644b + 64 (v - 0x90)), each cell one pixel, coloured by the cell colour table
(P:215d, 250 entries of 0-15, by tile number). `287f:275c` draws each tile at its column and row.

**The cache colours.** `1b5d:0327` swaps the 250 bytes at P:215d with those at P:2257 (on
level 0) or P:2351 (on level 1). It runs before and after the viewer, so while it shows the
level's table is in use. The rebuild picks the table instead of swapping.

**The pointer.** Unless in the cache, if flag 2f (F:d33b) is set and the cursor's area is not
0x8a: the direction from the cursor to 0a38,8038 (`287f:0971`) gives glyph `direction + 1` of
the font, written at column DG:0a8a[direction], row DG:0a92[direction] in colour 15 on 0
(`273d:00d5`). Those places ring the view's middle area. Nothing in the program image sets
flag 2f; a script must (by its index).

**The caption.** Row 24 is filled black (`273d:01fb`), and DG:09f7 is written at its column 0
in colour 15 on 0 if outdoors (flag 04) and not in the cache, else DG:0a16 (text IDs
`overhead.caption_outdoors` and `overhead.caption`).

**The dot.** The party is a 2 x 2 dot at x = `((party x & 0x7f) >> 1) + 64 (its area column -
col0)`, y = `((party y & 0x7f) >> 1) + 64 (its area row - the view's first row)`, from the
position passed in, not the cursor. If it falls on the map (x 0..0x13f, y 0..0xbf; then held
to x <= 0x13e, y <= 0xbf), each pass of the wait draws it in the colour of a fresh random
number's low four bits (`287f:0bc0`), then asks whether a key is waiting (`273d:002f`). When the
dot is off the map no number is drawn. A key ends the wait: it is read (`273d:0259`), the rest
of the buffer flushed (`1000:2a2b`), and returned. While a demo plays (F:3938), the wait instead
counts 600 frames (`273d:0006(1)`); the rebuild plays no demo.

## Original bugs and quirks

- **Maps 12 and 13 move the block store on too far** (combat.md 6, row 39). DG:0a70 gives 512
  bytes for maps 12 and 13, which DG:0a54 and DG:0a62 make one block (64 cells), and their files
  hold only that one block's cells. The cells cover only the first 64 bytes of the header just
  read there, so the rest of the 512 is the header's bytes 64-511, and the offset moves on by
  512 while the running number moves on by 1. A map drawn after one of them in the same view
  has its block n read 448 bytes early: for n < 7 from that header, for n >= 7 from its own
  block n - 7; its last 7 blocks are never shown. Reached: map 13 (area 0x66) and map 2 (area
  0x8a) both show with the view's first area column 6, rows 6-8. The rebuild keeps the shift.
  Only a map file shorter than its read would leave store bytes no read wrote (memory the
  rebuild does not hold); those are drawn as 0 with a log line. `fix_overhead_store` moves the
  offset by the map's blocks instead.
- A byte below 0x90 of 0x42 or more would be past TINYLAND's pieces. The terrain's entries
  never give one; the rebuild logs it if one comes.

## The rebuild

`Town::overhead_map` follows 1000:3d40; `Town::draw_overhead` follows 1000:3fae up to the wait,
and the wait is a loop on `KeySource::poll` (a pass with no key draws the dot's number), then
`waiting_key`. The keys running out inside it leave the viewer showing (`place_open`,
`poll_open`).

The logic hands over (`Town::overhead`, while it shows):

- `tiles`: 40 x 24 entries, row by row: hidden, a TINYLAND piece (asset
  `{"TINYLAND.CMP:8x8", n}`), or a town block (its index in `blocks`);
- `blocks`: each built block's 64 cell tile numbers, and `colours`: which cell colour table
  (outside, cache level 0, cache level 1). The renderer colours the cells; a makeover can draw
  the blocks from the tile numbers any way it likes;
- the text cells (caption and pointer, roles `Text` and `MapPointer`);
- the dot: its place in the 320 x 200 layout and the pass's random number (0-15), which the
  original look uses as the colour number.

The tables (tables.json): `overhead_block_x` (DG:0a38), `overhead_block_y` (DG:0a46),
`overhead_blocks_across` (DG:0a54), `overhead_blocks_down` (DG:0a62), `overhead_cell_bytes`
(DG:0a70), `overhead_pointer_column` (DG:0a8a), `overhead_pointer_row` (DG:0a92), and
`overhead_colours` (2c6c:215d, three tables of 250).
