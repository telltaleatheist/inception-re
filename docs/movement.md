# Walking on a map

How the party walks around a town or area map, from `1000:218f` (the walking keys) and
`1000:1c12` (the step check). Rebuilt in `src/game/movement.{h,cpp}`, driven turn by turn by
`src/game/town.{h,cpp}`.

## Positions

A position is two 16-bit words (docs/formats/mtp.md). The low 7 bits of each count half-cells
(8 pixels) inside a 64x64-cell area. The x word's high byte is the area's column (0-15); the y
word's high byte is the area's row times 16. The new game starts at x 0x0c45, y 0xc019: half-cell
(69, 25) of area column 12, row 12, just south of the Citadel on map 1.

The view's top-left corner is 12 half-cells up and left of the position, so the party stands near
the middle of the 27x25 half-cell view and the view scrolls with every step.

## Keys

The program's key reader hands back a character, or minus the scan code for keys without one.
`1000:218f` turns them into a step of one half-cell on each axis it names:

| Key | Scan code | Step |
|---|---|---|
| Up | 0x48 | (0, -1) |
| Down | 0x50 | (0, +1) |
| Left | 0x4b | (-1, 0) |
| Right | 0x4d | (+1, 0) |
| Home | 0x47 | (-1, -1) |
| PgUp | 0x49 | (+1, -1) |
| End | 0x4f | (-1, +1) |
| PgDn | 0x51 | (+1, +1) |

(The keypad's 7, 9, 1 and 3 are Home, PgUp, End and PgDn with Num Lock off.) Every key is one
turn: the main loop (`1000:002d`) reads it, empties the keyboard buffer, selects the upper panel
box (window 4) and takes the step DG:015a times (1 at the start; the game settings change it, docs/town_loop.md). Unless a
message or question came up during the step (F:d55c), it then writes the direction's name in
that box (the compass directions' names, DG:01aa), blocked or not, so the box is a log of the last
eleven steps. The map, the figures and the panel are drawn again afterwards (`1000:01f4`), so a
message drawn over the map lasts until the next key. The keyboard and the idle half of the loop
are in docs/reference.md.

## The step check (1000:1c12)

It returns "blocked" or "free". In order:

1. **Block edges.** If the step crosses into the next 8x8-cell block (16 half-cells) on either
   axis, the program looks at the block it would enter (the diagonal one if both axes cross),
   using the window's nine block values (P:07a4, mask OR entry; DG:04a0 turns the edges crossed
   into the index, in the plain 3x3 order). A value of 0x0f, a terrain block of type 0 whose four
   neighbours are all type 0, is deep water: it opens the message box (`24d3:17c6`), prints the
   too-deep message (`walk.too_deep`, `24d3:17ea`, which then pauses a moment) and the step is
   blocked. A map block's value has bit 0x80 set, so this only happens outdoors.
2. **Each character.** The party is up to eight characters, each at a fixed offset from the
   position (in half-cells): (0,0), (0,-1), (0,1), (1,0), (-1,0), (1,-1), (-1,1), (1,1). A new game
   has Jason alone, at (0,0). For each present character, with its position after the step:
   - **Entrance.** Unless a question has been asked this turn already (flag F:d55c) or the party
     is in its 'Mechs, if the character would stand exactly on one of the map's first 12 entrance
     positions, the message box opens with the enter question (`walk.enter_question`, the place's
     name, `walk.enter_question_end`) and the yes/no question (`1000:1a13`, Yes picked first). Yes goes into the place
     (`17dc:0008`). The check then carries on either way.
   - **Tile.** Read first (`1000:1d9e`), before the entrance: the cell of the window under the
     character's half-cell after the step, with the same arithmetic as the 'Mechs' (below). If
     that tile number is at least DG:0150 the step is blocked. DG:0150 is 0x55 on every map but
     map 14, where it is 0x21 (`1000:2da8` sets it). Outdoors the window holds terrain, so the
     party walks on it as on the map.
   The loop runs over the active figures 4-11 (F:4072), not over the characters: `1000:051b`
   makes the figure of each character on foot active and clears the rest, and `1000:0bd8` gives
   the k-th character on foot figure 4 + k at the k-th offset above. A character in a 'Mech leaves
   no gap: with Rex (character 1) in the Commando, Edward (character 2) is figure 5 at (0,-1),
   not (0,1). The tile is read at that slot's offset; the entrance is matched at the figure's own
   position (F:400c / F:403e + 2k) plus the step, which 0bd8 set to the same place.
3. **The 'Mechs.** Only when no character was blocked. For each active 'Mech figure 0-3 (F:406a,
   also set by `051b`, for the 'Mechs of records 0-3), at its offset from the party in half-cells
   (DG:045c / 0474, `tables.mech_step_x/y`: (0,-2), (0,4), (3,1), (-3,1)) plus the step: two cells
   of the window, the one under the half-cell it is drawn on and the one beside it (right when
   that half-cell's x and the party's x have the same parity, else left); either at DG:0150 or
   above blocks. Then, unless a question was asked this turn, an entrance on the 'Mech figure's
   own row (F:4036, plus the step) at its x (F:4004, plus the step) or one either side asks the
   same question from the program's second copy of the text (DG:0446 / 045a), and Yes goes in.
   Then the block, if any. The cells are read from the cell window (P:07ad) at the corner cell
   P:09ed with the same arithmetic as the townspeople's (docs/townspeople.md), the row from the
   table DG:048c (k x 24).
4. **Map 14** (F:d346 set): blocking tiles run the tile handlers (`1b5d:*`) instead (docs/cache.md). Rebuilt (`src/game/cache.cpp`).

**After a place.** The question runs the place inside the check, and the check then goes on
reading memory as the place left it. The character's tile was read before the question, so it
is the one from before the place. After the walking course, TRAINING.BLD's special 3 puts the
party at (0x0c2e, 0xc076) and nothing has redrawn the figures yet, so the Locust's figure 0 is
still active: the 'Mech part checks it against the window in memory (at the party's position
then), and blocks the step that went in. The party stays where special 3 put it. Which window
that is depends on what ran in the place. After a fight with no step since, it is the cells the
fight's last redraw copied back (combat.md 1.2.1). After the mission's walk back to the door,
it is the one the last step built (287f:1314 rebuilds every cell each time), and in mission 4's
first try the step goes on. The rebuild reads the position, the active 'Mechs, the window and
its corner again after a place (`take_step`'s `window_at`, `Terrain::held_window`).

## Moving (1000:218f)

If the step is free, the program moves y first, then x, one half-cell each (`287f:158c` up,
`287f:163b` down, `287f:16e3` left, `287f:17c5` right). If the low byte would leave 0..0x7f the
position moves into the next area: the low byte wraps to 0x7f or 0, the high byte steps by one
area, and the program shifts its grid of loaded areas and loads the maps it now needs between
the two moves (docs/wilderness.md). At the edge of the 16x16-area world the position does not
change.

An axis the step does not move is not touched: `1000:218f` calls the up or down move only for a
y step of -1 or 1, and the left or right one only for an x step. So a place off the map stays as
it is. After a leader-fell Arena fight with the theft (`chain_arena_leader_poked`) the party is at
x 0970 and y ffff, Jason's killed figure's row. The step out of the Arena moves x to 0971 and
leaves y ffff, and no map loads.

**The window at such a place** (`287f:1314`, `287f:13d9`). The window's block row is
((y >> 1) & 0x7f) >> 3 of y's low byte, so 15 for y ffff. Each block is found at the centre
grid's start plus row x 8 (a byte) plus the column. Only row -1, row 8, column -1 and column 8
step into a neighbouring grid. Rows 14-16 therefore read the grids that follow the centre one in
memory, and the window holds whatever those blocks are. This is kept, with no fix switch
(docs/combat.md, section 6, bug 42).

## Trying it

    chr --map 1 --script "up*5 y" --screenshot out.bmp

plays the keys headless and prints what each did ("moved at 0c45,c018", "blocked at ...",
"entered place 1 (Citadel)"). Key names: up down left right home end pgup pgdn space enter, or a
single character; "*N" repeats; `idle:N` is N passes of the main loop without a key
(docs/town_loop.md). Without `--screenshot` a window opens and play continues with the
keyboard.
