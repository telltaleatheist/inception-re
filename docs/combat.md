# Combat

How a fight starts, how it runs and what it leaves behind, as read from BTECH.EXE. What is
rebuilt is said in 1.2.1 and 1.2.2. The records it works on are in [formats/mech.md](formats/mech.md) (the 'Mech
record and the tables in the EXE) and [formats/characters.md](formats/characters.md) (the
character record and the per-figure arrays).

Each statement is from the code unless it says "guess". "Checked" means it was read
instruction by instruction more than once; "open" items are collected at the end.

## Conventions

**Addresses.** Code is `ssss:oooo` (load segment 0x1000 included; Ghidra names the function
`FUN_ssss_oooo`). Data addresses carry the prefix of the segment the code really uses: `DG:`
(DGROUP: texts, rule tables, settings), `F:` (far data: flags, C-Bills, the character and 'Mech
records, the figure arrays), `P:` (the party position, the map cells), `M:` (the menu state) and
`27e8:` (the 'Mech templates). docs/original.md, "Data segments", defines them.

**Positions** are two words (docs/movement.md): the low 7 bits of each count half-cells in a
64x64-cell area, x bits 8-11 are the area column, y bits 12-15 the area row. Combat measures in
half-cells on a "linear" grid: X = column x 128 + (x & 0x7f), Y = row x 128 + (y & 0x7f).
Steps that cross an area edge are fixed up by carrying into or borrowing from the area bits.

**The party position as a cursor.** Most combat helpers measure from the party position, not
from an argument. The code first moves the party position onto the figure that acts (with
`1000:17bb`, which walks it there a half-cell at a time and loads map areas, or `1000:186f`,
which only sets it) and puts it back afterwards. The rebuild passes the position as an argument.

**Figures.** A fight has 24 figure slots: 0-3 the party's 'Mechs ('Mech records 0-3), 4-11 the
party's eight characters on foot (character p is figure 4 + p), 12-15 the enemy 'Mechs ('Mech
records 4-7, figure - 8), 16-23 the enemy people (character records 8-15, figure - 8). Outside a
fight figures 16-23 are the townspeople. Details in formats/characters.md.

**Random numbers.** Three primitives, all from the one generator (docs/town_loop.md):

| name | code | draws |
|---|---|---|
| r | `287f:0bc0` | one raw byte 0-255 |
| d6 | `1000:19f3` | r & 7, drawn again while it is above 5, plus 1: one or more raw draws (one with chance 3/4) |
| 2d6 | `1000:19dd` | two d6 |

Every draw counts: the rebuild must make the same draws in the same order, including draws whose
result is thrown away.

## 1. How a fight is reached

`203b:000a(kind)` (FUN_203b_000a) runs every fight. Its one argument picks the kind:

| kind | caller | what |
|---|---|---|
| 0 | the town's game tick, `1000:02ae` | a random encounter outdoors |
| 1 | the mission driver `17dc:0629`, modes 2-7 | a training mission or the raid |
| 2 | the mission driver, mode 8 | the Arena |
| 3 | the mission driver, mode 9 | the fights of mode 9 (Jason alone on foot) |

Kind 0 is the only one that builds its own enemies, asks "Engage in combat?", allows evading,
and gets the salvage tail; kind 3 shares that tail; kinds 1 and 2 get the mission tail. For
kinds 1-3 the mission driver calls `17dc:0d49` to set up the opponents first.

### 1.1 The random encounter

In the game tick (docs/town_loop.md, step 2) one r is drawn every tick. A fight of kind 0
starts when the party is outdoors (flag 04, F:d310), (r AND flag 24 (F:d330)) is 0, and
F:d346 (flag 3a) is 0. Flag 24 is a mask: with 0 every tick qualifies, with 0x7f one tick in
128. The double-agent ending sets flag 24 to 0x7f (2.9). Where else flag 24 is set is not
traced. A tick that qualifies does not always give a fight: the encounter set-up may place
nobody, and the fight then ends at once (2.3).

### 1.2 The training center: specials 1-3

TRAINING.BLD uses specials 1, 2 and 3 (docs/scripts.md, "Specials").

- **Special 1** (`24d3:0027`): chooses Jason's 'Mech by flag 02: 0 the LOCUST template
  (27e8:02f0, sprite base 0), 1 the WASP (27e8:036d, sprite base 0x92), 2 the CHAMELEON
  (27e8:04e4). It sets Jason's 'Mech slot (character 0 byte 0x0c, F:c620) to 0, copies the
  0x7d-byte template into 'Mech record 0 (F:c724) and sets its byte 0x79 (pilot) to 0. Unless
  the demo plays, it shows picture 0. The party is put at (0x0c3c, 0xc04f), figure 0's walking
  script is set (DG:01f6 = 27e8:0280) and the 'Mech walks in four half-cells east, one walking
  frame and six frames of map animation per step. Then it runs the mission driver with
  flag 00 (the mission number).
- **Special 2** (`24d3:0187`): prints the 'Mech's name by its first letter (DG:48b3 for 'L',
  DG:48df + DG:48ee for 'W', DG:48e4 + DG:48ee otherwise).
- **Special 3** (`24d3:01c2`): Jason leaves the 'Mech: 'Mech 0 byte 0 = ff, character 0's
  'Mech slot = 8 (on foot), the party at (0x0c2e, 0xc076). In town it also removes every map mark
  whose code is 0x7c or 0x7d (with or without bit 7) from the 64-entry mark ring (3.13): the
  fires left on the training field, a guess.

### 1.2.1 Mission 2 from the 'Mech choice to the orders menu (rebuilt)

Rebuilt in `src/game/fight.cpp` and matched on the original by the rig's training cases
(docs/reference.md), screen and state, for the Locust:

- Special 1 makes one draw (in `1000:48b7`, picture 0 always shows) and walks the 'Mech in: four
  half-cells from x 0x0c3c, at each one a walking frame (the 'Mech script for east: 6, 7, 4, 5)
  and six frames, each with the tile animation's next step, the view, the figures (`1000:051b`)
  and the map columns 13-39 copied to the screen, which erases the text there. A fifth frame (6)
  is taken and not drawn. `051b` draws the k-th 'Mech two half-cells above the party (DG:3a26 /
  3a2a; the 24x24 sprite's foot is two half-cells below its top) and records it as figure k.
- `0d49(1, 0)` (1.4) makes two draws: the start x (then replaced by 0x0c72) and the start y of
  the enemy, figure 12, 50 half-cells east, out of view. Flag 03 at most 4 (no failed tries) adds
  none. The townspeople's figures become ffff.
- `203b:000a(1)` places figure 0 two half-cells above the party (its active count goes to 2) and
  asks the three questions in window 3 over the walk-in's last frame (the view is not drawn
  again): No first, Verbose (menu record 3 from row 2), Yes. The party box's bars are gone.
- `203b:14c3`: "Jason's" / the 'Mech's name in window 4 (`15ab:18e8`), the party's position
  moved onto figure 0, the view and the fight's figures (`1000:0e4b`: 'Mechs sorted by their
  screen row) drawn, a 3x3-cell box XORed with 15 over the unit on the shown screen, and the
  orders menu (DG:3a6f, menu record 3, ten items) with the bar on Begin Fight.

The box XORs colour numbers, not colours: the game's palette shows numbers 0 and 1 both as black,
and the 'Mech sprites' outline is number 1, so it turns 14 where a black 0 turns 15. The renderer
works the box out from the colour numbers of the tiles and figures under it
(`platform::draw_xor_box`).

The box stays on the screen until something paints over it: the next page copied to the screen
(`273d:06c3`), or a window cleared, framed or written over its cells. With combat graphics off no
round redraws the map, so after the fight the box is still there until the next window covers it
(the raid's ending text does). The rebuild's text layer counts its painting calls and stamps each
cell it paints; a box cell painted after the box was drawn shows the paint (`Town::boxes`).

Mission 3 (mode 3) is rebuilt: (r & 3) + 3 enemy people (`0d49`'s second count, 1.4 step 6),
then the same fight as mode 2. The `people` sequence compares it to the orders menu and 20
rounds; from round 6 the soldiers' lines of sight read the area grids (1.2.1). The orders menu
itself is in 2.6.

### 1.2.2 Mission 2 fought by the computer (rebuilt)

With "Should the computer fight?" answered Yes, at every combat messages level and with combat
graphics off or on, the whole fight runs from random draws alone: the planner (section 4), the rounds (section 3) and
the end tests (2.8), up to "You should return to base now." (2.12). Rebuilt in
`src/game/combat.cpp` (the rules) and `Town::fight_rounds` (the loop), and matched on the
original by the rig's `mission`, `verbose`, `brief` and `drawn` (graphics on) sequences
(docs/reference.md): the
screen (with messages on, every shot's line in window 4, 3.8) and the state after rounds 4 to 8 and
at the end, every figure's place, heading, facing and frame, the orders, targets, paths, heat
and map marks, what the figure drawer wrote (3.14), the parked facings and where each figure is
in its script, and both 'Mech records byte for byte. In it the party's Locust shoots the enemy
Locust (which has no weapons, no ammunition and no hips, so it creeps 1 half-cell a round)
until it is destroyed.

The townspeople's headings are figures 16-23's headings (one table, F:3920), so a fight starts
from and leaves behind the walkers' headings.

The logic runs a fight on to its next stop at once (the round's poll, docs/resuming.md) and
records what the screen showed on the way: every page shown with how long it was held, the
line pixels, the sounds, and the pause after each shot's message (3.8, 12 x DG:015e frames). The
platform plays them back at the original's pace (`src/game/shown.h`, `main.cpp`) and then
resumes the fight; a key pressed meanwhile is what the next poll sees.

With combat graphics on, everything the shots' graphics change is rebuilt (3.14): the poses'
frames and scripts, the parked facings, and the drawer's terrain at every redraw, which changes
the fight's outcome (quirk 36: in the `drawn` run Jason's Locust starts in water). So is what
they show: the round's redraw, the poses, the missiles, the lines, the explosions and the
pictures, matched pixel for pixel at the end of every round by the `drawn` sequence. Steps the
original shows without a wait (the round's redraw, a missile's steps, an explosion's) went by
as fast as the machine could draw the next one, so their pace depended on the CPU: on the
machines of the day each probably took several display frames. The platform holds each one
`--step-hold` frames (default 1), a setting of the platform, not of the logic. A line is shown whole once plotted. The
sounds are recorded but not played yet (no sound is rebuilt). A story picture is held 50 frames
at its last frame.

Not rebuilt yet: the sounds and the double agent. Scan's Detail (2.6) opens the 'Mech stats screen
(docs/town_loop.md).

**The cell window in a fight.** The planner's path (3.3) and the line of sight (3.6) read map
cells from the 24x24-cell window P:07ad by index, as the townspeople do. The window and its
corner index (P:09ed) are not moved with the party position: `1000:17bb` walks P:a44b / P:a44d
there with the step routines `287f:158c` etc., which change only the area grids, at an area
edge. For a shot (`22e8:000c`'s firing loop) the round keeps the position it stands at, sets it
to the shooter for the direction to the target (`287f:0971`), puts the kept position back and
only then walks to the shooter, so a shooter across an area edge has its area in grid 4; after the line
of sight, clear (the shot fired) or blocked, it walks back to the kept position (`22e8:0d9f`) and
sets it; the other ways out (out of range, no weapon) only set it, leaving the grids where they
were. The line of sight reloads both at the shooter itself (`287f:1314`, then `287f:1df8`, which
sets P:09ed..09f1, at `1e31:1c24`), and the planner calls it for the figure it plans before it
simulates the path, so both read the window around that figure. The rebuild builds the same
window from the terrain (docs/wilderness.md: `Terrain::window`, read through `window_cell`), so the
cells past the map file's edge are the terrain blocks the original shows there. Row wrapping (an
index past a row's end reads the next row) is the original's and is kept.

**An index outside the window.** Below 0 the original reads the P: bytes before the buffer: the
nine block values (P:07a4..07ac), then the nine area grids (P:0564..07a3), then the masks
(P:0324..0563). The rebuild holds all three and reads them in the same order, so an index down to
-0x489 gives the original's byte. On the town maps the grids hold the neighbouring areas' terrain
types (0x00-0x30), all below DG:0150, so a line of sight goes on through them. Mission 3 meets it
from round 6: a soldier's line of sight to Jason's 'Mech reads indices -22 and -21 (P:0797,
P:0798, grid 8's entries) and the soldier sees and fires; the `people` sequence checks 20 rounds
(40 were checked once). The masks differ from the original's only at the outer edges of the 3 x 3
grids (wilderness.md), which no line of sight has reached. From 576 on the original reads the
corner words P:09ed..09f2 and the bytes after them (docs/wilderness.md, "The window"); the rebuild
gives them up to the map's last block, and past that counts the cell as blocking and logs it. A path (12 half-cells at most) cannot leave the window; a line of
sight at long range can (the figure is 8-15 cells from the window's edges).

**Where the rebuild stands in for memory it does not hold.** Logged when met.

- *A 'Mech with no pilot fires (pilot byte ff).* The round reads the shooter's skill from
  character 255 (the byte is not sign-extended: F:c614 + 0x11 x 255 + 4 + skill, F:d707 on),
  past the 16 records. The rebuild uses skill 0. Enemy records copy their pilot byte from the
  template (0, Jason) in mission 2; how a party 'Mech with nobody inside could reach a round is
  not known.

### 1.3 The mission driver `17dc:0629(mode)`

FUN_17dc_0629. A jump table at cs:086f by mode. All modes share a walking loop like the town's
(a key is a step, `1000:218f`; with no key the tiles animate and the townspeople move) and the
results step at 0bf2. Rebuilt: modes 0-9, the raid and the walk (`game/mission.cpp`), and the
results of modes 2-7.

**The walk** (0883..0bef). Each pass, first: with the party back at the training center's door
(x <= 0x0c3c, 0xc049 <= y <= 0xc04f) the mission ends if its task is done ([bp-2]); if not, the
first time only, the message box shows DGROUP:146b (`mission.not_done`) and waits for a key. Then a key waiting (`273d:002f`) is read, turned into an arrow
(`2656:0d1d`), the rest of the keyboard is thrown away (`1000:2a2b`), the figures walk
(`1000:231d`) and the party steps (`1000:218f`, any other key a step of (0, 0)), and a tick
follows at once. With no key a frame passes (`273d:0006(1)`), and every eleventh such pass ticks
(a countdown from 10, 0 at the start, so the first pass ticks). A tick: the tiles' next frame
(`1000:240b`), the townspeople move every fourth tick (DGROUP:5802, kept from one mission to the
next, counts 1, 2, 3, 0: they move when it reaches 1), and the map and figures are drawn
(`287f:18ef`, `1000:051b`, `273d:06c3`). Then the mode's own check. The town's timers, money step
and random draw do not run here. The pacing is the town's: the key feeder's polls give the same
pass counts. The callers: special 1 (modes 0-7 from flag 00), ARENA.BLD's special 0x23
(`24d3:10e2`, mode 8) and `19b8:152f` (mode 9, run from JAIL.BLD's special 0x28).

| mode | what happens | pass (flag 01 = 1) |
|---|---|---|
| 0 | A walking course. P:200a (the map store's byte 0xfed) is saved and set to 0, then a step of (0, 0) (`218f(0)`). Every tick counts 1, plus F:32ae of figure 0 >> 3: 1 more in shallow water, 2 more in deep (`1000:051b` writes 8 or 16 there for a 'Mech, below). So the count is time, idle ticks included. Reaching x >= 0x0c78 and y >= 0xc07c marks the task done, the first time with DG:14a1 (`mission.course_corner`, `24d3:17ea`, a key). | done, and the count (less 50 if record 0's name starts with L, a Locust) below 215. P:200a is restored. |
| 1 | A marked spot. One r & 7 picks a spot from DG:1632 / DG:163a (`tables.marked_spot_x/y`, a cell column and row in the area); that cell of the map store (P:101d + block x 64 + cell) is saved and set to tile 0x50. With a Locust a message (DG:1434, `mission.failed`) and a key come first. Then `218f(0)`. The task is done, after a tick, when the party's cell ((x & 0x7f) / 2, (y & 0x7f) / 2) is the one below the spot or below and to the left: a message and a key, DG:14ef (`mission.no_hands`) with a Locust, else DG:152a (`mission.hands`), and then the map cell is put back. Only the map store: the cell window keeps tile 0x50 until the next step builds it (`287f:1314` at the end of `218f`). | the 'Mech is not a Locust (it has no hands; the script says so). The cell is put back again. Either way Piloting (character 0 byte 8) + 1 if below 3. |
| 2 | Target practice. One enemy 'Mech (`0d49(1, 0)`), then its record 4 is disarmed: both actuator bytes 0x24 / 0x25, all ten ammunition bytes 0x27-0x30 and both MP bytes 0x31 / 0x32 set to 0. It cannot move, kick or fire. Fight kind 1. | as below |
| 3 | 3-6 enemy people (`0d49(0, (r & 3) + 3)`: the random count is pushed first, so it is the second argument), fight kind 1. | as below |
| 4-7 | 1-4 enemy 'Mechs (`0d49(mode - 3, 0)`), fight kind 1. Mode 4 alone sets F:0090 = 1 (no "computer fights for you" question, no Computer order): the switch (17dc:086f) sends mode 4 to 17dc:0765, which sets it, and modes 5-7 to 0770, just after. Mode 6: one r, the raid if r & 1. Mode 7: always the raid. In the raid F:3772 = 1 and the 16 cells P:101d + 0ff0..0fff are set to tile 0x40 (a gap in a wall: a guess; map 11's load in the fight replaces them). F:0090 = 0 afterwards. | as below |
| 8 | The Arena: `0d49(0x81, 0)`: one enemy 'Mech by the bit-0x80 branch of 1.4 (two, the second the spectator, when F:d452..d455 holds an ff), figure 0 active, fight kind 2. | not set by this mode |
| 9 | The jail break: Jason alone on foot, a fight of kind 3 every 81 ticks, and the four bays on row 702d (1.7). | not set by this mode |

The results step (all modes): flag 01 (F:d30d) = 0 first.
- Modes 2-7 pass when Jason's 'Mech still exists (F:c724 != ff) and F:3992 is 0 (the party did
  not flee; see bug 13).
- Modes 5-7: Gunnery (character 0 byte 7) and then Piloting (byte 8) each go up by r & 1. The r
  is drawn only for a skill below 4.
- After the raid (F:3772): flag 04 (outdoors) = 1, flag 05 = 1 if Jason's 'Mech exists. F:3772 = 0.
- F:398e = 0. If F:4594 is set, the place's script is deciphered again (`17dc:1d30`): the fight
  reused the script buffer.

After a fight in modes 2-8, `17dc:134b` puts the party position on figure 0, or on figure 4 when
Jason is on foot (F:c620 = 8), and redraws: the window built there, the map and the figures
drawn. Figure 0's y is taken 2 half-cells down (its formation place is 2 above the party's), into
the next area when that passes the area's edge (y & 0x80: + 0x0f80). The task is then done, and
the walk takes the party back to the door (`Town::party_to_jason`).

### 1.4 Opponent set-up `17dc:0d49(a, b)`

FUN_17dc_0d49. `a` is the number of enemy 'Mechs, `b` the number of enemy people (each with bit
0x80 as a flag).

1. Saves the townspeople's positions (figures 16-23) into their records, F:d390 + 0x1a i, at +0
   and +2 (docs/townspeople.md). The fight puts them back (2.13).
2. Empties 'Mech records 4-7 and character records 8-15 (byte 0 = ff). Every figure: destroyed
   flag 0, active 0, x = y = ffff.
3. b below 0x80: figure 0 active (Jason's 'Mech); else figure 4 (Jason on foot).
4. The place. If a has bit 0x80 (only the Arena, mode 8: `0d49(0x81, 0)`): x = 0x0a10 + (r & 3),
   then an r, and if r & 1, x = 0x0a28 + (r & 3); y = 0x806f + (r & 3) (`17dc:0e4e`). With F:e48e
   set, a = 2 if any of F:d452..d455 (the first bytes `19b8:152f` saved of 'Mech records 0-3) is
   ff. Then a 2d6 is drawn and the index is 2d6 - 2; with F:e48e set, one more r replaces it:
   (r & 3) + 3. The template is DG:13e2 at that index and the sprite base DG:140e
   (formats/mech.md). In the Arena F:e48e is set (`17dc:1c9b`, 1.5), so the index is 3-6: the
   LOCUST, WASP, LOCUST or STINGER, and the 2d6 is drawn but not used. So the Arena draws three
   or four r for the place, a 2d6 (`1000:19dd`: two d6, each drawing again on r & 7 above 5) and
   one r for the template.
   Otherwise: x = 0x0c65 + (r & 7), y = 0xc059 + (r & 7) (the training field); b with bit 0x80:
   (0x0d10, 0x7024); flag 00 = 2: x = 0x0c72. The template is LOCUST (27e8:02f0).
5. a = a & 0x7f; in the raid (F:3772) a = 4. For each i below a:
   - character 8 + i: byte 0 = 0, its 'Mech slot (byte 0x0c) = 4 + i;
   - 'Mech record 4 + i: the template's 0x7d bytes. In the raid the template is JENNER
     (27e8:0561). With F:e48e and i > 0, record 5 is also filled from 27e8:05de (the
     "spectator", 3.14);
   - flag 03 (F:d30f): 0 in the raid; clamped to at most 6. Each armour byte 0x11-0x1b is cut by
     DG:165a[flag 03], to 0 if it is not more than the cut;
   - flag 03 above 4: five draws, each picks slot byte 0x34 + (r & 0x1f) and sets its destroyed
     bit if the slot is not empty;
   - flag 03 above 5: engine hits (0x75), sensor hits (0x77), gyro hits (0x76) = r & 1 each, in
     that order;
   - so a higher flag 03 makes the enemy 'Mechs weaker;
   - figure 12 + i: sprite base, frame 0x0c, walking script 27e8:02a0 (DG:0226 + 4i), heading
     and facing 6, active, position = place + (DG:1642[i], DG:164e[i]).
6. People: if b is not 0, b = (b & 0x7f) + the next character number, at most 16. For each
   remaining character j up to b: byte 0 = 0, 'Mech slot 8 (on foot), sprite base fe, frame
   0x1c, walking script 27e8:02e0, heading and facing 6, active, position from the same offset
   tables at index j; skills 4-9 = r & 1 each (6 draws, Medical is not set), byte 1 = 2d6,
   byte 0x0f (hit points) = 10 x byte 1, byte 0x0b (weapon) = r mod 14 (r is 0-255).
   The offsets are read at the character number j (8-15), not from 0: past index 11 the x offset
   comes from the y table (DG:164e + j - 12) and the y offset from the armour cut after it
   (DG:165a + j - 12). The three tables lie in a row, so this is the original's placement and
   is kept as it is (no switch: the intended places are not known). Rebuilt for the training
   missions (`Town::place_mission`). Each soldier's heading is written to F:3920 + figure,
   which is also its townsperson's heading (one table), so the townspeople come back from the
   fight facing as their figures last did.

### 1.5 The Arena and the raid

- **The Arena.** ARENA.BLD's special 0x21 calls `17dc:1c9b`: 'Mech record 0 is saved at F:3780
  and replaced by the LOCUST template, characters 1-7 are hidden (first bytes saved at F:3fe9),
  all eight characters' 'Mech slot bytes are set to 3, and F:e48e = 1. Special 0x23 runs mode 8;
  then, only with flag 23 clear (the spectator not destroyed), `17dc:1b41` undoes it (slots
  back to 8); with flag 23 set it takes the theft's way instead. Either way F:e48e = 0 at the
  end (docs/scripts.md, "The Arena"). F:e48e marks figure 13 as
  the "spectator" (3.14). The enemy 'Mech is made by `0d49`'s bit-0x80 branch (1.4, step 4).
- **The Kurita raid** (F:3772, modes 6 and 7): four JENNERs, the armour cut off, a story message
  first (2.4), one more story line per round (2.11), and fleeing only by walking out of the
  area (2.7).

### 1.6 The alley fight: special 1e and kind -1

PARTY's second visit (docs/scripts.md, "The first companion") ends in special 1e, which calls
`19b8:104e` (FUN_19b8_104e). It sets everything up itself and then calls the fight routine with
kind -1. Its steps, in order:

1. The party position becomes 0a69,805e (this is the fight's entry position).
2. The companion, character record 1: byte 0 = F:d456, which is then increased (the next name
   number); byte 1 = 12, byte 2 = 9, byte 3 = 8, the seven skills (bytes 4-10) from DG:1e7a,
   byte 0x0b = 7, byte 0x0c = 8 (on foot), bytes 0x0d and 0x0e = 0, byte 0x0f = 120 (hit
   points), byte 0x10 = 0. Figure 5's sprite set F:d55e + 5 = 0.
3. A 'Mech for him: the first slot k (0-3) whose letter F:d452 + k is ff. 'Mech record k is a
   copy of template 3 (37e8:0467) with byte 0 = ff, so it is not in use; byte 0x79 = 1, the
   letter F:d452 + k = 'C', the sprite set F:d55e + k = 0x92. With no free slot the search
   stops at k = 4, which would write 'Mech record 4 and, as the "letter", F:d456 (the name
   number). Kept, not reached on our routes.
4. The townspeople's positions (figures 16-23) are saved in their records (F:d390 + 0x1a i,
   +0 and +2), as the fight routine does for kind 0 (2.1).
5. Enemy 'Mech records 4-7 and character records 8-15: byte 0 = ff.
6. All 24 figures: destroyed and active words 0, x = y = ffff. Then figures 4 and 5 (Jason and
   the companion on foot) get active = 1, and their computer-control flags F:3994 + 4/5 = 0.
7. Three draws: the base x = 0a72 + (r & 1), the base y = 805d + (r & 1), and the last record
   = 10 + (r & 3), so records 8 up to 9-12: two to five soldiers.
8. For each soldier, record i (from 8) and figure i + 8 (the townspeople's figures 16-20):
   - record byte 0 = 0 (a name number of 0), byte 0x0c = 8 (on foot);
   - figure: sprite set F:d55e + (i + 8) = fe, walking frame F:409a + (i + 8) = 0x1c, figure
     script DG:01f6 + 4(i + 8) = 37e8:02e0, heading F:3920 and facing F:396c = 6, active = 1;
     x = base x + DG:1e7a[i], y = base y + DG:1e82[i] (fixed offsets: a short block of
     three soldiers by two rows);
   - six draws: record bytes 4-9 = r & 1 each; then byte 1 = 2d6 (`1000:19dd`) and byte 0x0f
     = 10 x byte 1; then byte 0x0b = r mod 8.
   Bytes 2, 3, 0x0a, 0x0d, 0x0e and 0x10 are not written: they keep what the last fight left in
   that record.
9. `203b:000a(-1)`.

How kind -1 goes through the fight routine. The routine tests the kind 16 times, in 14 places;
-1 is "not 0" where it tests for 0, and "below" where it compares, so it takes kind 0's way
in some places and kinds 1-3's in others. The kind is not stored anywhere else.

| where | the test | kind -1 |
|---|---|---|
| 203b:005c | kind 0 saves the townspeople's positions (2.1) | no (step 4 above did it) |
| 203b:0116 | kind 0 clears every figure (2.1) | no (step 6 above did it) |
| 203b:02db | kind 0 builds the encounter and tests the reach (2.3) | no: an enemy is "reached" at once |
| 203b:0476 | any other kind has an enemy present | yes: it always fights, no Attacking-force box, no Engage question |
| 203b:04e6 | kind 2 draws the view on the party (2.4) | no |
| 203b:0680 | kinds 2 and 3 cannot flee (2.7) | it can flee, with the same draw |
| 203b:06cb | escaped in kinds 1-3 sets F:3992 (2.7) | not set |
| 203b:087b | the round's argument, 1 for kinds 1-3 (3) | 0, as kind 0 |
| 203b:0d7c | kind 0's enemy people run (2.10) | no: the soldiers never run |
| 203b:0ef6 | kind 1 ends when figure 0 is gone (2.8) | no |
| 203b:0f13 | kind 2 ends when the Arena is lost (2.8) | no |
| 203b:1051 | kinds below 1 and kind 3 go on to the after-fight steps (2.12) | yes: the win text, 2.15, healing, or "eluded" |
| 203b:12f1 | kinds below 1 and kind 3: "You return to where you were..." and the walk back (2.12) | yes, to 0a69,805e |
| 203b:1388 | kind 1: "You should return to base now." | not reached (kinds 1-2 only) |

So the alley fight runs like a won or fled random encounter, with no encounter box: the computer
question first (2.4), Flee allowed, and kind 0's ending, which walks the party back to 0a69,805e.

### 1.7 The jail break: special 0x28 and mode 9

JAIL.BLD's special 0x28 (40), only on the recruit's visit and only after Yes at the impounded
'Mech's question (docs/scripts.md, "JAIL after its recruit"), calls `19b8:152f`:

1. **Alone.** For characters 1-7, the first byte of the record is saved at F:3fe9 + p and set to
   ff; for 'Mech records 0-3, the first byte at F:d452 + m (where the held 'Mechs' letters live)
   and set to ff. The party is put at (0x0d10, 0x7024) and Jason on foot (record 0 byte 0x0c =
   8, F:c620).
2. **The walk:** the mission driver with mode 9 (below).
3. **Back.** The party is put at (0x0d00, 0x7014). Records 1-7 and the 'Mech records get their first bytes back, and F:d452-d455 are
   set to ff: no 'Mech is held any more.
4. **The impounded 'Mech.** If Jason is up (DG:014a not 0), the 0x7d bytes at 27e8:03ea (the
   segment in DG:54f2) are copied into the first 'Mech record 0-3 whose first byte is ff. That
   is 'Mech template 2, the STINGER (27e8:02f0 + 2 x 0x7d, `rules.mech_templates`,
   formats/mech.md). Only one: the loop stops there.

Special 0x28 then sets flag 28 = 1 if Jason fell (DG:014a = 0), and the script ends at 10c4.
Otherwise it goes on to the escape scene and special 0x27 (39), the crew screen started with 1:
everyone on foot and every seat empty first.

**Mode 9 in the driver** (`17dc:0629`, 1.3). Set-up: the tick counter and the found count are
0, the four bays unvisited, F:398e = 1; the window is rebuilt at the party (`287f:1314`) and the
figures drawn (`1000:051b`). Then the walk shared with every mode: a key is a step (`218f`),
and with no key, every eleventh empty poll is a tick. After each step or tick:

- **A fight.** The counter is compared before it goes up: at 80 or more, `0d49(0, 0x88)` places
  eight enemy people (1.4, bit 0x80) and the fight routine runs with kind 3, then the counter is
  0 again. So the first fight comes on the 81st tick of the walk, and one every 81 after that.
  If Jason fell (DG:014a = 0), the mode ends.
- **The bays.** On row y = 0x702d, at x = 0x0d14 + 4i (i = 0-3), a bay not visited before: it is
  marked visited, window 6 is cleared and two texts (DG:155d, 159a) say Jason climbs aboard;
  `173d:086a`, a key, and a third text (DG:15d4) says he tries to start it. The found count
  is then compared before it goes up:
  - **0 or 1:** `19b8:16b2` (below), then window 6 and DG:1611, the 'Mech will not start
    (`24d3:17ea`, with its pause), and a key.
  - **2:** the picture box, O0.ANM's last frame (`1000:48b7(0, 0)`), window 6 and DG:15f8, the
    'Mech starts (`17ea`), and the mode ends with a key.

  So the third bay Jason visits always holds the 'Mech that starts, whichever bays they are
  and in whatever order: it is a count, not a place.

`19b8:16b2` plays the failed start: the disk check (`1000:28cc(1)`, no disks in the rebuild),
O0.ANM loaded at P:42c3 (`173d:0814`), its frame buffer (0x1e78 bytes at P:244b) cleared, the
list cursor F:e48a at 0 and the frames' data at 42f6. Then `1000:1afd` shows frames until the
frame list entry at the cursor is 0x49 or more. Then sound 14 (`1000:19bf`), a wait of 60 ticks
(`173d:0006`), and window 4 cleared.

The results step does not set flag 01 for mode 9; F:398e goes back to 0 (`17dc:0d21`).

**The bays drawn (F:398e).** While F:398e is set, the figure drawing (`1000:051b` at 0d30, and
`1000:161a` likewise) adds the four bays after the 'Mechs: at (0x0d13 + 4i, 0x702c), only when
the whole of it is in the view (half-cells 0d-27 across and 00-18 down, the party at 1a, 0c).
Each is sprite 0x92 (the sprite table F:39fa, entry 0x92 at F:3c42), the first frame of the
'Mechs' sprites: a 'Mech standing, facing the viewer. `287f:28eb` draws it onto the back buffer
P:244b (its first argument is the destination), the buffer `19b8:16b2` clears for O0.ANM's
frames.

**The panel.** Nothing draws the party box during the jail break, so it keeps the bars of the
whole party, though records 1-7 are hidden (the rebuild holds the bars as last drawn until the
panel is drawn again).

## 2. The fight routine `203b:000a`

FUN_203b_000a, 5240 bytes. No return value.

### 2.1 Set-up

1. DG:2b20 = 12 (the weapon target picker's first candidate, `1d43:07cb`).
2. The party position at entry is saved.
3. The nine bytes P:07a4..07ac are copied (only their bit 0x80 is used, in 2.15). They sit just
   before the map cell buffer P:07ad; one per loaded map block is a guess.
4. Kind 0: the townspeople's current positions (figures 16-23) are saved into their records
   (+0 / +2), as `0d49` does for the other kinds.
5. The per-figure tables are cleared for all 24 figures: paths F:40b4 to 2 (the end mark), targets
   F:3800 to ff, orders F:32c6 to ff. Kind 0 also clears every figure (destroyed 0, active 0,
   terrain F:32ae 0, heading ff, x = y = ffff); for kinds 1-3 `0d49` did that.
6. For the eight 'Mech records: heat F:006e, Inferno count F:d576 = 0; the computer-control
   flags F:3994 for figures 0-11 = 0.

### 2.2 Placement

The party is placed from the saved position each time.
- Characters on foot first (character exists and byte 0x0c >= 8). The k-th one placed goes at
  the party position plus formation offset k (DG:3a16 / DG:3a1e, eight places: the first at the
  party's own half-cell, the next ones around it). Its active word is incremented, not set.
- Then the party's 'Mechs that exist, the k-th at offset k of DG:3a26 / DG:3a2a (four places,
  the first two half-cells north).
- k counts only members that exist, so the first 'Mech always gets the first place whatever
  its slot.

### 2.3 The encounter (kind 0 only)

1. `15ab:0d3d` (FUN_15ab_0d3d) builds the encounter:
   - The view is set on the party. Offsets: dx = (r & 7) + 10, negated if r & 1; dy the same (4
     draws in the order dx, sign, dy, sign). The search starts at that view cell, 10-17
     half-cells away on each axis.
   - Figures 12-23 are cleared.
   - Enemy people, characters 8-15 in order: byte 0 = ff, then one r; if r & 1 a soldier is
     made: name number 1, on foot; weapon = DG:2cf4[sum of 7 draws of r & 3]; byte 1 = 2d6,
     hit points = 10 x byte 1 (8-bit); byte 2 = 2d6; the seven skills = r & 3 each; armour type
     = r & 3; armour points = 2d6 + 2d6, whatever the type.
   - Enemy 'Mechs, records 4-7: byte 0 = ff, then one r; if r & 1 and the party's 'Mech m - 4
     exists, one more r picks the template DG:2df8[r mod 3] (LOCUST, WASP, STINGER) and the
     sprite base (0 for the Locust, else 0x92). So there are never more enemy 'Mechs than party
     'Mechs. The armour cut and the destroyed slots of `0d49` are not applied.
   - Placing: first the window is built at the party (`287f:1314`, `1df8`). A cursor steps right
     from the start cell; each step counts, and when the count passes 16 it goes back to 0, the
     cursor to the start column and one row down (so a row is 17 cells). A person needs a cell
     of the window whose tile is above 0x0f and below DG:0150 (the blocking tile), at an index
     of 0 or more; the cursor moves on until it finds one, and one more after placing. A 'Mech
     needs that cell and the one beside it (right when the cursor's x and the party's x have
     the same parity, else left), as in the step check. The cursor moves one on (without the
     row test) before the 'Mechs, and 3 on after each 'Mech, going back to the row start when
     the count passes 8. The cell found becomes a position: party + cursor - (0x1a, 0x0c),
     and a low byte past 0x7f is carried into the next area (or back from it). The search can run
     past the window's last cell into the memory after it (docs/wilderness.md), until a byte there
     passes the test. Someone whose cell is outside the window's 24 x 24 cells is dropped:
     record emptied (byte 0 = ff), figure inactive, x = y = ffff. A person placed gets frame
     0x10, a 'Mech frame 0. No draws.
   Then (`203b:02e9..0305`) window 4, the box above the party box, is selected and cleared
   (`2656:0281`, `0388`) and the live colour set to white (F:37fe = 15), whatever colour its
   record held (a fight's round texts leave 13 there; only a direction write resets it).
2. If any enemy figure is active, `203b:28db` asks whether Jason can reach the first one (the
   lowest active figure of 12-23): the view moves to figure 4 (or to Jason's 'Mech, the figure
   his byte 0x0c names) and the window is built there; then a point walks from there with up
   to 30 steps (F:3770) of `1e31:0006` in mode 0x80. In that mode no figure blocks, the
   stepper tests two cells as for a 'Mech, and it turns figure 0's heading as it goes. A point
   that starts at the target counts as not reached. Afterwards (`203b:2aa3`) the view walks
   back to the place it left and the window is built there again. No: no fight.
3. With no enemy present or none reachable: no fight; the view goes back (2.12), and the
   townspeople come back (2.13).

The encounter comes at the game tick's step 2, before the tick draws the map (docs/town_loop.md),
so the screen (and the page) still shows the map as the previous tick drew it, from before the
step that started the encounter. Nothing draws the map again until a later tick, unless the
fight's graphics or orders screen do.
4. "Attacking force:" (DG:3402), the number of enemy 'Mechs with " 'Mech" / "s" if more than
   one and " and", then always the number of enemy people placed with " human" and "s" unless
   it is 1 (so "0 humans").
5. "Engage in combat?" (DG:3428), yes/no, default yes.
6. Declined: one r; r & 3 not 0 (3 in 4) evades (no fight, 2.12). Otherwise "You did not evade
   them!" (DG:343a), a key, and the fight goes on.

Rebuilt (`Town::random_encounter`, `Town::place_encounter`, `Combat::reaches`,
`Town::encounter_end`). The rig checks it on the walk out of the town after the raid: four
encounters out of reach, then one in reach (four soldiers against Jason alone on foot). Declining
doesn't evade it, and the computer fights five rounds until Jason is killed (2.12, the game over
in docs/town_loop.md). A won encounter's after-fight steps: loot and healing are rebuilt and
compared (`chain_mech_poked`, `chain_heal_poked`), and so are repairs and salvage
(`chain_repair_poked`, `chain_salvage_poked`).

Kinds 1-3 always fight.

### 2.4 The options before the first round

1. In the raid: a message box (DG:3452), a key.
2. Kind 2: the view is set on the party and drawn.
3. F:009e (the computer fights for the party) = 0. Unless F:0090 is set (mode 4), the
   question "Do you want the computer to fight for you?" (`15ab:0b5e`, DG:1115), yes/no with
   F:009e as the default: always No, as it was just cleared.
4. "Combat messages:" None / Brief / Verbose (`203b:24f0`, DG:3d3a) into DG:2e38 (0, 1, 2),
   remembered between fights.
5. "See combat graphics:" yes/no (`203b:2556`, DG:3d5e) into DG:2e3a, remembered. **This
   setting changes how many random numbers a fight draws** (section 5).

### 2.5 The round loop and the order of turns

Each round, in order:

1. The round-start party position is saved. F:3992 = 0.
2. **The party side.**
   - Player control (F:009e = 0): figures 0-11 with x or y = ffff are made inactive, and every
     party figure's path is reset. The orders menu `203b:14c3` runs (2.6); it returns 1 for
     Flee. Fleeing is handled (2.7). If nobody fled, each active party figure in slot order 0-11
     acts: one with F:3994 set is planned by the computer (`1e31:03ab(s, 0)`, section 4); one
     with orders has its movement points worked out (`203b:22bc` for a 'Mech with the type of
     its first order, `203b:2474` on foot) and its path simulated (`203b:193b`).
   - Computer control: `203b:1482(0)` plans figures 0-11 with `1e31:03ab`. If that set F:3992
     (a party person faced a 'Mech, 4.4) and the party has no 'Mech left: one r, and r & 3 not
     0 ends the fight as a flight.
3. If nobody fled:
   - F:374c = 0. `203b:1482(12)` plans every active enemy figure 12-23.
   - `22e8:000c(kind > 0)` runs the round (section 3).
   - The double agent (2.9), the exit zone, the end tests (2.8), enemy people running (2.10),
     the raid story (2.11).
4. With the computer in control: if F:3938 is 0 (no demo) and a key is waiting, F:009e = 0 and
   the key is thrown away. The player takes over next round.

So the turn order is: party orders, party plans in slot order, enemy plans, then the round,
where everyone moves and fires in slot order (section 3).

### 2.6 The player's orders

`203b:14c3` (FUN_203b_14c3) shows the first active party figure and a menu (window 3). The bar
starts on Begin Fight every time the menu is entered (a local of 14c3), so every round. Under demo
playback or key recording (F:3938, F:458c) `2656:0b5e` starts every menu on its first item
instead.

| 'Mech (figures 0-3) | person (4-11) | what |
|---|---|---|
| Walk, Run, Jump | Move | `203b:1c1f`: add one waypoint with the map cursor |
| Use Weapons | Use Weapon | `1d43:0004`: assign targets per weapon |
| Kick | | `203b:2231`: needs both hip actuators; target picker with the Kick in slot 11 |
| | Clear Moves | orders deleted, computer control off |
| Computer | Computer | `1e31:03ab(fig, 1)`: hand the unit to the computer (not when F:0090) |
| Scan Unit | Scan Unit | `203b:2591`: Friends / Enemies / Cancel, then the unit list `15ab:1467` |
| Next Unit | Next Unit | the next active party figure, wrapping 11 to 0 |
| Flee | Flee | leaves the menu, result 1 |
| Begin Fight | Begin Fight | leaves the menu, result 0 |

- **A move order** (`203b:1c1f`): if the 'Mech's orders are of another type (walk, run, jump),
  the player is asked whether to change; yes deletes them. With no movement points left a reason
  is shown (no jump jets DG:3bad; overheated DG:3bca; leg damage or heat DG:3bf3; "You can't ...
  any further this turn" DG:3c33, with a hint to try running). Otherwise "You have N" movement
  points and the cursor: the eight direction keys move it (kept inside columns 0x0d-0x27, rows
  0-0x18); each move puts a tentative waypoint at the cursor and redraws the path; Space or Enter
  accepts. There is no cancel key. Each call adds at most one waypoint (up to 12 per figure).
  The number shown is not updated as the cursor moves. The target may be out of reach: the path
  stops where the points run out. In detail (rebuilt, `Town::move_order`):
  - A unit under the computer has its orders deleted and its screen redrawn first; computer
    control goes off.
  - The question (a 'Mech with orders of another type, unless it asks to jump with no jump jets):
    "You are now" and one string built from the old type's name, "n" for Run, and "ing", then the
    new type's name, as a yes/no question starting on yes. No leaves the order. Yes: "orders
    deleted", the orders cleared, the screen shown again without redrawing, and a key.
  - The new waypoint goes in the first order whose type is ff. The movement points are worked
    out for the new type and the path simulated, window 4 is cleared.
  - With no points: the reasons in that order, then "Press a key." On foot, F:377c and F:4592 are
    cleared before the test, so a person only ever gets "You can't move any further". Both are
    cleared afterwards.
  - The cursor starts where the shown path ends (F:3778/377a), a cell XORed with 14. Keys go
    through `2656:0d1d`. A move that would leave columns 0x0d-0x27 or rows 0-0x18 is undone on
    that axis. On a move the waypoint under the cursor (`1000:191b` from the unit) is written
    into the new order, unless an earlier order holds the same cell (area, x, y): then the new
    order's type is set to ff (bug 16 for areas 0x80 and up). The screen is shown again without
    redrawing (the path's arrows), and the cursor XORed again. Space or Enter ends, keeping the
    waypoint as it stands; window 4 is cleared.
  - Overlapping XORs combine: the cursor on the unit's box shows both colours XORed, and the
    cursor on an arrow (text written over the box) flips the arrow's own colours.
- **Use Weapons** (`1d43:0004`): a 'Mech that is shut down (heat 30) cannot (DG:2904); sensors
  damaged once is a warning (DG:292d), twice and it cannot fire (DG:2964). The list shows each
  weapon (up to 12), its ammunition (Full / Out / a count), its target and range band. A
  destroyed weapon's target is cleared. Listing a weapon clears its "held" bit. Choosing one
  that already has a target asks to override. The target picker (`1d43:07cb`) offers only enemy
  figures 12-23, with no range or line-of-sight check (the round decides). In detail (rebuilt,
  `Town::weapons_order`, `Town::pick_target`):
  - A 'Mech's list is its first twelve items 0x10-0x20 (bit 7 destroyed) in item order. Window 5
    is sized to it: top row 0x16 - n, height n + 2 (the window record changes). "Select the
    weapon to fire." is printed once in window 4; each pass shows the unit's screen again and the
    list in window 5 (frame 0): the heading in light green, then per weapon its name (yellow with
    a target, white without, grey destroyed; on the CGA 1, 15, 2), at column 0x0b its ammunition
    padded to three, at 0x10 its target and at 0x18 the range band from the unit; then Done. A
    target no longer active is cleared.
  - Choosing a destroyed weapon or one with no ammunition: a message in window 4 and a key
    (`1e31:1057`), and the menu's bar is XORed off. Done ends: windows 4 and 3 are cleared and
    framed again.
  - The picker starts from DG:2b20, the figure last offered, kept between calls (12 at start),
    skipping inactive figures and wrapping 23 to 12. Each shows the figure's screen
    (`203b:1774(t, 1)`: the view moves onto the target), a menu of three in window 3 (target
    here, next enemy, cancel; the bar where menu 3 was left), the target's name ('Mech) or its
    weapon (person) and the range band from the unit. Target here sets the slot, cancel clears
    it. If no enemy figure were active the loop would never end; a fight with none is over first.
  - On foot: the weapon's target now and its range band, with a yes/no to change it; then the
    picker for slot 0.
- **Kick** (`203b:2231`, rebuilt, `Town::kick_order`): window 4 cleared. With both hip
  actuators (bit 3 of bytes 0x24 and 0x25), "Choose the 'Mech to kick" and the target picker for
  weapon 0x20 into slot 11; without, a message and a key. Either way the orders menu's last item
  (menu 3, F:00c8) is set to Kick, so the bar comes back on it. The weapons list also uses slots
  0-11, one per listed weapon, and the round reads slot 11 of a 'Mech as the Kick: a 'Mech with
  twelve weapon items would show its twelfth weapon's target in the kick's slot, and ordering
  that weapon would order a kick. Unreachable in 1.03 (a latent bug, no switch): the templates
  carry 2-9 weapons (the Chameleon 9), and the only other writes of weapon items are the 'Mech
  upgrades (`19b8:0925`, below), which reach at most 10 (the Commando after both). A 'Mech
  record is otherwise only ever a copy of a template or of a wreck. The same limit keeps the
  list's ammunition column (byte 0x27 + i) inside the ten ammunition bytes: an eleventh weapon
  would read the walking MP.
- *The 'Mech upgrades* (`19b8:080a` offers, `19b8:0925` pays and applies; read only for the
  weapon count, not rebuilt). The upgrade is chosen by the record's byte 0x7b (a model number,
  0-3 upgradable: 0 the Locust and the Spectator, 1 the Wasp, 2 the Stinger, 3 the Commando)
  and byte 0x7c (0 first upgrade, 1 second, 3 none left): eight cases, each writing fixed item
  slots, ammunition and armour bytes. Weapon counts before, after the first and after the
  second: Locust and Spectator 3, 3, 5; Wasp 2, 2, 5; Stinger 3, 5, 7; Commando 3, 4, 10.
- **Computer** (`1e31:03ab(fig, 1)`): the planner, then the unit's screen with the planned path
  (`203b:1774(fig, 1)`), and the position restored. The menu's pass for a unit under the computer
  shows "The computer is currently moving this unit" and does the same.
- **Scan** (`203b:2591`): a person cannot see enemy 'Mechs while any is active; a 'Mech whose
  sensors are destroyed (byte 0x77 = 2) cannot scan 'Mechs. In detail (rebuilt for 'Mechs,
  `Town::scan_order`, `Town::scan_list`):
  - Window 3: Friends / Enemies / Cancel (menu 3 from row 1, starting on Friends). Enemies, for a
    person: "humans only" and a key if an enemy 'Mech is active, and the list holds people only;
    for a 'Mech with destroyed sensors: a message, a key, people only (bug 19: the check for enemy
    people that follows is skipped). If the list would hold no 'Mechs and no enemy person is
    placed, "no humans" and `1000:2a4f`'s key. Afterwards menu 3's first row is 0 and its last
    item Scan Unit (6 for a 'Mech, 4 for a person).
  - The list (`15ab:1467(fig, 12 x side, mechs)`) takes the twelve figures of that side in turn,
    from the first (from the first person without 'Mechs), skipping any inactive or unplaced
    (x or y ffff). Each is shown in window 4: a party unit's heading (`15ab:18e8`); an enemy
    'Mech, "Enemy", the record's name as it is stored (padded with spaces) and in light green
    "Direction:" with the direction from the scanning unit (`287f:0971`, DG:01aa's names); an
    enemy person, the enemy-human label (DG:114f), its condition (`1e31:02e4`), at row 7 in light green
    the weapon label (DG:115b), at row 8 its weapon's name, then the direction as for a 'Mech.
    The heading is called with both flags 1 here: for a party person it is the character's name,
    then (unless it is the double agent found out: flag 0x27 set, F:d331 the character, the
    first flag, which shows DG:1222, waits for a key and sets F:374a) its condition, at row 7 in
    light green the weapon and armour labels (DG:1245, which ends in white), at row 8 the weapon's
    name and at row 10 its armour: DG:1260 with no armour type (byte 0x0d), DG:1257 in grey
    when the armour's points (byte 0x0e) are 0, else the type's name (F:4e8a), yellow when the
    points differ from the type's full value (DG:4ddb). For a 'Mech the flags are not used. The
    condition (`1e31:02e4`) is the line hit points / B (character bytes 0x0f / 1) of the eleven
    at DG:2e0c. Then the unit's screen
    (`203b:1774(t, 1)`), and in window 3 Next Unit, Detail Scan (a 'Mech, while the list holds
    'Mechs) and Done. Detail opens the stats screen (`15ab:1afe(t)`, docs/town_loop.md) and
    redraws the screen (`1e31:032f(1)`: the cell window built, the map and the fight's figures
    drawn, windows 4 and 3 cleared and framed), then shows the same figure again. In the Arena (F:e48e) the enemy list jumps
    back to figure 12 after each figure, so the spectator (13) is never shown.
  - Rebuilt (`Town::scan_list`), the people and Detail Scan included (`Town::mech_stats`, then
    `Town::redraw_map_screen(true)`).

The menu's loop, as rebuilt (`Town::orders_menu`): each pass prints the unit's heading in
window 4 (`15ab:18e8`), then either "The computer is moving" (DG:3a42) and the planner with show
(a unit under F:3994) or the unit's screen (`203b:1774`), then the menu in window 3: ten items for a
'Mech (DG:3a6f), eight on foot (DG:3abc). The bar goes on Begin Fight only on the menu's first pass
in a round; after that it stays on the item last chosen. Next Unit puts it back on Next Unit
(item 7, or 5 on foot) for the next unit. With F:0090 set, the first unit loses computer control
on entry, and Computer does nothing.

The unit's screen (`203b:1774(fig, redraw)`):
- With redraw, the view is moved onto the unit (`1000:17bb`) and drawn with the fight's figures
  (`287f:1314`, `18ef`, `1000:0e4b`). Clear Moves shows it again without redrawing.
- The page is copied to the screen (`273d:06c3`), and a box is XORed in colour 15 over the
  unit, row by row (`287f:2b87`): 3x3 cells from (25, 10) for a 'Mech, the one cell (26, 12) for a
  person. In the rebuild the logic gives this box, and the move order's cursor (colour 14, above)
  as what they mark (`XorBox`, `BoxMark`: the unit, the cursor), XORing the marks where
  boxes overlap as the original XORs the colours; the renderer draws them, the original look with
  these colours (`platform/text_layer.cpp`).
- For a party unit its movement points are worked out from the type of its first order
  (`203b:22bc`, or `2474` on foot) and its path simulated (`203b:193b`). Then each step (dx, dy) of
  the path is drawn from the cell (26, 12) on, as the glyph DG:3b06[(dy + 1) x 4 + dx + 1] in
  colour 15 on 0 (`273d:00d5`), until a step of 2. The cell where it ends goes to F:3778/377a.
- Enemy paths are never drawn.
- F:b782 = 1 (not read yet).

After the menu, the player's side (`203b:059b..07ce`): every party unit with orders whose path
is still unworked (its first step 2) has its movement points and path worked out at its own
position; a unit under the computer is planned (`1e31:03ab(fig, 0)`).

The `orders` reference sequence fights mission 2 with Begin Fight and no orders for eight rounds,
then flees, matching the original's screen and state at each menu. The `moves` sequence gives
walk orders with the cursor, changes them to run (the question), tries to jump with a Locust,
and fights three rounds on the run orders.

### 2.7 Fleeing

- Flee chosen: one r; r & 3 not 0 (3 in 4) escapes. The draw is made even where fleeing is then
  refused.
- Kinds 2 and 3: never (the result is set to 0).
- The raid: choosing Flee gives "You cannot escape these Jenners!" (DG:3497). Instead, if figure
  0 is outside the box x 0x0b7f-0x0cff, y 0xb07f-0xcfff at the orders step, the party escapes
  (value 2) without a draw.
- Failed: window 3 is cleared, "You do not escape!" (DG:34b8), "Press a key." (DG:050d, `1000:2a4f`)
  and a key; the round goes on. Kinds 2 and 3 always end here, after the draw.
- Escaped in kinds 1-3: F:3992 = 1 (the mission is failed).
- Computer control with no party 'Mech left: r & 3 (2.5).

### 2.8 What ends a fight

Tested after each round, in this order (a later test can also end it):

| test | ends when |
|---|---|
| win | no enemy figure 12-23 is active (positions ffff are made inactive first) |
| enemy people run (kind 0) | 2.10 |
| mission (kind 1) | figure 0 (Jason's 'Mech) is not active |
| Arena lost (kind 2) | figure 0 not active, the exit zone not reached and flag 23 clear |
| exit zone | flag 23 (F:d32f) set and figure 0 active with x between 0x0900 and 0x0a07 (exclusive) |
| the leader | DG:014a is 0: set by `22e8:12c7` when character 0 or 1 is killed |

A flight ends it at once (2.7), before the round runs.

### 2.9 The double agent

After the round (`203b:08a4`..`0cdb`, right after `22e8:000c`), if DG:014a, flag 27 (F:d333, a
traitor exists) and F:374a are all set, the traitor (character number in flag 25, F:d331) acts.

F:374a means the player has seen through him. Three places set it: the scan list's heading
(`15ab:18e8` with its first flag, in a fight: his name, then DG:1222 in place of his condition,
weapon and armour, and a key), the crew screen's tell (`2c67`, docs/scripts.md) and a third
screen, `1000:378d`, from the Space command menu (`1000:2c50`: its fourth item when the menu has
eight, with 'Mechs, its third when it has seven; DG:07ff; docs/town_loop.md). It is cleared only by a
new game (`1000:4dc7`) and a load (`1000:32b3`), not by a fight's start or end, and it is not in
the save: seen in one fight, he acts after the first round of the next fight he is in.

Window 6 is cleared and framed (`2656:0388`, `2656:0004(0)`), then by where he is
(the character's 'Mech byte, 0x0c):
- on foot: DG:34e9, his name, DG:34f6, DG:3510, DG:3555, DG:358e; mark 0x7e at his figure
  (4 + him), whose position becomes ffff;
- pilot of 'Mech m: his name, DG:35d1, then by the second seat (byte 0x7a):
  - empty: DG:3610, DG:3653; the 'Mech is lost;
  - someone whose Piloting (character byte 8) is 0: that one's name, DG:3666, DG:36a9; that one
    gets out on foot (byte 0x0c = 8, figure 4 + that one active at the 'Mech's position); the
    'Mech is lost;
  - someone with Piloting: that one's name, DG:36f6, DG:3731, DG:3774; that one becomes the pilot,
    the second seat ff, mark 0x7e at the 'Mech's position;
  - the 'Mech lost: record byte 0 = ff, mark 0x80 (0x81 when the figure's sprite set F:d55e + m
    is not 0) at the 'Mech's position (not moved back a cell as a destroyed 'Mech's wreck is,
    3.13), position ffff, figure inactive;
- in the second seat of 'Mech m: the seat ff, mark 0x7e at the 'Mech's position, his name,
  DG:37b8, DG:37fd, the pilot's name, DG:3830, DG:387a.

Then a key (`273d:086a`, `273d:0259`), his record emptied (byte 0 = ff), figure 4 + him
inactive, flag 27 = 0 and flag 24 = 0x7f. The mark is only added to the mark ring: his figure's
frame is not set to the body as a killed person's is. Rebuilt (`Town::double_agent_acts`), as is the
`1000:378d` screen (`Town::inspect`).

### 2.10 Enemy people run (kind 0)

If F:374c was set this round (an enemy person was planned against a 'Mech, 4.4):
- With an enemy 'Mech still active: each active enemy person further than 25 (distance 3.6) from
  its first target is removed. "Some of the enemy humans have fled!" (DG:38bc) if any. No draw.
- With no enemy 'Mech: one r; r & 1 ends the fight with "The enemy humans have fled!" (DG:38e0).

Only in a kind-0 fight that is not already over. The distance is measured from the person as if the
party stood there: the party position is set to each active person's in turn and put back only
after the message and its key, so while it waits the party position is the last active person's. The first target is
the first byte of its target list (F:3800 + 12 x figure), sign-extended: a byte with bit 7 set
indexes before the position arrays (bug 21). A person with no target has ff there (a killed
target's bytes are set to ff, 22e8:12c7), and ff is figure -1: x at F:4002, which nothing writes
(0 from the program image), and y at F:4034, the fourth word of the last kind-2 sound entry
played with sound on (docs/sound.md, which also says how the writes to F:4002 were searched).
The person counts as fled if that point is more than 25 away. Reached: the HUT to ENTRANCE forced
fight has a person with ff here. The fight ends as a fight, not a flight, so 2.15 runs its fought
path. Rebuilt (`Town::enemy_people_run`). Any other byte above 0x7f would read further back,
from memory the rebuild does not hold; it takes the fixed path (not fled) with a log line.

### 2.11 The raid story

In the raid, one story line per round (five lines, far pointer table DG:3a2e), all remaining
lines at once when the fight ends (203b:0f52..0ffb). Each is a message box and a key, and
window 4 (the picture box) is cleared after it. Before the second line, place picture O2 is
shown (`1000:48b7(2, 1)`, one draw). After the third, map 11 (the destroyed Citadel, with its
own tile sheet DESTRUCT.ICN) is loaded into grid 4 (`1000:2da8(4, 11)`), the masks rebuilt
(`287f:1da8`) and 27e8:00fc = 0x0b. The load places the townspeople again (8 draws, none out on
maps above 10) but does not build the cell window again: it keeps its cells until the next
`287f:1314`.

The raid's 'Mech list is fixed: four JENNERs (1.4). The rig plays mission 7 to its end (`raid`,
docs/reference.md): the Locust falls in round 17 of the raid, the remaining lines follow, then
the results (not passed) and the script.

**The saved window.** After a round's movement (22e8:1210..1281, with combat graphics on) the
fight walks the party's place onto the view figure, builds the window there (`1314`) and saves
its 576 cells (`287f:1ece` into 2892:4314). Each redraw during a shot (`1e31:1f09`) copies the
saved cells back, leaving the window's place (P:02cf) and block values (P:07a4) as the last
`1314` set them, often at a figure's place for its line of sight. So the cells can belong to
another block than the place says. Nothing visible depends on it; the rig compares it, and it
shows after the raid's map load, when the saved cells differ from the fresh ones.

### 2.12 After the loop

A walk of the party position to a place (`1000:17bb`: the returns below, and every "the view
moves to a figure" in this document) goes a cell at a time, up or down first, then across,
through the four step functions. So a figure in the next area moves the nine grids over and back
(docs/wilderness.md, "Stepping into the next area"), and the cell window last built stays in
memory as it was (rebuilt, `Town::walk_position`).

- **No fight** (kind 0 with no enemy or none reachable): the view goes back to the entry
  position (`203b:2aa3`).
- **Kinds 0 and 3**, fought, not fled and the leader alive: window 1; "After an invigorating
  scuffle, you defeated the Kuritans." (DG:38fc) if any enemy figure is destroyed; then the
  after-fight steps (2.15), flag 29 = 0, and healing `1c31:000a(0)` if any party character has
  lost hit points (the sum of 10 x byte 1 - byte 0x0f is not 0; healing is below).
  Declined and evaded, or fled: "You have eluded your enemies!" (DG:3936).
  Then, leader alive: the panel, "You return to where you were before combat took place."
  (DG:3954), and the party position is walked back to the entry position, the areas around it
  reloaded (`203b:2835`) and the view drawn.
- **Kind 1** (leader alive, not the raid): "You should return to base now." (DG:398b).
- **Kind 2** (leader alive, not the raid, the exit zone not reached): flag 21 (F:d32d) = 1 and
  "Congratulations! You have won!" (DG:39aa) if Jason's 'Mech exists; otherwise flag 21 = 0 and
  the crowd boos (DG:39ca).
- **The raid** escaped by leaving the area: "You have eluded your enemies!" (DG:39f7).

### 2.13 Always last

For every kind, the townspeople's positions are put back from their records; F:397c + i and
F:396c + i (i 0-7) = ff, that is the facing of figures 16-23 and of figures 0-7; the frame of
figures 16-23 = 0x10. The facing stays ff through a mission's walk afterwards: the walk moves the
townspeople with `1000:24c2`, which sets a facing only for a walker near the party, and the
first town tick after it starts their walking scripts afresh.

Kinds 1 and 2 skip the after-fight steps (2.15): 203b:1051 goes on to them only for kinds 0 and 3.
For kind 1 the ending is "You should return to base now." (DG:398b) and a key, unless the raid
(F:3772) or the fight is over without it (DG:014a = 0).

### 2.14 What the fight writes back

| state | change |
|---|---|
| F:3992 | 1 when the party fled in kinds 1-3 or the computer planned a party person against a 'Mech; read by the mission driver |
| F:009e, DG:2e38, DG:2e3a | the auto-fight answer, the message level, the graphics setting |
| flags | 21 (Arena won), 23 (Arena exit opened, 3.14), 24 = 0x7f and 27 = 0 (traitor), 29 = 0 |
| character records | killed: byte 0 = ff; ejected: byte 0x0c = 8; hit points and armour; skills (3.5); the traitor removed |
| 'Mech records | armour, structure, items, actuators, hits, ammunition; byte 0 = ff when destroyed; the first letter kept in F:323e + m; pilot bytes 0x79 / 0x7a |
| F:d370 | C-Bills from scrap and pockets |
| the mark ring | wrecks, bodies, fires (3.13) |
| P:a44b / a44d | back to the entry position (kinds 0 and 3) |
| F:d390 records | the townspeople's saved positions (+0 / +2) |

### 2.15 The after-fight steps

**Repairs and scrap** (`15ab:0002`, FUN_15ab_0002). Run when the party has a 'Mech, an enemy
'Mech was destroyed and some character has Tech above 0.
1. The best tech: the highest Tech (byte 9) among the party, ties to the lowest index; S is the
   skill, I the character number. Text DG:0efb + name + DG:0efe, plus one of DG:0f9a's phrases for
   S 2-4, + DG:0f28, a key.
2. Armour: the pool is the sum of the wrecks' armour bytes (enemy figures 12-15 destroyed). Each
   location from the rear torsos (0x1b) down to the left arm (0x11), each party 'Mech in slot
   order, gets what it lacks (maximum at +0x45) as far as the pool lasts.
3. S >= 4: the same for internal structure, from the wrecks' structure.
4. S >= 2: every destroyed heat sink (item 0xa2) in a location that still has structure is
   mended (bug 22).
5. S >= 3: destroyed weapons are mended, one for each intact weapon of the same type in the
   wrecks, if the location has structure (bug 23).
6. Scrap money, only if N (wrecks) > 0 and I > 0: M = the sum of N draws of (r & 0x7f) + 90,
   times I (bug 24). Text DG:0f40, the amount, DG:0f65 ("s" if N > 1), a key; C-Bills + M.

In detail: the text goes to the current window (window 1, from the ending); the phrase for S is
the far pointer at DG:0f9a + 4 x S, so S 2-4 give DG:0eae, 0ebe and 0ed7, and S above 4 would read
the pointers past them (the rebuild logs that and stops). The pool loops go byte by byte from the
last location down, and within each byte through the party's 'Mechs in slot order. The lack is
the maximum less the byte, as unsigned bytes in a signed word: a byte above its maximum is
lowered and the pool grows. Heat sinks are counted (item 0x22 in a wreck's slots) but not
checked. Weapons count only intact items 0x10-0x20 in the wrecks; a party slot whose item has
bit 0x80 and code 0x10-0x20 is mended (bit cleared) while the count for its code is not 0 and
its location has structure (`203b:273d`: the location's structure byte, or 1 for model 200; the
slot ranges are those of docs/formats/mech.md). The amount is printed in light green, and the
colour stays light green for the rest. Rebuilt (`Town::repair`).

The counts are bytes at [bp-0x34 + item]; the clearing loop zeroes [bp-0x24]..[bp-0x15] (items
0x10-0x1f), so item 0x20's count is [bp-0x14], never written by 15ab:0002 before the loop. At the
function's own key wait (`1000:2a4f`) the word there is DGROUP's segment number, left by an
earlier call from the caller (`203b:116e`) at the same depth (the word below it is 7246, so
most likely a pushed far pointer to DGROUP:7246; the call is not identified). The count starts
at that segment's low byte. Under the rig (DOSBox-X with SNAP resident) DGROUP is 47e9 and every
capture of the repairs' key wait has 0xe9 there: 20 captures, six fights, techs of skill 2-4.
Another load address gives another byte; any value at least the number of destroyed SRM 6s
gives the same result, so in practice every destroyed SRM 6 in a location with structure is
mended. With `fix_weapon_count` off (the default) the rebuild starts the count at 0xe9, and
`chain_flee` matches the original: 'Mech 0's SRM 6 (slot byte 0x53) is mended in both
(docs/reference.md). On, the count comes from the wrecks only.

**Salvage** (`15ab:04f9`, FUN_15ab_04f9). Run when a 'Mech figure was destroyed on either side, a
party character with Piloting above 0 is on foot, a party 'Mech slot is free, and none of the
nine bytes saved in 2.1 has bit 0x80. For each such character, the question DG:0fae (yes,
default). Yes: the best tech inspects (wait 120 frames); a wreck is picked: slot = (r & 3) +
12 x (r & 1), then the first slot from there (wrapping 15 to 0) whose destroyed flag is set; the
flag is cleared. It can be salvaged unless (engine hits 3, or gyro hits 2, or head structure 0)
and S < 4. Salvaged: the first free party slot gets the record (first letter from F:323e),
pilot = the character, engine and gyro hits = 1, centre torso and head structure at least 1,
sprite base 0 for an 'L', else 0x92 (bug 25). Not salvageable: DG:102f, and a new pick. No wrecks
left: DG:104c and it stops. See bug 26. Each salvaging message is the template's text without
its last character, then the record's first letter (F:323e) and the rest of its name, then
`1000:2a7e`; a salvaged wreck, or none left, waits for a key (`1000:2a4f`). One wreck at most per
character: with wrecks left the loop goes on to the next character, with none it ends.

**Loot** (`15ab:094b`, FUN_15ab_094b), always on this path. If any figure 4-11 or 16-23 is
marked destroyed: "You dig through the soldiers' pockets and find" (DG:1085) a sum of (r & 0xf)
+ 3 per beaten enemy person (at least 2 in all) C-Bills. If weapons were found (bug 27), each
beaten enemy person's weapon is offered: when Jason is alone, as a swap for his own (DG:10e5,
default no); otherwise through `17dc:15e6`.

In detail: window 1 is selected, cleared and framed (`2656:0004(0)`); the pocket text; the sum
in light green (`1000:28a2`), then colour 15 and DG:10b5; the sum is added
to the C-Bills (F:d370) without redrawing them; the weapons line (DG:10c6) if any were found;
"." (DG:051d, `1000:2a7e`); the message pause and a key (`273d:086a`, `0259`). Then, only if
weapons were found, for each beaten enemy person 0-7 in order: with a second party member,
`17dc:15e6` with that person's weapon (below); with Jason alone, the window is cleared and,
if the person's weapon is not 0, the swap question (DG:10e5), Jason's weapon's name, DG:10ff, the
found one's name, DG:1113, and a yes/no with No first; Yes swaps them. The
weapons test reads character s + 12 for figure s + 12 (s = 4-11), past the sixteen records into
'Mech record 0, whose name and armour bytes are seldom 0: so Jason is usually offered a swap even
when the soldiers carried nothing. Rebuilt (`Town::loot`).

**A found weapon offered to the party** (`17dc:15e6`, FUN_17dc_15e6; its argument is a weapon
number w). The party is the character records 0-7 whose byte 0 is not ff, in order: n of them.
With n = 1, record 0's weapon (byte 0x0b) becomes w and it ends (the loot never calls it so).
Otherwise, while w is not 0:
1. The current window is cleared (`2656:0388`); "Give " (DG:16ae), w's name (the weapons table
   at DG:2ed8), " to:" and a line end (DG:16b4).
2. For each member: his name, the cursor to column 10 (F:3748), his weapon's name, a line end
   (`24d3:181e`). Then "Drop it" (DG:16ba).
3. Menu 4's item count (F:00d6) is set to n + 1, and menu 4 runs (`2656:0b5e`).
4. The last item (Drop it): it ends, the weapon is left. A member: his weapon becomes w. If it
   already was w, "He already has that weapon." (DG:16c2) is printed at column 0, row 20 in
   white (`1000:2867`), then the message pause and a key (`273d:086a`, `0259`). Either way his
   old weapon becomes w, and the loop offers it next: so the old one goes round until someone
   is given a 0 (no weapon) or it is dropped. A member who already had w gets w again, and w is
   offered again.
For the player: the menu starts on the member last chosen, so Enter after Enter gives every
weapon to the same member, and when he already had a found weapon the two go back and forth
for ever. Choosing someone else, or Drop it, ends it. Seen on the original (docs/reference.md,
chain_party, and Edward's Rifle and Crossbow in chain_money_1_swap); this is how the offer
works, not a fault in it, so it is not in the bug list.

**Healing** (`1c31:000a`, FUN_1c31_000a; its argument is a doctor's level, 0 after a fight). With
flag 29 set it only opens the message box with "Your characters need more time to recover."
(DG:25a4) if anyone is hurt, otherwise DG:25cf, and waits for a key. Otherwise:
- The medic is the first present character (records 0-7) with the highest Medical skill (byte
  0x0a, signed); Jason and 0 if nobody has any.
- Special 23 sets flag 19 if anyone's hit points (byte 0x0f) differ from 10 x B (byte 1). If
  nobody is hurt: with no doctor, the message box, DG:2591 and a key; with a doctor, nothing.
- Window 1, framed (style 1) and cleared. The care is 1 (torn cloth strips), 2 with a MedKit
  (F:d450), 3 with a field surgery kit (F:d451) and skill 3 or more. A doctor sets the care to 2,
  or 4 above level 2, and the skill to the doctor's level, and the line starts DG:252d instead of
  the medic's name.
- With skill: DG:2538, the skill's name (far pointers at DG:25de + 4 x entry; the entry is the
  skill below 3, the skill - 1 for 3 and 4, and 4 from 5 on: so 1 the first text, 2 and 3 the
  second, 4 the third, 5 and above the fourth), DG:2543 and the care's name. Without: DG:2553. Then DG:256f.
- In light green, for each hurt member: a line end, the name, DG:2587, and the hit points go up
  by dice from DG:2601 + 4 x skill + care: the low nibble is a count of d6 (`1000:19f3`: r & 7,
  drawn again above 5, plus 1), and a high nibble, when not 0, multiplies their sum. Never above
  10 x B. Skills above 7 read past the table, into the text after it.
- Colour 15, the message pause, "Press a key." and a key, the panel (`4cac(0)`), flag 29 = 0x3f.
  The panel's bars show the hit points from before the healing until that `4cac(0)`.

F:d450 and F:d451 are `Town::kits_`: 0 at a new game, read back by a load, and set by nothing
else rebuilt yet (two of the specials, 24d3:0dc6 and 0dd3), so the care is 1 unless a doctor treats or a
loaded game has a kit. Rebuilt
(`Town::heal`; special 23 `Town::anyone_hurt`); the doctor's callers are not.

## 3. One round: `22e8:000c`

FUN_22e8_000c, 4795 bytes. The argument is 1 for kinds 1-3. It reads no keys except the message
pause.

### 3.1 Shape

A round is **12 phases**. In each phase, unless DG:014a is 0 (the fight is over):
1. **Movement**: every active figure, in slot order 0-23, takes at most one step of its path.
2. If anyone moved (always in phase 0) and graphics are on, the view is centred on Jason's figure
   and redrawn.
3. **Firing**: each active figure in slot order 0-23 tries its weapon slots 0-11 in order; the
   first one that fires ends that figure's turn for this phase. Each weapon fires at most once a
   round. DG:014a is checked before each figure.
4. **Waypoints**: for party figures 0-11 only, if the first order's place equals the figure's
   position, the order list moves up by one (the next leg's type now applies). Enemy orders are
   never moved up.

At the end, the party position is put back and `1e31:0c63` runs (3.15).

So in each phase the party fires first (party 'Mechs, then party people), then the enemy, and a
kill takes effect at once: later shots at a dead figure are cancelled.

### 3.2 Movement points

**A 'Mech** (`203b:22bc(m, type)`, FUN_203b_22bc; type 0 walk, 1 run, 2 jump, -1 none = walk):
1. MP = walking MP (byte 0x31). Jump: MP = jumping MP (byte 0x32) and go to step 6 (no leg or
   heat penalty).
2. Hips: bit 3 of the two leg nibbles (bytes 0x24 and 0x25). Neither: MP = 1, skip step 3. One:
   MP = half the walking MP rounded up.
3. Actuators: for each of bits 2, 1, 0 (then left, right), a missing bit costs 1 MP. If MP is now
   exactly 0, it becomes 1 (bug 6).
4. Heat: MP -= heat / 5 (truncated).
5. Run: MP += MP / 2 (rounded down), + 1 if the undamaged walking MP is odd (bug 7). An undamaged
   'Mech runs ceil(1.5 x walk).
6. Heat 30 (shut down): MP = 0. MP is at least 0.

F:377c is set when legs reduced MP, F:4592 holds the heat loss (for the "inhibited" texts).
Example: the Locust walks 8, runs 12.

**A person** (`203b:2474(n)`): MP = 3 x byte 2 / 4 (rounded down), at least 3; halved if the
armour type (byte 0x0d) is above 1 (the heavier suits); at most 8. For n 12 and above: a flat 6
(bug 8).

### 3.3 The path and its cost

`203b:193b(fig)` (FUN_203b_193b) simulates the orders from the figure's position and writes the
path (up to 12 steps of one half-cell each, F:40b4) and the points left (F:3770):
- Each order in turn: if the figure is free to turn (a party figure not under computer control,
  or one with no heading yet) it faces the waypoint at once; otherwise the stepper turns it one
  eighth per step.
- While points remain: stop the order at the waypoint, or when the waypoint is taken by another
  figure. Otherwise one step of `1e31:0006` (a blocked step stays in place) and its cost.
- **Cost of the half-cell stepped onto**: 1. Not jumping and the tile below 0x40: if the tile's
  low four bits share a bit with the quarter of the cell the half-cell is in, 2, and 3 if the
  tile's group (high nibble) is 2. Tiles 0x40 and above cost 1; jumping always costs 1. The low
  four bits seem to say which edges of the cell the terrain covers (bit 0 top, 1 right, 2 bottom,
  3 left); this reading is a guess, the numbers are exact.
- A step is taken whenever at least 1 point is left, so the last step can overspend. A blocked
  step still costs its cell, so a blocked unit spends its points standing.
- The heading is put back afterwards: simulating does not turn the figure.

### 3.4 Moving

For each active figure, the next step of its path (F:0078 counts the steps taken):
- the end mark (2): nothing;
- (0, 0): nothing, and the counter does not move on, so the figure stops for the rest of the
  round (`1c67:0d7e`, which zeroes every path, stops everybody this way);
- the place taken (`1e31:16ab`): no move, and the same step is tried again next phase;
- otherwise the figure moves, its heading follows the step (0 north, clockwise), its walking
  script changes when the direction does, and its "steps so far" count goes up.

There are no draws in movement.

**Crushing.** The moving step's place test (`1e31:16ab`) is the only one that crushes; the
planner's path test (3.3) does not. For a 'Mech stepping, after the tests that refuse the place
(another active 'Mech on the row within 2 half-cells, one of its own side's people within 1 of
its middle), every active person of the other side on the same row within 1 half-cell of the
'Mech's middle is crushed: their name (a party member) or the enemy-person word, then
`shot.squashed`, sound 0x12, a pause (two for a character record below 8), map mark 0x7e, frame
0x7e, sprite base 0, the figure inactive, the character's hit points 0 and its record emptied.
Crushing character 0 or 1 ends the fight, as their deaths do (open question 6). The 'Mech then
takes the step. `chain_crush_poked` (docs/reference.md) checks it against the original.

### 3.5 Choosing a shot

For figure s and weapon slot w (0-11), in this order:
1. The target is the signed byte F:3800 + 12s + w (ff none; bit 7 = already fired this round).
   A 'Mech at heat 30 or more has no target.
2. The weapon: a person always uses its character's weapon (byte 0x0b), for every slot. A 'Mech's
   slot 11 is the Kick (weapon 0x20), allowed only with both hip actuators (otherwise the slot's
   target is cleared); slots 0-10 are its weapons in item order (`1e31:10a2`: none, or no
   ammunition left, clears the target). A destroyed weapon is skipped.
3. A target that is no longer active is cleared.
4. The range band (3.6). Out of range: skip; the target is kept and tried again in later
   phases, and the figure may still fire another weapon this phase.
5. The direction from the attacker to the target. Line of sight (`1e31:1bfe`): blocked, skip.
6. From here the weapon fires.
7. **Skill practice** (party people, figures 4-11 only): F:d360 + s holds the skill last used and
   F:d358 + s a count of shots with it. Same skill: the count + 1, and when it wraps from 255 to
   0 the skill goes up by 1 if below 4. Another skill: the count starts again. (These bytes lie
   in the flag array, flags 0x50-0x5f.)
8. **Ammunition** (a 'Mech's slots 0-10): the slot's ammunition byte, unless ff (unlimited), goes
   down by 1, before the hit roll.

### 3.6 Range

**Distance** (`1e31:0bb5`, FUN_1e31_0bb5), from the party position, in half-cells on the linear
grid: d = min(|dX|, |dY|) + max(|dX|, |dY|) / 2 (rounded down). A straight line of 10 half-cells
is 5; a diagonal of 10 by 10 is 15. See bug 1.

**The aim point** (`1e31:0f24`): for a 'Mech target more than 3 away, the aim point moves one
half-cell toward the shooter in x, and 2 up if the shooter is above it (a 'Mech is three
half-cells tall).

**Bands**: S and M from the weapon's +0x0e byte, L its +0x0f (formats/mech.md). For a 'Mech
weapon other than the Kick, S and M are multiplied by 3; L never is. Short if d < S, medium if
d < M, long if d < L, else out (band 3). The Medium Laser: short below 12, medium below 21, long
below 30. A pistol: 3, 5, 9. The weapon's minimum range (the high nibble of +0x0d) is not used.

### 3.7 The to-hit number

The shot hits when 2d6 >= T. T is built in this order:

| step | term |
|---|---|
| 1 | 4 + 2 x band (short 4, medium 6, long 8) |
| 2 | Kick: T is replaced by 3 + the number of missing actuators among bits 0-2 of both leg nibbles (bug 2) |
| 3 | + the attacker's movement: the type of its first order + 1 (none 0, walk 1, run 2, jump 3) |
| 4 | + the target's movement: DG:2d1a[steps the target has taken so far this round] (0 for 0-2 steps, up to +4 for 10-12) |
| 5 | attacking 'Mech: + 2 if it has any sensor hit (byte 0x77); + 1 for each heat level reached: 8, 13, 17, 24. The weapon's heat (low nibble of +0x0d) is added to F:0092 + m here. |
| 6 | - the shooter's skill: character byte 4 + the weapon's skill number (+0x10). A 'Mech's shooter is its pilot (byte 0x79). |
| 7 | + the target's terrain F:32ae: >> 3 for a 'Mech target, >> 1 for a person (signed) |

There is no clamp: T 2 or less always hits, T 13 or more never.

The skill numbers: 0 Bow & Blade, 1 Pistol, 2 Rifle, 3 Gunnery (the 'Mech weapons, the people's
missiles), 4 Piloting (the Kick).

Because phases interleave, a target that has taken few steps so far is easier to hit early in
the round. Because party orders are moved up at each waypoint (3.1), a party 'Mech that has
reached its last waypoint has no orders and counts as standing still for step 3 (open
question 8).

### 3.8 Combat messages

`1e31:1dab` prints a text into the current window only when DG:2e38 (the combat messages level:
0 None, 1 Brief, 2 Verbose) is not 0. Some pieces are first copied into a buffer (DG:0012) and
printed as one string, which matters for the word wrap. The line, before the skill is taken
off the to-hit number:

1. With messages on: window 4 is made current and cleared (`2656:0281(4)`, `2656:0388`).
2. The colour (F:37fe) is 15 for a party shooter, 14 for an enemy, whatever the level.
3. The shooter. Verbose only, except the Kick:
   - a party 'Mech: its pilot's name, "'s Mech" (DG:3e44), then "uses a" (DG:3e58), or for the
     Kick the buffer "kicks" (DG:3e4d; Brief: "Kick", DG:3e53), at both levels;
   - a party person: the name, "uses a" (DG:3e60);
   - an enemy: "An enemy" (DG:3e69), then "Mech" (DG:3e81) and "uses a" (DG:3e87) or the Kick's
     buffer (DG:3e8f / DG:3e95), or "human uses a" (DG:3e73).
4. The weapon's name (its record's first bytes), unless it is the Kick: at both levels.
5. Verbose only: " on " (DG:3e9a) and the target: a party 'Mech, its pilot's name and "'s Mech"
   (DG:3e9f) and "." (DG:3ea7) in one buffer; a party person, the name and "."; an enemy, "an
   enemy" (DG:3ea9) and "Mech." / "human." (DG:3eb3 / DG:3eb9); the Arena's spectator, "a
   spectator." (DG:3ec0) instead.
6. The result, at both levels: a 'Mech target "Missed!" (DG:3ede), or "Hit " (DG:3ee7) and then
   `1e31:1b8f`'s buffer, the location's name (found through DG:3242, names at DG:324e) and "."
   (DG:3240); a person "Missed!" (DG:3ecd) or "Hit!" (DG:3ed6). A personal weapon at a 'Mech
   prints nothing (bug 5).
7. A critical hit (3.12) makes window 4 current without clearing it and adds "Critical!"
   (DG:315e) after the line. A 'Mech destroyed (3.14) does the same with light green (colour 10,
   `1000:28a2`) and "Mech is destroyed!" (DG:2b6d), then the colour goes back to 15.

`1e31:1dcc` pauses after a shot: 12 x DG:015e frames, or until a key when DG:015e is 5 or more.
Whatever the level, so a fight with messages None pauses as long.

### 3.9 The hit location

Only for a 'Mech target. The target's facing is F:396c + t, or F:45b6 + t when that is ff (the
facing is parked there while a figure shoots). d = facing - direction (attacker to target),
taken mod 8, picks a row of the location table through DG:2d11:

| d | row of DG:2e40 | that row matches the board game's |
|---|---|---|
| 0 | 2 | rear column |
| 1, 2 | 1 | front column |
| 3, 4, 5 | 0 | left side column |
| 6, 7 | 3 | right side column |

The location is DG:2e40[11 x row + 2d6]. One 2d6 is drawn for every shot at a 'Mech, hit or
miss, even for weapons that do nothing to 'Mechs. The rows are not split evenly (one direction
gives the rear, three the left side); see open question 4.

### 3.10 Damage to a person

Target figure 4-11 or 16-23.
- Damage D and shots: a personal weapon (+0x0c bit 7) does D = the low nibble of +0x0b plus
  (high nibble) d6 (the dice are drawn before the hit roll), with +0x0c & 0x7f shots. A 'Mech
  weapon (and the people's missiles) does D = 127 per shot, with +0x0c shots (an LRM 20: 8). D
  is at least 1. The Rifle: 3d6; the Pistol: 3 + 2d6.
- The hit roll (2d6 >= T). A hit means every shot hits.
- Each shot: armour a (byte 0x0e). If a >= D / 2: D is halved and a -= the halved D. Otherwise
  D -= a and a = 0. Then hit points (byte 0x0f) -= D, to 0 at most. D is not reset between shots
  (bug 4).
- Hit points 0: killed (3.14).
- A miss may start a fire (3.13).

### 3.11 Damage to a 'Mech

Target figure 0-3 or 12-15 ('Mech m = t or t - 8).
1. The location 2d6 (3.9).
2. **The Kick**: D = the kicker's tonnage (byte 0x10) / 5, truncated (written into the Kick's
   damage byte, DG:3103). One r: r & 8 = 0 the left leg, else the right leg; the table result
   is replaced.
3. **A personal weapon**: nothing more. No hit roll, no damage, no text (bug 5).
4. **Clusters**: if +0x0c is above 1, a 2d6 c, and D = +0x0b x DG:2e5e[7c + +0x0c]. The whole
   cluster lands on the one location. An LRM 20 does 6-20, an SRM 2 1-2 (x 2).
5. **The hit roll** 2d6 >= T. A miss: D = 0, a fire check (3.13), and if the target stands on a
   blocking tile (F:45ce + t >= DG:0150) a mark 0x7f is added just above it (a shot into the
   building behind, a guess).
6. **Damage** while D > 0 and the 'Mech is not destroyed:
   - The Inferno (weapon 0x0b): no damage; the 'Mech burns for 3 rounds (F:d576 + m = 3, 3.15).
     It goes on through the step below with D = 0, so the location still gets its critical
     check.
   - D not more than the location's value v: v -= D. A location that is internal structure
     (0x1c-0x23) gets a critical check (3.12). Head or centre torso structure now 0: the 'Mech is
     destroyed (`1d43:0a35`), and if it is figure 0 in a fight of kinds 1-3, every path is
     cleared (`1c67:0d7e`; bug 33).
   - D more than v: D -= v, v = 0; a critical check if internal; then D moves on (transfer):

| from | to |
|---|---|
| armour of a location (0x11-0x18) | its internal structure |
| rear armour LT, CT, RT (0x19-0x1b) | that torso's internal structure |
| arm or leg structure | the side torso's armour (left to LT, right to RT) |
| side torso structure | the centre torso's armour |
| head or centre torso structure | the 'Mech is destroyed |

   - An arm whose structure is used up exactly or emptied this shot sets F:3986 (a picture, 3.14).

### 3.12 Critical hits

`1e31:11ab(m, L)` (FUN_1e31_11ab), for every internal location damaged:
1. 2d6: below 8, 1 critical; 8-9, 1; 10-11, 2; 12, 3 (bug 3). With combat messages on only:
   "Critical!" (DG:315e) and F:4586 = 1 (so the picture after the shot needs messages too).
2. If the location's structure is now 0: every item in its slots is destroyed, and an arm or leg
   loses all its actuators. No more draws.
3. Otherwise, until that many criticals have landed (a pick that finds nothing draws again):
   - Head: d6. 1 or 6: byte 0x78 = ff if not 0 (life support, a guess). 2 or 5: sensor hits + 1
     (at most 2). 3: the cockpit: head structure 0, the 'Mech destroyed. 4: the head's item slot.
   - Centre torso: d6 above 3: one of its two item slots. Otherwise another d6: above 3, engine
     hits + 1 (3 destroys the 'Mech); else gyro hits + 1 (2 destroys it). Destroyed here means
     centre torso structure 0.
   - Arm or leg: nothing left: done. d6 v of 3 or more with actuators left: actuator v (leg bits
     8, 4, 2, 1; arm 0x80, 0x40, 0x20, 0x10, tables DG:316a / DG:317a) is cleared, or the pick
     fails if it is already gone. v below 3 or no actuators: an item slot.
   - Side torso: no intact item: done; else an item slot.
   - An item slot pick (`1e31:163e`): if any item in the range is intact, one r: r & 3 is the
     starting slot, and the first intact item from there (wrapping) is destroyed.

There are no ammunition explosions: a critical on an ammunition slot destroys the item like any
other, and nothing else reads the ammunition bytes (0x27-0x30) when a slot is hit.

Open: the life-support critical (byte 0x78 = ff) is now reached against the original
(`chain_crit_poked`, docs/reference.md), but what reads byte 0x78 afterwards, and to what
effect, is not yet checked.

### 3.13 Fires and map marks

On a miss only (`22e8:1e46`): if the target's terrain byte F:32ae is set, the weapon is the
Inferno, the Medium Laser, the Large Laser or the PPC, and the tile under the target (F:3750) is
0x10-0x3f: 2d6; 2-4 or 10-12 (1 in 3) adds mark 0x7c (fire, a guess) one half-cell left of a
person or above a 'Mech.

Marks go into a 64-entry ring (`203b:27c9`: F:d457 code, F:d497 area, F:d4d7 x, F:d517 y; write
index F:d557, the oldest overwritten). Codes: 0x7c fire, 0x7e a body, 0x7f a missed shot into a
building, 0x80 / 0x81 a wreck (two sprite sets). `1000:2a93` draws them (docs/town_loop.md): for each
entry, column x - party x + 0x1a and row y - party y + 0x0c (16-bit positions, the area in
the high byte); skipped past -0x73..0xa7 or -0xf80..0xf98, or, in the party's own area, outside
the view (columns 0x0d..0x27, rows 0..0x18); then drawn at column & 0x7f, row & 0x7f (8 pixels
each). The & 0x7f folds the area gap out, so a mark one half-cell into the next area is drawn
one half-cell from the party.

Every figure on the map is placed by the same rule. The people of `1000:051b` (townspeople and
the figures beside them) and of the fight view `1000:0e4b` use the same view test (in the
party's own area, per axis) and wide ranges, then fold. The fight's 'Mechs (`0e4b`, second
loop) use columns 0x0b..0x27, rows 0..0x1a and -0x75..0xa7, -0xf80..0xf9a, then fold, and
draw two half-cells higher. The bays (`1000:0d30`) test the view in every area, then the wide
ranges, then fold. A figure that has crossed into the next area is drawn beside the party,
not at its place within its own area.

### 3.14 After the shot: kills, wrecks, pictures

`22e8:12c7` (FUN_22e8_12c7):
- **Presentation** (only when DG:2e3a, combat graphics, is set and DG:014a). Arguments: shooter
  s, target t, weapon w, direction d (shooter to target), hit, killed, destroyed, character c.
  1. The view's centre is Jason's figure (F:c620, his 'Mech slot; a value above 7 means on foot,
     figure 4).
  2. The shooter's fire pose: a figure script (F:01f6 + 4s) from a table by direction: a 'Mech
     DG:2d58 (the Kick DG:2d98), or DG:2d78 / DG:2db8 when its sprite set F:d55e + s is not 0;
     a person DG:3ff8 + 0x20 w + 4d. Its facing F:396c + s, unless ff, is parked in F:45b6 + s,
     and a 'Mech's walking frame (F:409a + s) is kept to be put back at the end.
  3. The target's reaction, only when a person is hit (22e8:000c sets the flag only there) and
     not by missiles (w 0x0a-0x0b, 0x19-0x1f): the script DG:2e3c (it loops), its facing parked
     likewise. Its facing F:396c is left as it was, so the walk after it carries on in the
     reaction's loop if the person walks the way it last walked.
  4. Until the shooter's script reaches its end (ff): both figures take their next frame
     (`1000:1732`), and if the shooter is on screen (F:42f6 + s, set by the figure drawer) the
     view is redrawn (`1e31:1f09`), shown (`273d:06c3`) and held 5 frames (`273d:0006(5)`).
  5. A sound by weapon (`1000:19bf`): w 0-2: 0x0d; 3: 5; 4-6: 0x0b; 7-8: 6; 9, 0x13-0x17: 3.
  6. The shot itself, when either figure is on screen. Kind 1, the missiles (w 0x0a-0x0b and
     0x19-0x1f, sound 1 first): a sprite (frames from `1000:1732` + 0x68, script DG:2dd8 + 4d)
     moved along the line four pixels a step, each step inside the map window redrawn (`1e31:1f09`),
     drawn (`1e31:1f73`) and shown (on the Tandy, display 1, only every fourth). Kinds 2 (w 0x0c-0x0d) and 3 (w 0x0f-0x12, the lasers and the PPC): the
     line plotted pixel by pixel (`273d:031c`), from the figures' screen places (F:324c / F:327c
     + 2s, or worked out by `1e31:1df8` for one off screen) moved by 3 and by a muzzle offset by
     direction (DG:2d28/2d30 a 'Mech, 2d38/2d40 with the other sprite set, 2d48/2d50 a person),
     the target's 8 higher for a 'Mech, clipped to the map window (x 0x68-0x13f, y 0-199); the
     steps from DG:41dc-41f4 by the octant (`1e31:1ea2`). The line's colour: kind 2 14 (10 for
     an enemy person), kind 3 12 for a party shooter and 5 for an enemy (on the CGA 3 and 2).
     Then a sound: 9 after kind 2, 2 after kind 3. In the rebuild the logic sends the line as an
     event (`ShownLine`: from, to, what it is, a hand laser or a 'Mech's beam, who fired it, and
     the weapon), and the renderer draws it: the original look walks it with these steps and
     colours (`platform/map_layer.cpp`), so a makeover can draw a beam its own way.
  7. A miss: the view is redrawn. A hit: sound 4; for a weapon above 6 with the target on
     screen, an explosion (script DG:41d8, frames + 0xfa, drawn over the target until its end),
     5 frames held, and a place picture (`1000:48b7(n, 0)`, one draw) if the weapon calls for
     one: 1 for the missiles at a 'Mech, 4 (7 with the other sprite set) for a party figure's
     lasers or PPC, 3 for an enemy's when the target is figure 0 and the shooter's sprite set is
     not 0.
  8. A 'Mech shooter's walking frame is put back; a person's stays its pose's last. At the very
     end of the function, F:396c + s = ff (the facing is read from F:45b6 + s from then on, 3.9).

  Every redraw here (`1e31:1f09`) puts back the cell window saved at the round's last redraw
  (`287f:1ece`: the window around Jason, saved after the movement in phase 0 and in every phase
  where a figure moved), walks the party's place onto Jason's figure (`1000:17bb`), steps the
  animated tiles (`1000:240b`) and draws the view and figures (`287f:18ef`, `1000:0e4b`). The
  firing code puts the party's place back after each shot, so of all this only the drawer's
  writes and the tiles' frame remain. A line or missile from or to a figure off screen starts or
  ends at its place worked out from Jason's (`1e31:1df8`); it is drawn only when either is on
  screen, and not at all when the two are within 8 pixels.

  **The figure drawer** (`1000:0e4b`) first draws the map's marks (`1000:2a93`, a fire changing frame), then for every figure in view (people, then 'Mechs): F:42f6
  (on screen, all 24 cleared first; in the town F:42f6 + i is townsperson i's "near the party"
  flag, `1000:24c2`), F:324c / 327c (its pixel place), F:3750 (the tile under it), F:32ae (the
  rows it sinks: a person 2, 4 in deep water; a 'Mech 8, 16), and for a 'Mech F:45ce (the tile
  one cell up, for the missed-shot mark) and F:4554 (1 in its cell's other half-row; both
  cleared first for every 'Mech that exists). A figure out of view keeps its old values.
- **A person killed**: "Killed him!" (DG:41fc); destroyed, inactive, the record emptied, a body
  mark 0x7e; every target byte aimed at him cleared. Character 0 or 1: DG:014a = 0, the fight ends
  at once (open question 6).
- **A 'Mech destroyed** (`1d43:0a35`): inactive, destroyed flag, its first letter kept in
  F:323e + m, record byte 0 = ff, targets at it cleared, "Mech is destroyed!" (DG:2b6d). A party
  'Mech: "Men eject!" (DG:2b81, always, even when nobody is inside); its pilot and second
  occupant go on foot at its place (the second one a half-cell right). Enemy crews do not
  eject. A wreck mark 0x80 / 0x81 one half-cell up and left, sound 8.
- **The spectator** (F:e48e, figure 13): always in line of sight, never targeted by the computer,
  no "destroyed" text. Destroying it: a message box (DG:4209, a key), then DG:4235, 'Mech
  records 6 and 7 become URBANMECHs (27e8:065b) as figures 14 and 15 at x 0x097d, F:e48e = 0,
  flag 23 = 1 (the exit zone opens, 2.8). Destroying 'Mech 4 also removes 'Mech 5 (figure 13);
  destroying 'Mech 5 patches the map (`1d43:0c72(0)`).
- After the shot: if an arm was destroyed and graphics are on, picture 0x10 (one draw); if a
  critical hit Jason's 'Mech and graphics are on, picture 5 (one draw). The pause, the target
  byte gets bit 7 (fired).

### 3.15 The end of the round

`1e31:0c63` (FUN_1e31_0c63):
1. Every target byte: bit 7 cleared; a target no longer active is cleared.
2. **Heat**, for each 'Mech that exists (party figures 0-3, enemy 12-15):
   h = movement (first order type + 1; a jump of more than 3 steps counts the steps)
     + 5 x engine hits (byte 0x75)
     - heat sinks in the engine (byte 0x26)
     - intact heat sink items (0x22)
     + the weapon heat of this round (F:0092 + m).
   Heat += h. Burning (F:d576): + 6 and one round fewer. Standing in water (terrain byte set and
   the tile below 0x10, a guess): - 4. At least 0. The cap at 30 tests party record a (the loop
   index), not the record just heated, so an enemy 'Mech's heat has no cap (bug 35). F:0092 = 0,
   even for a record that does not exist.
3. Characters whose record is empty (dead): the figure position ffff (not for character 0);
   enemy people's sprite base fe; character 0's figure keeps sprite base 0x96.

Heat 30 is the only effect beyond the to-hit and MP penalties: the 'Mech cannot move or fire
until it cools. There is no shutdown or ammunition roll.

## 4. The computer planner: `1e31:03ab(fig, show)`

FUN_1e31_03ab, 2058 bytes. It plans one figure for either side. **It draws no random numbers:
the planner is deterministic.**

1. **Set-up**: the party position goes to the figure; its orders are cleared. A party figure gets
   F:3994 = 1 (the computer now moves it).
2. **Mode**: a 'Mech runs if its heat is 0, else walks; never jumps. People walk. Movement points
   as in 3.2 (bug 8 for enemy people).
3. **Target**: the nearest active figure, from lists tried in order, a later list only if the
   earlier ones gave nothing:

   | planning | first | then |
   |---|---|---|
   | a party 'Mech | enemy 'Mechs 12-15 | enemy people 16-23 |
   | an enemy 'Mech | party 'Mechs 0-3 | party people 4-11 |
   | a party person | enemy people 16-23 | figures 12-19 ('Mechs and people) |
   | an enemy person | party people 4-11 | figures 0-7 ('Mechs and people) |

   Ties keep the earlier figure. Never the spectator. The enemy ignores the traitor on foot.
4. **Reach R**: a person: the short limit S of its own weapon. A 'Mech: 4 x the smallest short
   limit among its intact weapons (bug 9; quirk 10). Then line of sight to the target.
5. **A person facing a 'Mech** runs: F:3992 = 1 for a party person, 0 for an enemy one (bug 13);
   F:374c = 1 for an enemy person (2.10). The waypoint is 6 half-cells away from the target on each
   axis (unchanged on an axis where they are level). It still fires at the target.
6. **Movement order** (only the first order is written, one waypoint):
   - R = 0 or no line of sight: straight to the target's position (the stepper stops next to it);
   - farther than R: a stop point, d - R diagonal or straight steps from the figure in the
     target's direction, the direction fixed once (quirk 11);
   - in reach with line of sight: no order.
7. **Weapons**: a person: its one slot at the target (not the traitor). A 'Mech: in contact with
   an enemy 'Mech it can see (|dX| <= 3, |dY| < 2): it stands, faces the target and its facing
   is parked. Shut down (heat 30): no order, nothing fires. Otherwise every weapon slot 0-9 that
   has ammunition gets the target, and the Kick (slot 11) always (the round drops it without hips).
   Slot 10 is never set. Range band, heat and minimum range are ignored: it fires everything.
8. The path is simulated (`203b:193b`) and, with show, drawn.

`203b:1482(first)` plans the twelve figures from `first` (0 or 12) that are active.

## 5. Every random draw, in order

Draws are r unless written d6 or 2d6 (each of those is one or more raw draws). The generator and the game's other draws are in
docs/random.md.

**Before a random encounter**: the game tick's r (1.1).

**The encounter set-up** (`15ab:0d3d`, kind 0):
1. dx, its sign, dy, its sign: four r.
2. For each enemy character 8-15: r (& 1); if a soldier: 7 r (weapon), 2d6, 2d6, 7 r (skills),
   r (armour type), 2d6, 2d6.
3. For each enemy 'Mech 4-7: r (& 1); if 1 and the matching party 'Mech exists: r (template).
4. Declined "Engage": r (& 3).

**A mission's set-up**:
1. Mode 1: r (& 7) for the spot. Mode 3: r (& 3) for the count. Mode 6: r (& 1) for the raid.
2. `17dc:0d49`: x, y: two r. Per enemy 'Mech: flag 03 above 4, five r; above 5, three r. Per
   enemy person: six r, 2d6, r (weapon).

**Each round**:
1. Orders and planning: none.
2. Flee chosen: r. (Not in the raid's walk-out case.) Computer control, F:3992 set and no party
   'Mech: r.
3. The 12 phases: the shots below, in phase order and slot order.
4. Enemy people running, no enemy 'Mech left: r.
5. The raid's second story line: one r (the picture).

**Each shot** that passes range and line of sight:
- At a person: personal weapon: (high nibble) d6; 2d6 to hit; a miss that meets the fire
  condition: 2d6.
- At a 'Mech: 2d6 location; Kick: r; personal weapon: stop; cluster: 2d6; 2d6 to hit; a miss
  that meets the fire condition: 2d6; on a hit, for each internal location damaged: 2d6 for the
  count, then per critical d6 (one or two) and possibly r for an item slot, repeated for failed
  picks.
- With graphics on (DG:2e3a): one r for the picture on a hit with a picture number; one r if an
  arm was destroyed; one r if a critical hit Jason's 'Mech.

**After the fight**: repairs: N r (only with N > 0 and I > 0); salvage: two r per pick; loot: one
r per beaten enemy person (only when something is shown); then, for modes 5-7, an r for Gunnery
and one for Piloting when each is below 4.

**Settings that change the draws**: the graphics setting DG:2e3a adds up to three draws per shot.
The computer-fights answer F:009e decides which flee draw can happen. The message level and the
message speed change no draws.

## 6. Original bugs and quirks

The rebuild keeps each one by default and offers a fix behind a switch that is off by default
(the mw3-re FIX practice). The switch names are proposals. "Probable" means it reads as a
mistake; "quirk" means it may be intended.

| # | what happens | where | fix switch |
|---|---|---|---|
| 1 | Distance is min + max/2, not the usual max + min/2: straight lines count half, diagonals relatively long. Quirk; it may be meant. | `1e31:0bb5` | `fix_distance` |
| 2 | The Kick's to-hit counts missing actuators of 'Mech record s, the figure number; for an enemy kicker (figures 12-15) that reads past the eight records, into the program's stack (F:cd24 + 0x7d (s - 12), bytes 0x24 / 0x25). Below the stack's deepest use, F:ce89 in every capture the rig has taken, those bytes are 0: all six actuators count as missing (T + 6), and the rebuild reads them so. Figure 15's (F:ce9b / ce9c) lie in stack the program has used: the rebuild takes 0 there too and logs it (open question). Probable. | `22e8:000c` to-hit, `1e31:1b44` | `fix_kick_actuators` (off; on: the kicker's own record) |
| 3 | Every damaged internal location gets at least one critical; the board game gives none below 8. Probable. | `1e31:11ab` | `fix_critical_count` |
| 4 | Against people, D is not reset between the shots of one weapon: armour halving carries on. Probable. | `22e8:000c` | `fix_person_damage` |
| 5 | A personal weapon at a 'Mech: no roll, no effect, and the hit flag passed to the animation is left from the round's last shot (unset at the round's first: 22e8:000c's stack, taken as a miss with a log line). With graphics on it picks between a miss's redraw and the explosion, so only the tiles' frame differs. The fix shows a miss. | `22e8:000c`, `22e8:12c7` | `fix_stale_hit_flag` |
| 6 | Actuator losses: MP is lifted to 1 only when exactly 0, so -1 ends as 0. Probable. | `203b:22bc` | `fix_actuator_min` |
| 7 | The run bonus +1 tests the undamaged walking MP for oddness. Quirk. | `203b:22bc` | `fix_run_parity` |
| 8 | Enemy people's MP: the planner passes figure - 8, so figures 16-19 use party characters 4-7 and 20-23 a flat 6. Probable. | `1e31:03ab`, `203b:2474` | `fix_enemy_foot_mp` |
| 9 | The 'Mech reach uses the short limit of the next weapon in the table (item code, not item code - 1). Probable. | `1e31:03ab` | `fix_planner_reach_index` |
| 10 | The reach is 4 x S where the bands use 3 x S. Quirk. | `1e31:03ab` | `fix_planner_reach_scale` |
| 11 | The stop point steps diagonally in a fixed direction, so it can overshoot the target's row or column. Quirk. | `1e31:03ab` | `fix_planner_stop_point` |
| 12 | The computer fires every weapon at every range and always tries the Kick. Behaviour; kept, no switch. | `1e31:03ab` | none |
| 13 | Planning an enemy person against a 'Mech sets F:3992 = 0, wiping a retreat set by a party person; planning a party person against a 'Mech sets it to 1, which the mission driver reads as fleeing. Probable. | `1e31:03ab`, `17dc:0629` | `fix_retreat_flag` |
| 14 | Under computer control, an auto-retreat value of 2 is later read as "left the raid area". Probable. | `203b:000a` | `fix_auto_retreat` |
| 15 | The flee draw is made in kinds 2 and 3, where fleeing is refused. Kept for the draw order. | `203b:000a` | none |
| 16 | A waypoint in area rows 8-15 is never recognised as a duplicate (sign extension), so it is added again. Harmless. | `203b:1c1f` | `fix_waypoint_compare` |
| 17 | A 13th waypoint is written into the next figure's first order (each Move adds one, so thirteen Moves reach it). Kept: the rebuild holds that order, and the `waypoints` sequence matches the original's write. The fix drops the waypoint and logs it. | `203b:1c1f` | `fix_order_bound` |
| 18 | The path is written without a bound; more than 12 steps would spill into the next figure's path. | `203b:193b` | `fix_path_bound` |
| 19 | Scan: for a 'Mech without sensors the "no enemy humans" check is skipped and the list is opened empty. With no enemy person active the original's list then never ends (bug 32), so only the sensors message with a person active is compared (`chain_scan_poked`). | `203b:2591` | `fix_scan_sensors` |
| 20 | The reachability test before "Engage" changes figure 0's heading. | `203b:28db` | `fix_probe_heading` |
| 21 | An enemy person whose target byte is ff is measured against figure -1 when the people run (2.10): x F:4002 (always 0), y F:4034 (the last kind-2 sound's fourth word, docs/sound.md). Kept. The fix: a person with no target never counts as fled. Other bytes above 0x7f always take the fixed path, with a log line. | `203b:000a` | `fix_flee_target` |
| 22 | Repairs with Tech 2+ mend every destroyed heat sink, even when the wrecks had none. Probable. | `15ab:0002` | `fix_heat_sink_count` |
| 23 | The weapon count for item 0x20 (SRM 6) is never cleared: it starts with leftover stack data, the low byte of DGROUP's segment (0xe9 under the rig), so destroyed SRM 6s are mended without a spare (2.15). Confirmed. | `15ab:0002` | `fix_weapon_count` |
| 24 | Scrap money is multiplied by the best tech's character number, not his skill; with Jason as the best tech there is none. Probable. | `15ab:0002` | `fix_scrap_multiplier` |
| 25 | A salvaged Jenner or UrbanMech gets sprite base 0x92; the encounter table gives them 0. | `15ab:04f9` | `fix_salvage_sprite` |
| 26 | The wreck search includes figures 4-11 (killed party people), which index the wrong records, and never ends if no flag is set. | `15ab:04f9` | `fix_wreck_search` |
| 27 | Loot reads character s + 12 (off the table, into 'Mech 0) to decide whether weapons were found; a dead party member alone gives the pocket text and 2 C-Bills. Probable. | `15ab:094b` | `fix_loot_index` |
| 28 | Encounter soldiers' armour points are 2d6 + 2d6 whatever the type (also with none). Quirk. | `15ab:0d3d` | none |
| 29 | Withdrawn: mode 3 asks for 3-6 enemy people, not 'Mechs (`0d49(0, (r & 3) + 3)`: the random count is pushed first, 1.3), and `0d49` caps the people at character record 16. Nothing to fix. | `17dc:0629` | none |
| 30 | The difficulty's destroyed slots are picked from 0x34-0x53 only: the first left-arm slot, the second centre-torso slot and the head slot are never hit. Quirk. | `17dc:0d49` | none |
| 31 | Reloading the areas around the party does not wrap columns: column 0's left neighbour is column 15 of the row above. | `203b:2835` | `fix_area_wrap` |
| 32 | The scan list never ends if no figure in it is active; a direction of -1 reads before the direction names. | `15ab:1467` | `fix_scan_list` |
| 33 | Jason's 'Mech destroyed in a mission stops all movement only when the damage ended exactly in the head or centre torso, not when it overflowed. | `22e8:000c` | `fix_stop_on_loss` |
| 34 | Only ten ammunition bytes: an 11th weapon would use the walking MP byte as its ammunition. No template has more than nine weapons. | `1e31:10a2`, `22e8:000c` | none |
| 35 | The end of the round caps heat at 30 for party record a only, whichever record it heated: an enemy 'Mech's heat climbs past 30 (and its MP stays 0 only at exactly 30, 3.2). | `1e31:0c63` | `fix_enemy_heat_cap` |
| 36 | The terrain under a figure (F:32ae, F:3750, F:45ce, F:4554: to-hit, water cooling, fire, the missed-shot mark) is written only by the figure drawers (`1000:051b`, `1000:0e4b`). In a computer-fought fight they run only for the combat graphics (the round's redraw and the shot's presentation) or the Arena's spectator, so with combat graphics off the terrain has no effect at all (the player's orders screen draws the figures too). With graphics on, only figures in Jason's view get theirs. Quirk; the fix writes the drawer's values at the round's redraw without showing it, as with graphics on. | `22e8:000c`, `22e8:12c7` | `fix_terrain_without_graphics` |
| 37 | The terrain generator's inside right sides take the whole noise byte as their displacement (c / 8 for c cells, which is 0), not one scaled to their length like the other sides. Every terrain block shows it; docs/wilderness.md. | `287f:0df8` | `fix_terrain_noise` |
| 38 | Talk to others tests the Lounge's let-out (flag 2e, walker 0 freed) for place 7 with the place of the last townsperson waiting anywhere, not this place (docs/scripts.md, "Original quirks"). | `17dc:17b9` | `fix_talk_let_out` |
| 39 | The overhead map reads 512 bytes of cells for maps 12 and 13, which hold one block (64), and moves its block store on by 512 while numbering one block: a map drawn after one of them in the same view shows its blocks 448 bytes early (docs/overhead_map.md). Probable. | `1000:3fae` | `fix_overhead_store` |
| 40 | REPAIR's weapon repairs (special 18): one payment of 300 C-Bills clears bit 0x80 on every destroyed weapon of the chosen type, but the type's count goes down by one only. A 'Mech with two or more of a type destroyed sees the type listed again, and paying for it again mends nothing. Kept. The fix: the type's count drops to 0 with the payment. | `19b8:0002` | `fix_repair_type_count` |
| 41 | REPAIR's weapon repairs (special 18): after a payment, the test for any destroyed weapon left counts types 0x10-0x1f and not 0x20 (the SRM 6), though the list and the counts run to 0x20. With only SRM 6s left the weapons are called all fixed and the stage ends; they stay destroyed (a later visit lists them again). Kept. The fix: type 0x20 counted too. | `19b8:06f9` | `fix_repair_last_type` |

The switches are `game/fixes.h`. Each is off by default, except where the original reads or
writes memory the rebuild does not hold, or never returns: bugs 18, 26 and 32 take the fixed
path by default and log it. Bugs 2, 21 and 23 read such memory too, but what they read is
known (this table's rows), so they are kept like the others. Bug 5 is kept, but the first shot of a session (whose
flag the original leaves uninitialised) counts as "missed" and is logged. Bug 17 writes into
memory the rebuild holds (the next figure's orders), so it is kept like the others; the
`waypoints` reference sequence checks that write against the original. Bugs 12, 15, 28, 30 and
34 have no switch.

## 7. The copy-protection check

`1c67:08a8` (FUN_1c67_08a8) runs on entering the training center (script 0: 17dc:0008 turns the entrance into its script
before the test, so entrance 0 of another map, such as map 2's garage, does not run it) while flag 14 is 0 and
no demo plays (docs/scripts.md, "Entering a place"). It is the original's copy protection: a quiz
on the 'Mech parts pictured in the manual.

1. BTSTATS.CMP (name at DG:27e6) is loaded and unpacked off the screen (the disk check 1000:28cc
   first; not when F:4594). A message (DG:27f2, `quiz.message`) in the big box (window 6, frame
   style 0) says a written quiz comes first; a key.
2. Three questions. For each: the picture is copied over the whole screen, a line drawn down
   x 128 from row 0 to 192 (colour 9; 10 on CGA), the left strip (window 8) cleared, "Identify
   this 'Mech part:" printed (DG:286c, `quiz.question`), and the 20 part names written from row 4,
   white on black (`273d:00d5`; far pointers at DG:4de2, `quiz.part.0`-`quiz.part.19`). Then
   the part: r mod 10 (signed division), drawn again while it was asked before. Part i is boxed
   on the picture: a 9x9 square in white at (DG:28c4[i] - 3, DG:28d8[i] - 3), and a white bar
   from the line at x 128 to the square, its rows +2 to +6. Menu 0x25 (20 items from row 4,
   16 cells wide: the part names) asks which part it is, and the answer is compared with
   DG:28ec[i]. Any wrong answer sets the fail flag.
3. Failed: text DG:2886, flag 14 = ff (no new try for 255 ticks), a key, and the place is not
   entered.
4. `287f:1fbe` sets the screen back, and the panel is redrawn (`4cac(1)`, the party box cleared,
   then the box above it).

The tables are ten words each: x (DG:28c4), y (DG:28d8) and the answer (DG:28ec), one per part
pictured. Every answer is one of menu 0x25's 20 items. Menu 0x25's count (20) and the loop's
(0x14, `1c67:0b1a`) agree with the 20 names at DG:4de2; the pointers after them belong to other
texts.

The quiz itself sets flag 14 only on a failure, but TRAINING.BLD sets it to ff as each mission
starts (after special 1), and the town's ticks count it down. While it is nonzero the training
center says the course is not ready yet (TRAINING.BLD +1bec) and no quiz runs. So the quiz is
asked on the first visit, and again on the first visit after flag 14 has run down from a mission
or a failure.

**The copy we read is modified here.** In the BTECH.EXE we read, this check has been altered so
that it always passes. That is not the original's behaviour, so this page describes the check as
the program was built and nothing here relies on the altered copy. Following docs/rules.md (rule 3),
the rebuild has no protection check and nothing in it can fail.

**The draws are kept.** The original picks each part before it reads that question's answer, so
the draws are the same whether the player passes or fails. The rebuild makes them on every
entry while flag 14 is 0 (`game/training_quiz.h`, `draw_quiz_parts`).

**The scene is optional.** By default the rebuild skips the scene: the draws are made and nothing
is shown, and the original's key and three menu answers are not asked (a rig script leaves them
out on the rebuild's side, `orig:`). With `chr --show-quiz` (`Town::show_quiz`) it plays the
scene as above from the player's own files: BTSTATS.CMP, the part's box and bar
(`platform/quiz_layer.cpp`), and the question and part names from imported text IDs. The
parts' places and answers are imported into `rules/quiz.json`. There is no check: each menu's
bar starts on the right part, any answer is taken, there is no fail path, and the quiz never
sets flag 14. The menu remembers the item chosen, as the original's does. The rig's `quiz`
sequence compares the scene with the original's, over the whole screen and the state
(docs/reference.md).

## 8. Open questions

1. `17dc:1c9b` sets the eight characters' 'Mech slot bytes to 3 (meaning unknown).
2. F:32ae and F:3750 are what the figure drawer `1000:0e4b` worked out (3.14: the rows a figure
   sinks, the tile under it). `1000:051b` writes F:32ae for each 'Mech of records 0-3 (at the
   record's index): the same 8 or 16 rows as `0e4b`, 0 with F:d346 set. Before a fight it is read
   by the walking course (1.3). With combat graphics off nothing draws the
   figures during the rounds, so in such a fight they keep their start values (0 for kinds
   1-3).
3. The nine bytes P:07a4..07ac whose bit 0x80 blocks salvage. In a fight's memory they are the
   area-grid entries of the 3x3 blocks in the cell window (0x80 + block, rows 8 apart), so bit
   0x80 would mean a fixed block; how salvage uses that is not read.
4. The location rows: d = 0 gives the rear column, but three directions give the left-side column
   and two the front. Either our reading of the facing (F:396c) or of the direction (`287f:0971`,
   not read) is off, or the original is lopsided. To check in the running original.
5. The meaning of the mark codes 0x7c and 0x7f, and what draws the mark ring.
6. Why character 1's death ends the fight like Jason's (DG:014a).
7. F:396c + i and F:397c + i at the end of the fight: which figures 0-7 is meant for.
8. A party 'Mech that reaches its last waypoint has no order left, so it counts as standing for
   to-hit and heat; enemies keep their order. This follows from the code as read; check in play.
9. Flag 24's values and where it is set, besides the traitor's 0x7f.
10. `1c31:000a` (healing after a fight), `17dc:15e6` (offering a weapon), `15ab:18e8`, `1000:0e4b`,
    `287f:1df8`, `287f:1da8`: not read.
11. (Answered: `2da8(4, 11)` loads map 11 into grid 4, 2.11.)
12. Whether the menu `2656:0b5e` can return -1 (Esc).
13. 'Mech byte 0x78, F:4586 / F:3986 readers besides those above, DG:015e and DG:0150 (where they
    are set).
14. The three character bytes 1-3 (by use: byte 1 sets the hit-point maximum, byte 2 the walking
    speed on foot); their names.
15. (Answered.) A failed Flee is compared by the `handover` sequence. With no shots, rounds draw
    no random numbers, and idle passes in town draw one only on the 11th in a row (every key is a
    game tick and resets the countdown, docs/town_loop.md); 52 extra passes and six
    computer-fought rounds give a flee draw with r & 3 = 0.
16. The raid's walk-out ending (a party figure past the raid area's edge, [bp-0x38] = 2: window 3,
    "You have eluded your enemies!" (DG:39f7) and `1000:2a4f`'s key) is rebuilt but not compared
    by the rig: the `raid` sequence ends with the Locust destroyed.
17. Answered: mission 6's draw lands on the raid in the campaign run (`chain`, docs/reference.md),
    and the raid matches from there to its ending text, Begin Fight round after round until the
    Jenners bring the Chameleon down. Mission 6 without the raid (the draw's other side) is still
    not compared.
18. Answered: special 43 (scripts.md) is compared in the campaign run (`chain`, docs/reference.md),
    which wins mission 5 by orders. Its walker stays inside while flag 2d is set
    (docs/townspeople.md).
19. Figure 15's Kick (bug 2) reads 'Mech record 15's bytes 0x24 / 0x25, F:ce9b / ce9c, in stack the
    program has used (its deepest use in the rig's captures is F:ce89). What they hold depends on the
    calls before; the rebuild takes 0, as below the deepest use, and logs it. A difference there shows
    as a state difference after that fight.

## Functions

| address | Ghidra | what |
|---|---|---|
| `17dc:0629` | FUN_17dc_0629 | the mission driver, modes 0-9 |
| `17dc:0d49` | FUN_17dc_0d49 | opponent set-up for missions |
| `17dc:134b` | FUN_17dc_134b | the party position back on Jason's figure |
| `17dc:1c9b`, `1b41` | FUN_17dc_1c9b, FUN_17dc_1b41 | the Arena 'Mech set-up and its undo |
| `19b8:152f` | FUN_19b8_152f | Jason alone (hides the rest), then mode 9 |
| `1c67:08a8` | FUN_1c67_08a8 | the copy-protection quiz |
| `1c67:0d7e` | FUN_1c67_0d7e | clears every path |
| `1d43:0004` | FUN_1d43_0004 | Use Weapons |
| `1d43:07cb` | FUN_1d43_07cb | the target picker |
| `1d43:0a35` | FUN_1d43_0a35 | a 'Mech is destroyed; crews eject |
| `1e31:03ab` | FUN_1e31_03ab | the computer planner |
| `1e31:0bb5` | FUN_1e31_0bb5 | distance |
| `1e31:0c63` | FUN_1e31_0c63 | end of round: heat, cleanup |
| `1e31:0f24` | FUN_1e31_0f24 | range band |
| `1e31:10a2` | FUN_1e31_10a2 | a 'Mech's i-th weapon |
| `1e31:1122` | FUN_1e31_1122 | damage transfer |
| `1e31:11ab` | FUN_1e31_11ab | critical hits |
| `1e31:163e` | FUN_1e31_163e | destroy an item slot |
| `1e31:1bfe` | FUN_1e31_1bfe | line of sight |
| `15ab:0002` | FUN_15ab_0002 | repairs and scrap |
| `15ab:04f9` | FUN_15ab_04f9 | salvage |
| `15ab:094b` | FUN_15ab_094b | loot |
| `15ab:0b5e` | FUN_15ab_0b5e | "computer fights for you?" |
| `15ab:0d3d` | FUN_15ab_0d3d | the random encounter's set-up |
| `15ab:1467` | FUN_15ab_1467 | the scan list |
| `203b:000a` | FUN_203b_000a | the fight |
| `203b:1482` | FUN_203b_1482 | plan twelve figures |
| `203b:14c3` | FUN_203b_14c3 | the orders menu |
| `203b:1774` | FUN_203b_1774 | show a figure and its path |
| `203b:193b` | FUN_203b_193b | simulate the orders: path and cost |
| `203b:1c1f` | FUN_203b_1c1f | enter a waypoint |
| `203b:2231` | FUN_203b_2231 | Kick |
| `203b:22bc` | FUN_203b_22bc | a 'Mech's movement points |
| `203b:2474` | FUN_203b_2474 | a person's movement points |
| `203b:24f0`, `2556` | FUN_203b_24f0, FUN_203b_2556 | the message and graphics questions |
| `203b:2591` | FUN_203b_2591 | Scan Unit |
| `203b:273d` | FUN_203b_273d | is an item slot's location intact |
| `203b:27c9` | FUN_203b_27c9 | add a map mark |
| `203b:2835` | FUN_203b_2835 | reload the areas around the party |
| `203b:28db` | FUN_203b_28db | can Jason reach an enemy |
| `22e8:000c` | FUN_22e8_000c | one round |
| `22e8:12c7` | FUN_22e8_12c7 | a shot's presentation, kills and wrecks |
| `22e8:1e46` | FUN_22e8_1e46 | the fire check on a miss |
| `24d3:0027`, `0187`, `01c2` | in FUN_24d3_0004 | training specials 1-3 |
