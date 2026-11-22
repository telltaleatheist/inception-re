# The town's main loop

`1000:0000` starts the main loop on a map; `1000:002d` is its key half and `1000:01f4` its idle
half. Rebuilt in `src/game/town.cpp` (`Town::key`, `Town::idle_pass`, `Town::game_tick`).

## Pace: a pass is a display frame

Each pass of the loop asks DOS whether a key is waiting (`273d:002f`, int 21h function 0Bh).

- **No key** (idle half): it waits once in `273d:0006`. That is not a timer wait: it calls
  `287f:0b40` (0x29330), which waits for the vertical retrace on the VGA/EGA status port 3DAh.
  So a pass lasts one display frame: about 70 per second on a VGA card in the 320x200 modes (what
  DOSBox shows), 60 on an EGA card. Then a countdown (`[bp-1a]`, 0 when the loop starts) is
  decremented; when it goes below 0 a game tick runs. So the first pass on a map is a tick, and
  then every 11th pass.
- **A key** (key half): it reads the key, flushes the keyboard, takes the steps (each step is
  followed by `1000:051b`, which draws the figures and records where the party's characters
  stand, so the townspeople moved in the tick below see the party's new place), logs the
  direction, then calls `1000:231d` (the figures' walking frames) and goes straight on to a game
  tick, without waiting. Every key is a game tick.

The timer chip is reprogrammed, but only for the music: `284b:0048`, called after
WWOODBT.SIF is loaded into the sound driver, hooks int 8 and sets PIT channel 0 to divisor
0x0FFF (about 291 Hz). Its handler calls the sound driver's hook every interrupt and the BIOS's
own handler every 16th (so the BIOS clock still runs at 18.2 Hz). `284b:0091` restores 0xFFFF.
The main loop never reads it.

The rebuild counts passes and ticks only. The window's main loop turns real time into passes
with `game::FrameClock` (70.086 a second, a VGA card's rate); scripts (`idle:N`) and tests give
the passes directly.

## The game tick

In order (`1000:020a`..`050d`):

1. Timers that count down when set (below, "Timers"): flags 29 and 37.
2. A random number (`287f:0bc0`), always. Outdoors (F:d310 set), if it has none of the bits of
   F:d330 and F:d346 is clear, a random encounter starts (`203b:000a`, docs/combat.md 1.1).
   Rebuilt up to its "no fight" ending (`Town::random_encounter`); a fight that takes place
   stops the rebuild with a log line. Then flags 1d, 14, 15 and 16 count down.
3. The money step, every 256th tick (F:d323, flag 17, counts down; it starts at 0, so the first tick
   runs it). Below.
4. The countdown is set to 10.
5. `1000:240b`: the animated tiles' next frame. Below.
6. DG:57fe counts ticks; on every third (`57fe` was 2), `1000:24c2` moves the townspeople
   (docs/townspeople.md).
7. If DG:014a is set (it is in town): the map is drawn again (`2000:9b04`, `a0df`), the figures
   (`1000:051b`, which also records where the party's characters stand), and the screen is
   flipped (`7a93`). Then, if DG:01a8 is set (the cache's way out, docs/cache.md), the game is
   over the same way as below, but without the leader's text: the message box, DG:012f and the
   yes/no question (`1000:049d` jumps to `04e5`). Rebuilt in `Town::game_tick` (`game_won_`).
   If DG:014a is 0 (the leader was killed in a fight, docs/combat.md 2.12) nothing is drawn and
   the game is over at once (`1000:04a4`): the message box, the leader's name (DG:00f1 if
   Jason's record is empty, else DG:00f7), DG:00fb, a key; the box cleared, DG:012f and a
   yes/no question, Yes first. Yes starts a new game (`1000:4dc7`); No sets DG:0152 and the
   main loop ends. Rebuilt in `Town::game_over`; the rig compares the box, not yet the question
   (its capture after the key still shows the box). Yes is `Town::new_game` (docs/startup.md,
   "The new game"), and the loop goes on at its key half; quitting stops the rebuild's game.

## The command menu (Space)

The key half calls `1000:2c50` for key 0x20 (at `1000:0101`), after the step and the walking
frames and before the tick, which still follows. It sets window 1 to row 13, height 7, and menu 1
to 7 items; if any of 'Mech records 0-3 is in use (byte 0 not ff), one row higher, one row taller
and 8 items, and the crew line is printed among the others (DG:051f, then 0543 with 'Mechs,
055b, 0592). Window 1 is selected, cleared and framed (style 1), and menu 1 is read
(`2656:0b5e`). Its jump table (`1000:2d91`) is numbered as with the crew item; without
'Mechs, items from 2 on are one lower:

| item | what |
|---|---|
| 0 | back to the game |
| 1 | game settings (`1000:3bd0`) |
| 2 | the crew, with 'Mechs only (`1000:4d57`) |
| 3 | inspect a 'Mech or a character (`1000:378d`, below) |
| 4 | healing, `1c31:000a(0)` (docs/combat.md) |
| 5 | load a game (`1000:32b3`) |
| 6 | save a game (`1000:35d3`) |
| 7 | the overhead map (`1000:3d40`) |

`1000:4d57`: if any of the window's nine block values (P:07a4) has bit 0x80 (a map block
near the party), the message box with DG:0ace and a key. Otherwise the crew screen with
argument 0 (docs/scripts.md), the panel (`1000:4cac(1)`) and window 1 cleared. After any item
`273d:06c3` copies the page's map columns back over the windows.

Rebuilt: all eight items (`Town::command_menu`, `Town::game_settings` and `Town::inspect` below,
`Town::allocate_crew`, `Town::load_game`, `Town::save_game`, docs/formats/save.md;
`Town::overhead_map`, docs/overhead_map.md). Item 3's 'Mech stats screen (`15ab:1afe`) is
logged.

### Inspect a 'Mech or a character (`1000:378d`)

Window 6 is selected, cleared and framed (style 0). `1000:2867(text, col, row)` puts the
window's cursor at (col, row) and prints in white (F:37fe = 15); the page below uses it for every
value.

1. Special 17 (flag 10 = 'Mech records 0-3 in use). With any: `inspect.mech_or_person` and the
   Yes/No question (`1000:1a13(1)`, Yes first). Yes: the window cleared,
   `inspect.which_mech`, the 'Mech chooser (`1c67:0b98`, docs/scripts.md) into DG:0068, that
   record's heat (F:006e + m) = 0, the stats screen (`15ab:1afe(m)`, not rebuilt), the panel
   (`1000:4cac(1)`) and the window cleared; done, with no key.
2. Otherwise (no 'Mechs, or No): special 14 (flag 0e = members besides Jason). With any: the
   window cleared, `inspect.which_character` and special 13 (the menu of present characters;
   flag 0e = the chosen record). With none flag 0e is 0, Jason.
3. The page, for character c = flag 0e. The window cleared; in light green (`1000:28a2`)
   `inspect.labels` and `inspect.labels_2`, which lay out every label: name, weapon and armour
   on rows 0-2, health on row 4, the skills' heading on row 11 and their seven names on rows
   13-19, the MedKit and mapper labels on row 21 and the field surgery kit's on row 22. Then in
   white: the name (DG:01ca by byte 0) at (8, 0), the weapon's name (the weapon table, byte
   0x0b) at (8, 1), the armour type's name (DG:4e8a, byte 0x0d) at (8, 2).
4. The window's left edge moved 8 columns right (F:39a0 + 8, width F:3990 - 8): the name again
   at (0, 4), then the condition (`1e31:02e4`), so it wraps under the name; the edge back.
5. If flag 27 is set and flag 25 is c (the double agent): `inspect.uneasy` at (0, 8) in yellow
   (colour 14), white again, the message pause (`273d:086a`) and F:374a = 1, as the crew
   screen's tell and the scan list do (docs/combat.md 2.9). Otherwise, with an armour type
   (byte 0x0d not 0): `inspect.armour` at (0, 8), then `inspect.armour_whole` when the points
   (byte 0x0e) equal the type's full points (DG:4ddb), else `inspect.armour_lost`, then
   `inspect.armour_all` with 0 points or the points lost (full - left) in light green,
   white, `inspect.armour_of`, the full points and `inspect.armour_points`.
6. The seven skills (bytes 4-0x0a), each capped at 4, as words (DG:0194: the
   `inspect.skill_level.N` texts) at column 12, rows 13-19.
7. The kits: F:d450 (the MedKit) at (8, 21), flag 31 (F:d33d, the mapper) at (20, 21), F:d451
   (the field surgery kit) at (19, 22), each `..._yes` or `..._no`.
8. The message pause and a key (`273d:086a`, `273d:0259`).

The window stays selected and is not cleared; the tick that follows draws the map back over it
(`273d:06c3`).

### Game settings (`1000:3bd0`)

In window 1, which the command menu left selected: the window cleared (`2656:0388`), DG:0896,
then DG:08c9 if the word DG:015c is not 0, else DG:08cc (the sound's state; the word takes in
DG:015d, which is 0 and which nothing in the game segments writes), then DG:08ce, and menu 0x21
(six items). The menu records are at 385b:0092, 16 bytes each (docs/formats/tables.md); the
function writes into menu 0x27's record directly.

| item | what |
|---|---|
| 0 | Steps a key (DG:015a). The window cleared and DG:08f9. Menu 0x27's item count (+4) = 3, its first row (+0) = 1, its last item (+6) = DG:015a - 1, one less again when DG:015a is 4. After menu 0x27, DG:015a = item + 1, and 4 in place of 3; first row back to 0 (the count stays 3). Then if F:d34e is set (the cache's second level) DG:015a = 1. No panel redraw. |
| 1 | Message speed. The window cleared, DG:092f, menu 0x22 (six items); DG:015e = the item. Then the panel (`1000:4cac(1)`). |
| 2 | Sound: DG:015c's low byte flipped (`1000:3ce4`). Nothing drawn. |
| 3 | Pictures. The window cleared, DG:095d, menu 0x26 (three items, its last item at M:02f8, which the load sets from flag 4f); F:d35b (flag 4f) = the item. Then the panel. |
| 4 | Quit. The window cleared, DG:0993, DG:09cf, the yes/no question with No first (`1000:1a13(0)`); DG:0152 = the answer, and Yes ends the main loop. |
| 5 | back |

Only this function uses menus 0x21, 0x22, 0x26 and 0x27. None of DG:015a, DG:015c and DG:015e is
in the saved game; all three keep their values through a load and a new game. Rebuilt in
`Town::game_settings`.

### Steps a key

The key half takes the step DG:015a times (`1000:0069..00a0`): each time the step (`1000:218f`,
skipped for Space) and the figures (`1000:051b`); if F:d55c is then set (a message or question
came up), it stops. The direction's name is written once, after the last step, when F:d55c is
0. DG:015a is 1 in the program image; menu item 0 above changes it, and the cache's way down
(`1b5d:055a`, at linear 0x1bbde) sets it back to 1. Rebuilt in `Town::turn` (`steps_per_key_`).
A rider ('Mech byte 0x7a) is not on foot, so soldiers can't shoot at him; the repairs still pick the
best tech among all the party's characters (docs/combat.md 2.15).

## Timers

These bytes are script flags (docs/scripts.md: flag f is F:d30c + f), so the places' scripts
can set them. All are 0 at a new game. Addresses marked linear are byte offsets in the unpacked
image (load segment 0x1000). Rebuilt in `Town::game_tick` (src/game/town.cpp), in the original's
order.

| Flag | Byte | Each tick | Set by | Read by |
|---|---|---|---|---|
| 29 | d335 | down to 0 | 0x3f at 1c31:02d3 (after a panel redraw), 0 at linear 0x2160a | 1c31:001b returns at once while it is not 0 |
| 37-39 | d343-d345 | while d343 is set, the 16-bit count d345:d344 goes down; d343 becomes "count not 0". At 0, if the party is within x 0x800-0xcff and y 0x6000-0xafff, `1d43:0c72(1)` | only the tick writes d343 (others by index) | 2da8: loading map 2 with d343 set calls `1d43:0c72(0)` |
| 1d | d329 | down to 0 | by index | by index |
| 14 | d320 | down to 0 | 0xff at linear 0x1d1f4 | 17dc:0008: the training center's first-visit routine (1c67:08a8) runs only while it is 0 |
| 15, 16 | d321, d322 | down to 0 | by index (CITADEL sets 16 to ff) | by index |

The first three are counted down before the tick's random number, the last four after it
(`1000:0213`..`0286`, then `02b6`..`02ed`).

## Money

C-Bills are F:d370 (32-bit). Three more 32-bit amounts at F:d374, d378, d37c behave like
investments. A new game (`1000:4e89`, `4e9c`) sets C-Bills to 20 and the three to 0; the floor,
the word made of F:d340 (high) and d33f (low) and read by `1000:29f5`, is 50 (`1000:4f70`).
That word is flags 33-34, which the training center's script raises.

The money step:

- The total is the C-Bills plus the three amounts.
- In town (d310 clear): if the floor is above the total, 15 C-Bills are added. Either way the
  C-Bills line is drawn again (`1e31:1fdf`).
- Outdoors, or if the floor was above the total: for each of the three, a random number; if it
  has any of the bits 0xff, 0x07, 0x01 (DG:02a2) the amount becomes amount x 100 / rate,
  otherwise amount x rate / 110, with the rates 96, 90, 48 (DG:02a8). 32-bit arithmetic,
  truncating. After a gain the third is cut to a quarter when it is over 18,000 (an unsigned
  32-bit shift).

So a new game in town has 35 C-Bills after the first tick and 50 after the 257th, and no more
come that way. Checked against the original by `check.py` (`money_step`).

`1e31:1fdf`, the C-Bills line: in window 3, cursor column 0 row 9, it prints the string at
DG:32da (colour code 15, "C-Bills:", a line break), sets the colour to 10 (`1000:28a2`; 1 on
CGA), prints the amount in decimal padded with spaces to 10 characters, sets 15 again, and
selects the window that was current before.

## Animated tiles

`1000:320b` loads ANIMATE.ICN into the tile slots and copies 30 of its tiles (from tile 1) to a
buffer of its own. `1000:240b` runs only when the word at 3892:3988 is 0: loading BTTLTECH.ICN
sets it to 0, map 11's tiles set 1 and map 14's 2. It moves DG:5800 on (0, 1, 2, 0, ...;
0 at start-up) and copies, for each of the 10 tile numbers at DG:04b0 (57 58 59 5A 5B 69 6A 6B
6F 71), ANIMATE.ICN tile 1 + 3i + frame over that tile's slot. Rebuilt as a lookup in
`Town::view()` (`game/tile_animation.h`): the cells keep their tile numbers and the view names
the frame's picture.

## Random numbers

The generator (`287f:0bc0`), its seeds and every place that draws are in docs/random.md.

In a new game one more number is drawn between the map load (the townspeople's placing) and
the main loop's first pass: the main loop (`1000:0000`) starts by calling `1000:48b7(0x0f, 0)`
(at `1000:0024`), whose first act is a draw (docs/scripts.md, "The picture"). The start-up's
load (docs/startup.md) comes before it, so a game loaded there places its townspeople one
number earlier than it would after the draw. A new game from the game-over question goes on in
the loop already running, without it.

## The panel

`1000:4cac` draws the party box (window 3): for each of the first four characters present,
`1000:4aa6` (row 14, 16, 18, 20), then the C-Bills line, then window 4's frame.

`1000:4aa6` prints the name and draws three bars at columns 9, 10, 11 of the name's row, with
the character's bytes 1, 2 and 3 (its 17-byte record at F:c614; Jason: 8, 9, 7). Over the
first it draws a red part (colour 4; 2 on CGA), from the bar's top down, the bar's number less a
tenth of byte 15 (at least 1) pixels tall, unless that is 0 (Jason's byte 15 is 80: 8 - 8 = 0).

`1000:4bc1` draws a bar at column c, pixel row y (all inclusive, x = 8c): yellow lines (14; 3 on
CGA, `273d:031c`) along the top (x+1..x+5, y), the sides (x and x+6, y+1..y+13) and the bottom
(x+1..x+5, y+14), and a green box (10; 1 on CGA, `273d:01fb`) from (x+2, y+14-value) to (x+4,
y+13).

In the rebuild the logic gives each bar in game terms (`StatBar`: the box's line, which of the
three numbers, the number, and the part the wounds take); the renderer places and draws it, the
original look as above (`platform/panel_layer.cpp`).

## The townspeople

docs/townspeople.md.

## The party's figures

`1000:051b` draws the map's marks first (`1000:2a93`: each of the 64 in view, at its place as a
person is placed, its code the sprite; a fire, 0x7c or 0x7d, changes to the other each time it is
drawn), then the
party. For each of the 8 characters present (byte 0 of its record not 0xff, byte 12 at least
8), the k-th one drawn goes at the k-th formation place: screen (DG:02ae[k], DG:02be[k]),
which is the party's half-cell plus the formation offset (docs/movement.md) plus one half-cell
right. Its sprite slot is its base (F:d562; Jason's is 0x96, from the new game at `1000:4f0f`)
plus its walking frame (F:409a). Only Jason's base is known so far; the others are set when
characters join the party (not rebuilt yet). Then the townspeople (0x10-0x17,
docs/townspeople.md) and the 'Mechs (docs/combat.md 1.2.1): each party 'Mech record 0-3 that is
not empty, at the k-th 'Mech place. So a character in a 'Mech is not drawn as a person; the party
shows as its 'Mech, on every map, in town or out. Rebuilt for every drawing (`Town::figures`,
`Town::party_mech_figure`).

Sinking (not while F:d346 is set, on map 14: docs/cache.md): the tile under the figure (the cell holding its half-cell) means water in that quarter
if it is below 0xf6, its high nibble below 3, and it has any of the bits that DG:0332 gives for
the figure's place k and the position's odd/even half-cells. Then the figure sinks 2 rows, or 4
if the high nibble is 2: the sprite's height is cut, so its bottom rows are not drawn.

Walking frames: after every key, `1000:231d` checks it against the 8 walking keys (DG:0160).
For the matching direction it moves every character's script on (`1000:1732`): if the
character last walked another way, its script starts over at that direction's. A script (at
37e8:0270, 8 bytes per direction, the 'Mechs' then the characters') is frame bytes with three
codes: 0xfd n records direction n, 0xfe n jumps back n bytes, 0xff holds. The characters' are
four frames per direction: north 0x18-0x1b, the east side 0x14-0x17, south 0x10-0x13, the west
side 0x1c-0x1f. A new game starts everyone on the south script without stepping through it:
frame 0x10. A key that asks a question gets its frame only after the answer. `1000:231d`
moves the 'Mechs' scripts on too, when there are any; that part is not rebuilt yet.

## Sprites

`1527:0044` cuts MECHSHAP.CMP into the sprite table at start-up with `273d:070a` (slot, column
of 8 pixels, pixel row, width in columns, height). The full list is `formats/sprite_sheet.cpp`.
The EGA drawer (`287f:0377`) treats colour 0 as see-through. The party's 8x8 people are row 32
(slots 0xa6..0xb9); row 24 has red ones, row 40 grey ones. A person's slot is its sprite base
(F:d55e + figure, so F:d562 + character) plus its frame. The new-game setup gives Jason 0x96, row 32;
the other characters' bases start at 0, so any other party member on foot is drawn from row 24 (red)
until something sets theirs.
