# Function map

Every function Ghidra finds in the unpacked `BTECH.EXE` (docs/original.md), by segment.
Addresses are `segment:offset` with the program loaded at 0x1000; *linear* is Ghidra's flat
address. *Span* is the distance to the next function (an upper bound on its size). *Label* and
*Status* fill in as each function is read and rebuilt. Plain words only: no decompiled code.

Status: `-` not read, `read` understood and documented, `rebuilt` in `src/`, `replaced` (hardware,
DOS or run-time library: stood in for by the platform layer or the C++ library).


## CODE_0: segment 1000, 21,114 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 1000:0000 | 0x10000 | `switchD_1000:f462::caseD_6` | 45 | Main loop: stores its argument in F:3938 (1: the demo), clears DG:0152 and runs passes until it is set (docs/town_loop.md, docs/startup.md) | rebuilt |
| 1000:002d | 0x1002d | `FUN_1000_002d` | 142 | Main loop, key half: reads a key, flushes, steps, logs the direction, walking frames (231d), then a game tick (docs/town_loop.md) | rebuilt |
| 1000:00bb | 0x100bb | `FUN_1000_00bb` | 313 | Main loop: one turn per key; redraws the map, figures and panel each turn; resets the one-question-per-turn flag | rebuilt |
| 1000:01f4 | 0x101f4 | `FUN_1000_01f4` | 807 | Main loop, idle half: waits one display frame (273d:0006); every 11 passes a game tick: timers, random, money, tile animation 240b, 24c2 every third, redraw (docs/town_loop.md) | rebuilt |
| 1000:051b | 0x1051b | `FUN_1000_051b` | 2352 | Draws the figures: map objects (2a93), the party (walking frame, sinking in water), the townspeople in view, 'Mechs; records the party's places | rebuilt |
| 1000:0e4b | 0x10e4b | `FUN_1000_0e4b` | 2279 | The fight's figure drawer: the map's marks (2a93), people, then 'Mechs top to bottom; writes on screen, screen place, tile, sink (docs/combat.md 3.14) | rebuilt |
| 1000:1732 | 0x11732 | `FUN_1000_1732` | 137 | Runs a figure's walking script to its next frame (0xfd direction, 0xfe jump back, 0xff hold) | rebuilt |
| 1000:17bb | 0x117bb | `FUN_1000_17bb` | 92 | The party position walked to a place a cell at a time (y first) through the area steps | rebuilt |
| 1000:1817 | 0x11817 | `FUN_1000_1817` | 88 |  | - |
| 1000:186f | 0x1186f | `FUN_1000_186f` | 172 | The party position set to a place directly, without the area steps | rebuilt |
| 1000:191b | 0x1191b | `FUN_1000_191b` | 164 | Moves the party position by (dx, dy) half-cells, across area edges | rebuilt |
| 1000:19bf | 0x119bf | `FUN_1000_19bf` | 30 | Plays sound effect n if sound is on (DG:015c) (docs/sound.md) | rebuilt |
| 1000:19dd | 0x119dd | `FUN_1000_19dd` | 22 | 2d6: two d6, the first drawn first | rebuilt |
| 1000:19f3 | 0x119f3 | `FUN_1000_19f3` | 32 | A d6: a random number's low 3 bits, drawn again while above 5, plus 1 | rebuilt |
| 1000:1a13 | 0x11a13 | `FUN_1000_1a13` | 234 | Yes/no question: Y/N answer, left/right pick, space/Enter accept; reprints the answer line each key | rebuilt |
| 1000:1afd | 0x11afd | `FUN_1000_1afd` | 277 | Shows one .ANM picture: its frames and delays (docs/formats/anm.md) | rebuilt |
| 1000:1c12 | 0x11c12 | `FUN_1000_1c12` | 1405 | Can the party step by (dx, dy)? Deep water at a block edge; offers to enter a place at an entrance; blocks on tile >= DG:0150; the party's 'Mechs (two cells each); map 14's tile events (not rebuilt) | rebuilt |
| 1000:218f | 0x1218f | `FUN_1000_218f` | 398 | Walking keys: arrows and Home/PgUp/End/PgDn to a step, checks it, moves y then x, loads areas crossed into | rebuilt |
| 1000:231d | 0x1231d | `FUN_1000_231d` | 238 | After a key: a walking key moves the figures' walking scripts on, the 'Mechs' too | rebuilt |
| 1000:240b | 0x1240b | `FUN_1000_240b` | 183 | Animated tiles: next of 3 frames, ANIMATE.ICN tile 1+3i+frame over each of the 10 tile numbers at DG:04b0 | rebuilt |
| 1000:24c2 | 0x124c2 | `FUN_1000_24c2` | 933 | Townspeople, every third game tick: count down inside, come out at the last entrance; out and near the party, a step (1e31:0006), walking frame, at the target go in for random&31 rounds | rebuilt |
| 1000:2867 | 0x12867 | `FUN_1000_2867` | 59 | Prints a text at (column, row) of the current window in white (F:3748, 374e, 37fe = 15) | rebuilt |
| 1000:28a2 | 0x128a2 | `FUN_1000_28a2` | 42 | Sets the text colour to light green (CGA: 1) | rebuilt |
| 1000:28cc | 0x128cc | `FUN_1000_28cc` | 71 | Asks for game disk n (a check file, then a prompt) (docs/scripts.md) | replaced (no disks) |
| 1000:2913 | 0x12913 | `FUN_1000_2913` | 226 | The disk check after a disk prompt | replaced (no disks) |
| 1000:29f5 | 0x129f5 | `FUN_1000_29f5` | 54 | The money floor: word F:d340/d33f | rebuilt |
| 1000:2a2b | 0x12a2b | `FUN_1000_2a2b` | 36 | Empties the keyboard buffer (unless playing back recorded keys) | rebuilt |
| 1000:2a4f | 0x12a4f | `FUN_1000_2a4f` | 26 | the press-a-key prompt (DG:050d) and a key | rebuilt |
| 1000:2a69 | 0x12a69 | `FUN_1000_2a69` | 21 | Prints the plural ending (DG:051b) | rebuilt |
| 1000:2a7e | 0x12a7e | `FUN_1000_2a7e` | 21 | Prints the full stop (DG:051d) | rebuilt |
| 1000:2a93 | 0x12a93 | `FUN_1000_2a93` | 445 | Draws the map's marks in view (the 64-place ring, docs/combat.md 3.13), toggling a fire's sprite 0x7c/0x7d each drawing | rebuilt |
| 1000:2c50 | 0x12c50 | `FUN_1000_2c50` | 344 | Space: the command menu | rebuilt |
| 1000:2da8 | 0x12da8 | `FUN_1000_2da8` | 1123 | Loads MAP<n>.MTP into a grid (its blocks 0x90 on, docs/wilderness.md); blocking tile; places the 8 townspeople (8 random numbers) unless DG:014c | rebuilt |
| 1000:320b | 0x1320b | `FUN_1000_320b` | 168 | Loads ANIMATE.ICN into the animation buffer | rebuilt |
| 1000:32b3 | 0x132b3 | `FUN_1000_32b3` | 800 | Loads a saved game: picks a slot (menu 0x28) and reads it | rebuilt |
| 1000:35d3 | 0x135d3 | `FUN_1000_35d3` | 442 | Saves a game: a slot from menu 0x28 and the file GameN; refused on the cache's second level (docs/formats/save.md) | rebuilt |
| 1000:378d | 0x1378d | `FUN_1000_378d` | 1091 | The command menu's inspect item: a 'Mech (chooser, stats screen) or a character's page (docs/town_loop.md) | rebuilt |
| 1000:3bd0 | 0x13bd0 | `FUN_1000_3bd0` | 368 | The command menu's settings: steps a key, message speed, sound, picture chance, quit (docs/town_loop.md) | rebuilt |
| 1000:3d40 | 0x13d40 | `FUN_1000_3d40` | 622 | The overhead map: the cursor moved two areas at a time, then the nine-map load without townspeople (docs/overhead_map.md) | rebuilt |
| 1000:3fae | 0x13fae | `FUN_1000_3fae` | 1556 | Draws the overhead map (5 x 3 areas, seen tiles only), its pointer, caption and the party's dot until a key | rebuilt |
| 1000:45c2 | 0x145c2 | `FUN_1000_45c2` | 95 | A town block drawn small for the overhead map (287f:1f51), converted for CGA or Tandy | replaced (platform/overhead_layer.cpp) |
| 1000:4621 | 0x14621 | `FUN_1000_4621` | 134 | Load BTTLTECH.ICN as the map tile sheet | rebuilt |
| 1000:46a7 | 0x146a7 | `FUN_1000_46a7` | 198 | Shows the title screen (BTTITLE.CMP, with its palette) (docs/startup.md) | rebuilt |
| 1000:476d | 0x1476d | `FUN_1000_476d` | 330 | Plays the title's tune (WWOODBT.SIF) until a key or its end (docs/startup.md) | rebuilt |
| 1000:48b7 | 0x148b7 | `FUN_1000_48b7` | 495 | Shows place picture O<n>.ANM in the picture box, or not, by chance (docs/formats/anm.md) | rebuilt |
| 1000:4aa6 | 0x14aa6 | `FUN_1000_4aa6` | 14 | Panel: a character's name and three bars (columns 9-11), the first with a red part | rebuilt |
| 1000:4ab4 | 0x14ab4 | `FUN_1000_4ab4` | 269 |  | - |
| 1000:4bc1 | 0x14bc1 | `FUN_1000_4bc1` | 235 | Panel: one bar, a yellow outline and a green fill from the bottom | rebuilt |
| 1000:4cac | 0x14cac | `FUN_1000_4cac` | 171 | The left panel: party box (window 3, frame style 3, 'BDC' heading, up to 4 names) and the box above (window 4, style 4) | rebuilt |
| 1000:4d57 | 0x14d57 | `FUN_1000_4d57` | 112 | The command menu's crew item: the crew screen, refused near a map block | rebuilt |
| 1000:4dc7 | 0x14dc7 | `FUN_1000_4dc7` | 769 | New game: reset the party, load map 1 into area 4, set the start position | rebuilt |
| 1000:50c8 | 0x150c8 | `FUN_1000_50c8` | 434 | Start-up: 256 random numbers into P:09fb; the title; the notice and first-time question; the demo if no key (docs/startup.md) | rebuilt |

## CODE_1: segment 1527, 2,104 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 1527:000a | 0x1527a | `FUN_1527_000a` | 58 |  | - |
| 1527:0044 | 0x152b4 | `FUN_1527_0044` | 2046 | Start-up: cuts MECHSHAP.CMP into the sprite table; shows the Infocom screen (docs/startup.md) | rebuilt |

## CODE_2: segment 15ab, 8,982 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 15ab:0002 | 0x15ab2 | `FUN_15ab_0002` | 1271 | After a fight: repairs from the wrecks and scrap money | rebuilt |
| 15ab:04f9 | 0x15fa9 | `FUN_15ab_04f9` | 1106 | After a fight: salvage a wreck for a pilot on foot | rebuilt |
| 15ab:094b | 0x163fb | `FUN_15ab_094b` | 531 | After a fight: loot the beaten soldiers (C-Bills, a weapon swap; bug 27) | rebuilt |
| 15ab:0b5e | 0x1660e | `FUN_15ab_0b5e` | 55 | Asks whether the computer fights for you (F:009e) | rebuilt |
| 15ab:0b95 | 0x16645 | `FUN_15ab_0b95` | 250 | The speaker speed test: passes in four timer ticks (docs/sound.md) | replaced (DG:5006 fixed, docs/sound.md) |
| 15ab:0c8f | 0x1673f | `FUN_15ab_0c8f` | 131 | Sets the sound delay DG:5006 from the speed test (docs/sound.md) | replaced (DG:5006 fixed, docs/sound.md) |
| 15ab:0d12 | 0x167c2 | `FUN_15ab_0d12` | 43 |  | - |
| 15ab:0d3d | 0x167ed | `FUN_15ab_0d3d` | 1834 | Sets up a random encounter: soldiers and 'Mechs, placed 10-17 cells away | rebuilt |
| 15ab:1467 | 0x16f17 | `FUN_15ab_1467` | 741 | The scan list: each figure of a side in turn, Next / Detail / Done | rebuilt |
| 15ab:174c | 0x171fc | `FUN_15ab_174c` | 268 | The stats screen's bar: two parts from a bottom row up; the heat bar's lower part blinks (docs/town_loop.md) | rebuilt |
| 15ab:1858 | 0x17308 | `FUN_15ab_1858` | 144 | The stats screen's pips: a row of squares, the first k green, five a row | rebuilt |
| 15ab:18e8 | 0x17398 | `FUN_15ab_18e8` | 534 | A unit's heading: a 'Mech's pilot and name; a person's name, with the details flag its condition, weapon and armour | rebuilt (not the double agent) |
| 15ab:1afe | 0x175ae | `FUN_15ab_1afe` | 2074 | The 'Mech stats screen: BTSTATS.CMP, the record's text, bars and pips, its own pulsing colour, until a key (docs/town_loop.md) | rebuilt |

## CODE_3: segment 17dc, 7,610 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 17dc:0008 | 0x17dc8 | `FUN_17dc_0008` | 440 | Enters a place: loads and runs its .BLD script, then puts the town screen back (docs/scripts.md) | rebuilt |
| 17dc:01c0 | 0x17f80 | `FUN_17dc_01c0` | 1079 | The .BLD script interpreter: opcodes E4-FF (docs/scripts.md) | rebuilt |
| 17dc:05f7 | 0x183b7 | `FUN_17dc_05f7` | 50 | Reads a script word (jump targets) | rebuilt |
| 17dc:0629 | 0x183e9 | `FUN_17dc_0629` | 1824 | Scene driver: training missions 0-7, the arena (8), the jail break (9); results in flag 01 (docs/combat.md); modes 0-7 (not the raid) and the walk rebuilt | rebuilt |
| 17dc:0d49 | 0x18b09 | `FUN_17dc_0d49` | 1538 | Sets up the opponents: enemy pilots and 'Mechs from templates, difficulty armour cut (docs/combat.md) | rebuilt |
| 17dc:134b | 0x1910b | `FUN_17dc_134b` | 147 | Puts the party at figure 0 (or 4) after a fight and redraws | rebuilt |
| 17dc:13de | 0x1919e | `FUN_17dc_13de` | 520 | Armour offered to the party (ARMOR's purchase; docs/scripts.md, "The shops") | rebuilt |
| 17dc:15e6 | 0x193a6 | `FUN_17dc_15e6` | 467 | A weapon offered to the party (after a fight, WEAPON's purchase) | rebuilt |
| 17dc:17b9 | 0x19579 | `FUN_17dc_17b9` | 573 | Talk to others: who is inside, and where they go next (docs/scripts.md, special 12) | rebuilt |
| 17dc:19f6 | 0x197b6 | `FUN_17dc_19f6` | 48 |  | - |
| 17dc:1a26 | 0x197e6 | `FUN_17dc_1a26` | 283 | ARENA's special 34: the party's own 'Mech for the Arena (docs/scripts.md, "The Arena") | rebuilt |
| 17dc:1b41 | 0x19901 | `FUN_17dc_1b41` | 346 | After an Arena fight with no theft: the 'Mechs, seats and members put back | rebuilt |
| 17dc:1c9b | 0x19a5b | `FUN_17dc_1c9b` | 149 | ARENA's special 33: the Arena's 'Mech rented, the spectator on | rebuilt |
| 17dc:1d30 | 0x19af0 | `FUN_17dc_1d30` | 146 | Deciphers the loaded .BLD script (docs/formats/bld.md) | rebuilt |

## CODE_4: segment 19b8, 6,738 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 19b8:0002 | 0x19b82 | `FUN_19b8_0002` | 2056 | REPAIR's repair shop, special 18: armour, structure, heat sinks, weapons by type (bug 40), actuators (docs/scripts.md, "The repair shop") | rebuilt |
| 19b8:080a | 0x1a38a | `FUN_19b8_080a` | 283 | The 'Mech upgrade offer, special 19: the upgrade from bytes 0x7b and 0x7c (docs/combat.md 2.6, Kick) | rebuilt |
| 19b8:0925 | 0x1a4a5 | `FUN_19b8_0925` | 1075 | Pays for and applies a 'Mech upgrade, special 20: eight cases by model and stage (docs/combat.md 2.6, Kick) | rebuilt |
| 19b8:0d58 | 0x1a8d8 | `FUN_19b8_0d58` | 758 | A new party member joins (script E9) | rebuilt |
| 19b8:104e | 0x1abce | `FUN_19b8_104e` | 817 | PARTY's alley: the first companion joins, then soldiers on foot attack (special 1e, docs/combat.md 1.6) | rebuilt |
| 19b8:137f | 0x1aeff | `FUN_19b8_137f` | 194 | One of three wall patches in the Arena, by chance (DG:0010) | rebuilt |
| 19b8:1441 | 0x1afc1 | `FUN_19b8_1441` | 238 | The Arena's patch made floor again, one draw a middle cell | rebuilt |
| 19b8:152f | 0x1b0af | `FUN_19b8_152f` | 387 | Jason alone on foot, the rest hidden, then mission mode 9 (the jail break) | rebuilt |
| 19b8:16b2 | 0x1b232 | `FUN_19b8_16b2` | 176 | The 'Mech that won't start: its animation, sound 14 and a wait (docs/combat.md) | rebuilt |
| 19b8:1762 | 0x1b2e2 | `FUN_19b8_1762` | 754 | REPAIR's ammunition, special 44 (docs/scripts.md, "The repair shop") | rebuilt |

## CODE_5: segment 1b5d, 3,397 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 1b5d:0004 | 0x1b5d4 | `FUN_1b5d_0004` | 485 | Load map 14 with STARLEAG.ICN (from its calls) | rebuilt |
| 1b5d:01e9 | 0x1b7b9 | `FUN_1b5d_01e9` | 159 | The cache: the wall at cells x 0-2, y 42-47 changed from a table (docs/cache.md) | rebuilt |
| 1b5d:0288 | 0x1b858 | `FUN_1b5d_0288` | 32 | The cache: ENDMECH at the tiles above f5 while F:d34b is clear (docs/cache.md) | rebuilt |
| 1b5d:02a8 | 0x1b878 | `FUN_1b5d_02a8` | 42 | The cache: a text and a key at tiles 83 and a5-a7 (docs/cache.md) | rebuilt |
| 1b5d:02d2 | 0x1b8a2 | `FUN_1b5d_02d2` | 85 | The cache: the hint at its own place; from then on the way out answers (docs/cache.md) | rebuilt |
| 1b5d:0327 | 0x1b8f7 | `FUN_1b5d_0327` | 131 | Swaps the cache level's cell colour table into the overhead map's (P:215d) | replaced (a table choice) |
| 1b5d:03aa | 0x1b97a | `FUN_1b5d_03aa` | 257 | The cache: the items at a place offered for their slots (docs/cache.md) | rebuilt |
| 1b5d:04ab | 0x1ba7b | `FUN_1b5d_04ab` | 175 | The cache: the way out (tile 4d), and the ending once everything is done (docs/cache.md) | rebuilt |
| 1b5d:055a | 0x1bb2a | `FUN_1b5d_055a` | 578 | The cache: tile 28, FINDIT and the way down (docs/cache.md) | rebuilt |
| 1b5d:079c | 0x1bd6c | `FUN_1b5d_079c` | 375 | The cache: tile 94, back up to the first level (docs/cache.md) | rebuilt |
| 1b5d:0913 | 0x1bee3 | `FUN_1b5d_0913` | 109 | The cache: a puzzle cell turned into its pair (docs/cache.md) | rebuilt |
| 1b5d:0980 | 0x1bf50 | `FUN_1b5d_0980` | 310 | The cache: the puzzle's check, sound 16 or 17, flag 3e (docs/cache.md) | rebuilt |
| 1b5d:0ab6 | 0x1c086 | `FUN_1b5d_0ab6` | 659 | The cache: a door and the three items it needs (docs/cache.md) | rebuilt |

## CODE_6: segment 1c31, 857 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 1c31:000a | 0x1c31a | `FUN_1c31_000a` | 856 | Healing: the best medic or a doctor treats the hurt, d6s by skill and care (docs/combat.md 2.15) | rebuilt |

## CODE_7: segment 1c67, 3,522 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 1c67:0002 | 0x1c672 | `FUN_1c67_0002` | 2214 | The crew screen: who pilots and who rides each 'Mech (docs/scripts.md) | rebuilt |
| 1c67:08a8 | 0x1cf18 | `FUN_1c67_08a8` | 752 | The training center's first visit: the copy-protection quiz (docs/combat.md) | rebuilt (its draws always; the scene with --show-quiz; no check, docs/rules.md) |
| 1c67:0b98 | 0x1d208 | `FUN_1c67_0b98` | 486 | Chooses a 'Mech by menu 0x17 (the Arena's own 'Mech) | rebuilt |
| 1c67:0d7e | 0x1d3ee | `FUN_1c67_0d7e` | 70 | Clears every figure's path | rebuilt |

## CODE_8: segment 1d43, 3,810 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 1d43:0004 | 0x1d434 | `FUN_1d43_0004` | 1991 | The Use Weapons screen | rebuilt |
| 1d43:07cb | 0x1dbfb | `FUN_1d43_07cb` | 618 | The target picker | rebuilt |
| 1d43:0a35 | 0x1de65 | `FUN_1d43_0a35` | 573 | A 'Mech destroyed: targets cleared, the party crew ejects | rebuilt |
| 1d43:0c72 | 0x1e0a2 | `FUN_1d43_0c72` | 108 | The Arena's broken wall: map-store cells 0xc00-0xc25 patched (DG:2b8e) or put back (DG:5804) | rebuilt |
| 1d43:0cde | 0x1e10e | `FUN_1d43_0cde` | 520 | A whole number typed at the cursor, up to 7 digits, until Enter (docs/scripts.md) | rebuilt |

## CODE_9: segment 1e31, 8,356 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 1e31:0006 | 0x1e316 | `FUN_1e31_0006` | 734 | A figure's step toward a target: turn one eighth, try 8 directions (wander pattern flips every 30), free cell and no figure there | rebuilt |
| 1e31:02e4 | 0x1e5f4 | `FUN_1e31_02e4` | 75 | A person's condition: line hit points / B of DG:2e0c | rebuilt |
| 1e31:032f | 0x1e63f | `FUN_1e31_032f` | 124 | The map screen drawn again (with 1, the fight's figures), windows 4 and 3 cleared and framed | rebuilt |
| 1e31:03ab | 0x1e6bb | `FUN_1e31_03ab` | 2058 | The computer's orders for a unit: target, waypoint, weapons; with show, the unit's screen (docs/combat.md) | rebuilt |
| 1e31:0bb5 | 0x1eec5 | `FUN_1e31_0bb5` | 174 | Distance from the party position: min + max/2 | rebuilt |
| 1e31:0c63 | 0x1ef73 | `FUN_1e31_0c63` | 705 | End of a combat round: heat, fired marks cleared, the dead removed | rebuilt |
| 1e31:0f24 | 0x1f234 | `FUN_1e31_0f24` | 307 | The range band (short, medium, long, out) from the party's position to a figure with a weapon | rebuilt |
| 1e31:1057 | 0x1f367 | `FUN_1e31_1057` | 75 | A message in window 4 and a key, then the window that was current | rebuilt |
| 1e31:10a2 | 0x1f3b2 | `FUN_1e31_10a2` | 128 | The weapon in a 'Mech's slot, or ff when none or out of ammunition | rebuilt |
| 1e31:1122 | 0x1f432 | `FUN_1e31_1122` | 137 | Damage transfer to the next location inward | rebuilt |
| 1e31:11ab | 0x1f4bb | `FUN_1e31_11ab` | 1103 | Critical hits in a location | rebuilt |
| 1e31:15fa | 0x1f90a | `FUN_1e31_15fa` | 68 | Counts a run of slots' intact items | rebuilt |
| 1e31:163e | 0x1f94e | `FUN_1e31_163e` | 109 | Destroys items in a run of a 'Mech's slots (docs/combat.md) | rebuilt |
| 1e31:16ab | 0x1f9bb | `FUN_1e31_16ab` | 1177 | Is a place taken? Party characters, townspeople, 'Mechs (3 half-cells); 'Mechs also crush (combat.md 3.4) | rebuilt |
| 1e31:1b44 | 0x1fe54 | `FUN_1e31_1b44` | 75 |  | - |
| 1e31:1b8f | 0x1fe9f | `FUN_1e31_1b8f` | 111 | Prints a hit location's name and "." as one string (docs/combat.md 3.8) | rebuilt |
| 1e31:1bfe | 0x1ff0e | `FUN_1e31_1bfe` | 429 | Line of sight from the party's position to a figure | rebuilt |
| 1e31:1dab | 0x200bb | `FUN_1e31_1dab` | 33 | Prints a text into the current window when combat messages are on (DG:2e38) (docs/combat.md 3.8) | rebuilt |
| 1e31:1dcc | 0x200dc | `FUN_1e31_1dcc` | 44 | The pause after a shot: 12 x DG:015e frames, or a key from 5 on (docs/combat.md 3.8) | rebuilt in part (speed 5's wait for a key is held as frames) |
| 1e31:1df8 | 0x20108 | `FUN_1e31_1df8` | 170 | A place's text cell in the view centred on another, half-cell by half-cell (into F:e486/e488) | rebuilt |
| 1e31:1ea2 | 0x201b2 | `FUN_1e31_1ea2` | 103 | A line's octant from its y and x differences | rebuilt |
| 1e31:1f09 | 0x20219 | `FUN_1e31_1f09` | 106 | The shot's redraw: the saved cell window back, the view onto Jason, tiles' frame, view and figures | rebuilt (not shown) |
| 1e31:1f73 | 0x20283 | `FUN_1e31_1f73` | 108 | A sprite slot drawn on the off-screen page at a screen point (missile, explosion) | rebuilt |
| 1e31:1fdf | 0x202ef | `FUN_1e31_1fdf` | 203 | Panel: the money label and the amount in green, in window 3 | rebuilt |

## CODE_10: segment 203b, 10,962 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 203b:000a | 0x203ba | `FUN_203b_000a` | 5240 | The fight: set-up, engage question, the round loop, the end, the after-fight steps (docs/combat.md) | rebuilt |
| 203b:1482 | 0x21832 | `FUN_203b_1482` | 65 | The computer plans a side's active figures | rebuilt |
| 203b:14c3 | 0x21873 | `FUN_203b_14c3` | 689 | The player's orders menu for each unit; 1 = flee | rebuilt |
| 203b:1774 | 0x21b24 | `FUN_203b_1774` | 455 | A unit on the orders screen: the view on it, the box, its path as arrows | rebuilt |
| 203b:193b | 0x21ceb | `FUN_203b_193b` | 740 | A figure's path from its orders, and its movement cost | rebuilt |
| 203b:1c1f | 0x21fcf | `FUN_203b_1c1f` | 1554 | A move order: the type question, the reasons, the map cursor and one waypoint | rebuilt |
| 203b:2231 | 0x225e1 | `FUN_203b_2231` | 139 | The Kick order: the target picker for slot 11, with both hips | rebuilt |
| 203b:22bc | 0x2266c | `FUN_203b_22bc` | 440 | A 'Mech's movement points (walk/run/jump, damage, heat) | rebuilt |
| 203b:2474 | 0x22824 | `FUN_203b_2474` | 124 | A person's movement points | rebuilt |
| 203b:24f0 | 0x228a0 | `FUN_203b_24f0` | 102 | The combat-messages question: none, brief, verbose (docs/combat.md) | rebuilt |
| 203b:2556 | 0x22906 | `FUN_203b_2556` | 59 | The combat-graphics question (docs/combat.md) | rebuilt |
| 203b:2591 | 0x22941 | `FUN_203b_2591` | 428 | Scan Unit: Friends / Enemies / Cancel, then the scan list | rebuilt |
| 203b:273d | 0x22aed | `FUN_203b_273d` | 140 | A critical slot's location's internal structure (1 for model 200) | rebuilt |
| 203b:27c9 | 0x22b79 | `FUN_203b_27c9` | 108 | Adds a mark to the map's marks | rebuilt |
| 203b:2835 | 0x22be5 | `FUN_203b_2835` | 166 | After an encounter: the nine areas' maps loaded again where a grid's first entry isn't 0x90, then the masks | rebuilt |
| 203b:28db | 0x22c8b | `FUN_203b_28db` | 456 | Can Jason reach the first enemy: 30 steps of 1e31:0006 in mode 0x80 (docs/combat.md 2.3) | rebuilt |
| 203b:2aa3 | 0x22e53 | `FUN_203b_2aa3` | 57 | The view walked back to a place (1000:17bb) and the window built there | rebuilt |

## CODE_11: segment 22e8, 7,848 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 22e8:000c | 0x22e8c | `FUN_22e8_000c` | 4795 | One combat round: 12 phases of moving and firing; to-hit, location, damage (docs/combat.md) | rebuilt |
| 22e8:12c7 | 0x24147 | `FUN_22e8_12c7` | 2943 | A shot's animation and sound; kills, wrecks, the leader's death | rebuilt |
| 22e8:1e46 | 0x24cc6 | `FUN_22e8_1e46` | 110 | On a miss, the fire check: on 2d6 outside 5-9, a fire mark (0x7c) beside the target | rebuilt |

## CODE_12: segment 24d3, 6,191 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 24d3:0004 | 0x24d34 | `FUN_24d3_0004` | 6082 | The script specials, F5 n (docs/scripts.md); the rebuilt ones are listed there | rebuilt |
| 24d3:17c6 | 0x264f6 | `FUN_24d3_17c6` | 36 | Opens the message box over the map: window 7, cleared, frame style 0 | rebuilt |
| 24d3:17ea | 0x2651a | `FUN_24d3_17ea` | 31 | Prints a message in the open box, waits a moment (273d:086a) | rebuilt |
| 24d3:1809 | 0x26539 | `FUN_24d3_1809` | 21 | Prints DGROUP:4f7e (special 5) | rebuilt |
| 24d3:181e | 0x2654e | `FUN_24d3_181e` | 21 | Prints DGROUP:4fa0, a line end (talk) | rebuilt |

## CODE_13: segment 2656, 3,699 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 2656:0004 | 0x26564 | `FUN_2656_0004` | 483 | Frame around the current window, one cell outside it, from a style table of corner and edge pieces | rebuilt |
| 2656:01e7 | 0x26747 | `FUN_2656_01e7` | 154 | One frame edge: repeats a run of pieces (ended by 0xff) along a row or column | rebuilt |
| 2656:0281 | 0x267e1 | `FUN_2656_0281` | 263 | Selects text window N, keeping the old window's colours and cursor | rebuilt |
| 2656:0388 | 0x268e8 | `FUN_2656_0388` | 109 | Clears the current window to its background colour, cursor to the corner | rebuilt |
| 2656:03f5 | 0x26955 | `FUN_2656_03f5` | 982 | Text printer: word wrap into the current window; codes 0d newline, 02 bg, 06 fg, 09 column, 13 pad; top bit ends | rebuilt |
| 2656:07cb | 0x26d2b | `FUN_2656_07cb` | 624 | Puts one line in the current window at the cursor; past the bottom scrolls the window up a row | rebuilt |
| 2656:0a3b | 0x26f9b | `FUN_2656_0a3b` | 170 | Part of the text cell layout, with 2656:0ae5 | rebuilt |
| 2656:0ae5 | 0x27045 | `FUN_2656_0ae5` | 121 | Cuts N 8x8 cells from a picture, left to right from a cell position (frame pieces from BTBORDER.CMP) | rebuilt |
| 2656:0b5e | 0x270be | `FUN_2656_0b5e` | 447 | A menu: the bar over the remembered item, arrows, Enter (docs/scripts.md) | rebuilt |
| 2656:0d1d | 0x2727d | `FUN_2656_0d1d` | 345 | Maps keypad keys and letters to arrows, for menus | rebuilt |

## CODE_14: segment 273d, 2,172 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 273d:0006 | 0x273d6 | `FUN_273d_0006` | 41 | Waits N display frames (vertical retrace, 287f:0b40) | replaced |
| 273d:002f | 0x273ff | `FUN_273d_002f` | 36 | Is a key waiting? (DOS int 21h 0Bh through 287f:3bdc, or recorded keys) | rebuilt |
| 273d:0053 | 0x27423 | `FUN_273d_0053` | 51 | A number printed in decimal (itoa, then the printer) | rebuilt |
| 273d:0086 | 0x27456 | `FUN_273d_0086` | 79 | Copy a picture buffer to the screen (by display) | replaced |
| 273d:00d5 | 0x274a5 | `FUN_273d_00d5` | 294 | Writes a string straight onto the screen at (column, row) in two colours with the 8x8 font | rebuilt |
| 273d:01fb | 0x275cb | `FUN_273d_01fb` | 94 | A rectangle of pixels filled with a colour (x0, y0, x1, y1, colour) | rebuilt (as text cells) |
| 273d:0259 | 0x27629 | `FUN_273d_0259` | 195 | Reads a key (287f:0b8a), or plays one back from / records one into the key buffer (F:3938, F:458c; docs/startup.md, "The demo") | rebuilt (playback; the recorder is unreachable) |
| 273d:031c | 0x276ec | `FUN_273d_031c` | 207 | A horizontal or vertical line (here a point) in a colour, clipped to the screen, on the visible screen | rebuilt (a line shot's pixels) |
| 273d:03eb | 0x277bb | `FUN_273d_03eb` | 178 |  | - |
| 273d:049d | 0x2786d | `FUN_273d_049d` | 136 | Unpack a loaded .CMP by its type byte (CGA: reduce to 4 colours) | rebuilt |
| 273d:0525 | 0x278f5 | `FUN_273d_0525` | 151 | Set the 16-colour palette for the chosen display | rebuilt |
| 273d:05bc | 0x2798c | `FUN_273d_05bc` | 127 |  | - |
| 273d:063b | 0x27a0b | `FUN_273d_063b` | 136 | Reads a length-prefixed file (.BLD) into a buffer; asks for the disk if missing | rebuilt |
| 273d:06c3 | 0x27a93 | `FUN_273d_06c3` | 71 | The off-screen page's map columns copied to the screen | rebuilt |
| 273d:070a | 0x27ada | `FUN_273d_070a` | 266 | Cuts a rectangle of a picture into a sprite slot | rebuilt |
| 273d:0814 | 0x27be4 | `FUN_273d_0814` | 86 | Reads a file into a buffer, up to a limit (DEMOFILE, the cache's tiles, O0.ANM) | replaced (formats::read_file) |
| 273d:086a | 0x27c3a | `FUN_273d_086a` | 24 | Short pause after a message, then flushes the keyboard | rebuilt |

## CODE_15: segment 27c5, 2,155 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 27c5:0002 | 0x27c52 | `FUN_27c5_0002` | 673 | Finds sound effect n in the lists at DG:5008 and plays its entries, storing their words in the far data (docs/sound.md) | rebuilt (as tones, docs/sound.md) |
| 27c5:02a3 | 0x27ef3 | `FUN_27c5_02a3` | 72 | A note entry: its tone, code times (docs/sound.md) | rebuilt (as tones, docs/sound.md) |
| 27c5:02eb | 0x27f3b | `FUN_27c5_02eb` | 90 | A kind-1 entry: bursts with the entry's own words (docs/sound.md) | rebuilt (as tones, docs/sound.md) |
| 27c5:0345 | 0x27f95 | `FUN_27c5_0345` | 99 | A kind-2 sound entry: the sweep, count times (docs/sound.md) | rebuilt (as tones, docs/sound.md) |
| 27c5:03a8 | 0x27ff8 | `FUN_27c5_03a8` | 99 | A kind-3 entry: a sweep down (docs/sound.md) | rebuilt (as tones, docs/sound.md) |
| 27c5:040b | 0x2805b | `FUN_27c5_040b` | 99 | A kind-4 entry: a sweep up (docs/sound.md) | rebuilt (as tones, docs/sound.md) |
| 27c5:046e | 0x280be | `FUN_27c5_046e` | 13 |  | - |
| 27c5:047b | 0x280cb | `FUN_27c5_047b` | 105 | One tone: a divisor for a length (docs/sound.md) | rebuilt (as tones, docs/sound.md) |
| 27c5:04e4 | 0x28134 | `FUN_27c5_04e4` | 13 |  | - |
| 27c5:04f1 | 0x28141 | `FUN_27c5_04f1` | 169 | A burst on the speaker: off, a short wait, then flips (docs/sound.md) | rebuilt (as tones, docs/sound.md) |
| 27c5:059a | 0x281ea | `FUN_27c5_059a` | 169 | A kind-1 burst (docs/sound.md) | rebuilt (as tones, docs/sound.md) |
| 27c5:0643 | 0x28293 | `FUN_27c5_0643` | 179 | One sweep on the speaker; stores its five arguments back in F:0000, 39f4, 4000, 4034, 0062 (docs/sound.md) | rebuilt (as tones, docs/sound.md) |
| 27c5:06f6 | 0x28346 | `FUN_27c5_06f6` | 81 | The tone wait for a length (docs/sound.md) | rebuilt (as tones, docs/sound.md) |
| 27c5:0747 | 0x28397 | `FUN_27c5_0747` | 147 | A kind-3 sweep's steps (docs/sound.md) | rebuilt (as tones, docs/sound.md) |
| 27c5:07da | 0x2842a | `FUN_27c5_07da` | 147 | A kind-4 sweep's steps (docs/sound.md) | rebuilt (as tones, docs/sound.md) |

## CODE_16: segment 284b, 831 bytes

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 284b:0048 | 0x284f8 | `FUN_284b_0048` | 73 | Hooks int 8 and sets the timer to ~291 Hz for the music driver | replaced |
| 284b:0091 | 0x28541 | `FUN_284b_0091` | 44 | Restores int 8 and the timer | replaced |
| 284b:00bd | 0x2856d | `FUN_284b_00bd` | 551 | Speaker on; Ghidra's span also holds the driver's unmarked routines: speaker off (00c6), the note's divisor (0139, 00cf), the Tandy voices (01b8), start and stop (0160, 0179, 020b, 0233), the step routines (024f Tandy, 0298 speaker) (docs/sound.md, "The title music") | replaced; the steps rebuilt |
| 284b:02e4 | 0x28794 | `FUN_284b_02e4` | 34 | Starts (1) or stops (0) the tune on Tandy graphics: three voices and the speaker, a step every 20 ticks | read |
| 284b:0306 | 0x287b6 | `FUN_284b_0306` | 54 | Starts (1) or stops (0) the tune on the speaker, a step every 4 ticks | replaced |
| 284b:033c | 0x287ec | `FUN_284b_033c` | 16 | Whether the tune has ended (the step routine is the idle one again) | replaced |

## CODE_17: segment 287f, 16,068 bytes (Microsoft C run-time library; entry point here)

This segment also holds the program's own assembly graphics routines, linked in beside the
run-time library: the .CMP unpackers (22f8, 2368) and the display drivers' copy and palette
code (0163, 022a, 0260, 0fee, 200e, 245c). So a `replaced` here is not proof of library code:
read each one before taking it as such.

| Address | Linear | Ghidra name | Span | Label | Status |
|---|---|---|---|---|---|
| 287f:001c | 0x2880c | `FUN_287f_001c` | 20 |  | replaced |
| 287f:0030 | 0x28820 | `FUN_287f_0030` | 33 |  | replaced |
| 287f:0051 | 0x28841 | `FUN_287f_0051` | 22 |  | replaced |
| 287f:0067 | 0x28857 | `FUN_287f_0067` | 22 |  | replaced |
| 287f:007d | 0x2886d | `FUN_287f_007d` | 44 |  | replaced |
| 287f:00a9 | 0x28899 | `FUN_287f_00a9` | 40 |  | replaced |
| 287f:00d1 | 0x288c1 | `FUN_287f_00d1` | 123 | CGA: choose the colour-reduction table for the next picture (from its arguments) | replaced |
| 287f:014c | 0x2893c | `FUN_287f_014c` | 23 |  | replaced |
| 287f:0163 | 0x28953 | `FUN_287f_0163` | 116 | CGA: reduce a 16-colour picture to 4 colours through two row tables | replaced |
| 287f:01d7 | 0x289c7 | `FUN_287f_01d7` | 60 |  | replaced |
| 287f:0213 | 0x28a03 | `FUN_287f_0213` | 23 |  | replaced |
| 287f:022a | 0x28a1a | `FUN_287f_022a` | 54 | EGA: set one attribute palette register | replaced |
| 287f:0260 | 0x28a50 | `FUN_287f_0260` | 179 | EGA: write a packed picture into the four planes at A800 | replaced |
| 287f:0313 | 0x28b03 | `FUN_287f_0313` | 100 |  | replaced |
| 287f:0377 | 0x28b67 | `FUN_287f_0377` | 507 | Draws a sprite on the EGA screen, colour 0 see-through | replaced |
| 287f:0572 | 0x28d62 | `FUN_287f_0572` | 77 | Converts an 88x88 picture to EGA planes | replaced |
| 287f:05bf | 0x28daf | `FUN_287f_05bf` | 17 |  | replaced |
| 287f:05d0 | 0x28dc0 | `FUN_287f_05d0` | 432 |  | replaced |
| 287f:0780 | 0x28f70 | `FUN_287f_0780` | 289 |  | replaced |
| 287f:08a1 | 0x29091 | `FUN_287f_08a1` | 144 |  | replaced |
| 287f:0931 | 0x29121 | `FUN_287f_0931` | 64 |  | replaced |
| 287f:0971 | 0x29161 | `FUN_287f_0971` | 181 | Direction (0-7) from one position to another, -1 if the same | rebuilt |
| 287f:0a26 | 0x29216 | `FUN_287f_0a26` | 80 |  | replaced |
| 287f:0a76 | 0x29266 | `FUN_287f_0a76` | 41 |  | replaced |
| 287f:0a9f | 0x2928f | `FUN_287f_0a9f` | 135 |  | replaced |
| 287f:0b26 | 0x29316 | `FUN_287f_0b26` | 26 |  | replaced |
| 287f:0b40 | 0x29330 | `FUN_287f_0b40` | 51 |  | replaced |
| 287f:0b73 | 0x29363 | `FUN_287f_0b73` | 23 |  | replaced |
| 287f:0b8a | 0x2937a | `FUN_287f_0b8a` | 29 | BIOS int 16h function 0: wait for a key; character, or minus the scan code | replaced |
| 287f:0ba7 | 0x29397 | `FUN_287f_0ba7` | 25 |  | replaced |
| 287f:0bc0 | 0x293b0 | `FUN_287f_0bc0` | 59 | Random number: 3-byte shift register at DG:4fc0 | rebuilt |
| 287f:0bfb | 0x293eb | `FUN_287f_0bfb` | 268 | One area's 8x8 terrain types from its corner heights: midpoint displacement on a 9x9 lattice (docs/wilderness.md) | rebuilt |
| 287f:0d07 | 0x294f7 | `FUN_287f_0d07` | 114 | Midpoint displacement along a lattice row | rebuilt |
| 287f:0d79 | 0x29569 | `FUN_287f_0d79` | 127 | Midpoint displacement down a lattice column | rebuilt |
| 287f:0df8 | 0x295e8 | `FUN_287f_0df8` | 502 | Midpoint displacement inside the lattice: centres, then sides (bug 37) | rebuilt |
| 287f:0fee | 0x297de | `FUN_287f_0fee` | 96 | MCGA: set the 16 DAC colours from 12-bit table words | replaced |
| 287f:104e | 0x2983e | `FUN_287f_104e` | 365 | The nine area grids around an area, from the world heights; then the masks (docs/wilderness.md) | rebuilt |
| 287f:11bb | 0x299ab | `FUN_287f_11bb` | 255 | One grid's neighbour masks (map blocks: their store number) | rebuilt |
| 287f:12ba | 0x29aaa | `FUN_287f_12ba` | 31 |  | replaced |
| 287f:12d9 | 0x29ac9 | `FUN_287f_12d9` | 25 |  | replaced |
| 287f:12f2 | 0x29ae2 | `FUN_287f_12f2` | 17 |  | replaced |
| 287f:1303 | 0x29af3 | `FUN_287f_1303` | 17 |  | replaced |
| 287f:1314 | 0x29b04 | `FUN_287f_1314` | 197 | The cell window: the 3x3 blocks around the position's block (game/terrain.cpp) | rebuilt |
| 287f:13d9 | 0x29bc9 | `FUN_287f_13d9` | 435 | One block into the cell window: a map block as it is, a terrain block from its template, turned (docs/wilderness.md) | rebuilt |
| 287f:158c | 0x29d7c | `FUN_287f_158c` | 175 | Position one half-cell up; into the area above at the edge (not past the world's) | rebuilt |
| 287f:163b | 0x29e2b | `FUN_287f_163b` | 168 | Position one half-cell down; into the area below at the edge | rebuilt |
| 287f:16e3 | 0x29ed3 | `FUN_287f_16e3` | 226 | Position one half-cell left; into the area to the left at the edge | rebuilt |
| 287f:17c5 | 0x29fb5 | `FUN_287f_17c5` | 193 | Position one half-cell right; into the area to the right at the edge | rebuilt |
| 287f:1886 | 0x2a076 | `FUN_287f_1886` | 82 | The nine grids' masks (11bb for each) | rebuilt |
| 287f:18d8 | 0x2a0c8 | `FUN_287f_18d8` | 23 | An area's four corner heights into the lattice | rebuilt |
| 287f:18ef | 0x2a0df | `FUN_287f_18ef` | 441 | Draw the map view: 13x12 tiles plus half tiles at the edges | rebuilt |
| 287f:1aa8 | 0x2a298 | `FUN_287f_1aa8` | 38 | Draw a half-width tile (by display) | replaced |
| 287f:1ace | 0x2a2be | `FUN_287f_1ace` | 38 | Draw a half-height tile (by display) | replaced |
| 287f:1af4 | 0x2a2e4 | `FUN_287f_1af4` | 38 | Draw a whole tile: offset = tile number x 128 into the sheet (by display) | replaced |
| 287f:1b1a | 0x2a30a | `FUN_287f_1b1a` | 87 | Copy a 16x16 tile into the screen buffer (CGA, Tandy, MCGA) | rebuilt |
| 287f:1b71 | 0x2a361 | `FUN_287f_1b71` | 35 | Draw a whole tile on EGA (from its caller) | replaced |
| 287f:1b94 | 0x2a384 | `FUN_287f_1b94` | 75 | Copy a half-height tile into the screen buffer (from its caller) | replaced |
| 287f:1bdf | 0x2a3cf | `FUN_287f_1bdf` | 29 |  | replaced |
| 287f:1bfc | 0x2a3ec | `FUN_287f_1bfc` | 135 | Copy a half-width tile into the screen buffer: right half when DG:a450 is 0 | replaced |
| 287f:1c83 | 0x2a473 | `FUN_287f_1c83` | 53 |  | replaced |
| 287f:1cb8 | 0x2a4a8 | `FUN_287f_1cb8` | 130 |  | replaced |
| 287f:1d3a | 0x2a52a | `FUN_287f_1d3a` | 82 |  | replaced |
| 287f:1d8c | 0x2a57c | `FUN_287f_1d8c` | 28 |  | replaced |
| 287f:1da8 | 0x2a598 | `FUN_287f_1da8` | 19 | Far call of 1886: rebuild the masks (after a map load) | rebuilt |
| 287f:1dbb | 0x2a5ab | `FUN_287f_1dbb` | 61 |  | replaced |
| 287f:1df8 | 0x2a5e8 | `FUN_287f_1df8` | 63 | The view's corner words P:09ed..09f1 from the party position | replaced |
| 287f:1e37 | 0x2a627 | `FUN_287f_1e37` | 151 | Blits a picture to the screen | replaced |
| 287f:1ece | 0x2a6be | `FUN_287f_1ece` | 54 | Saves the window's 576 cells to a far buffer, or copies them back (docs/combat.md 2.11) | rebuilt (`Terrain::save_cells`, `restore_cells`) |
| 287f:1f04 | 0x2a6f4 | `FUN_287f_1f04` | 77 | Copies the middle grid, with masks, into the overhead map's view (stride 40) | rebuilt |
| 287f:1f51 | 0x2a741 | `FUN_287f_1f51` | 75 | A store block's 64 cells as an 8x8 tile, a pixel a cell, colour from P:215d | replaced (platform/overhead_layer.cpp) |
| 287f:1f9c | 0x2a78c | `FUN_287f_1f9c` | 17 |  | replaced |
| 287f:1fad | 0x2a79d | `FUN_287f_1fad` | 17 |  | replaced |
| 287f:1fbe | 0x2a7ae | `FUN_287f_1fbe` | 80 |  | replaced |
| 287f:200e | 0x2a7fe | `FUN_287f_200e` | 281 | Copy a packed picture to CGA, Tandy or MCGA screen memory | replaced |
| 287f:2127 | 0x2a917 | `FUN_287f_2127` | 129 |  | replaced |
| 287f:21a8 | 0x2a998 | `FUN_287f_21a8` | 97 | Draws one font character on Tandy | replaced |
| 287f:2209 | 0x2a9f9 | `FUN_287f_2209` | 72 | Draws one font character on CGA | replaced |
| 287f:2251 | 0x2aa41 | `FUN_287f_2251` | 84 | Draws one font character on EGA | replaced |
| 287f:22a5 | 0x2aa95 | `FUN_287f_22a5` | 83 |  | replaced |
| 287f:22f8 | 0x2aae8 | `FUN_287f_22f8` | 112 | Unpack .CMP runs in row order | rebuilt |
| 287f:2368 | 0x2ab58 | `FUN_287f_2368` | 132 | Unpack .CMP runs in column order | rebuilt |
| 287f:23ec | 0x2abdc | `FUN_287f_23ec` | 112 | Decodes an .ANM frame: runs XORed over the last (docs/formats/anm.md) | replaced |
| 287f:245c | 0x2ac4c | `FUN_287f_245c` | 123 | EGA: copy the A800 page to the screen at A000 (from its call site) | replaced |
| 287f:24d7 | 0x2acc7 | `FUN_287f_24d7` | 645 |  | replaced |
| 287f:275c | 0x2af4c | `FUN_287f_275c` | 332 |  | replaced |
| 287f:28a8 | 0x2b098 | `FUN_287f_28a8` | 67 |  | replaced |
| 287f:28eb | 0x2b0db | `FUN_287f_28eb` | 602 |  | replaced |
| 287f:2b45 | 0x2b335 | `FUN_287f_2b45` | 66 |  | replaced |
| 287f:2b87 | 0x2b377 | `FUN_287f_2b87` | 346 | XORs a colour over a text row (the menu bar) | replaced |
| 287f:2ce1 | 0x2b4d1 | `FUN_287f_2ce1` | 22 |  | replaced |
| 287f:2cf7 | 0x2b4e7 | `FUN_287f_2cf7` | 109 | Widens the font for the CGA and Tandy drivers | replaced |
| 287f:2d64 | 0x2b554 | `FUN_287f_2d64` | 30 |  | replaced |
| 287f:2d82 | 0x2b572 | `entry` | 402 |  | replaced |
| 287f:2f14 | 0x2b704 | `FUN_287f_2f14` | 94 |  | replaced |
| 287f:2f72 | 0x2b762 | `FUN_287f_2f72` | 45 |  | replaced |
| 287f:2f9f | 0x2b78f | `FUN_287f_2f9f` | 19 |  | replaced |
| 287f:2fb2 | 0x2b7a2 | `FUN_287f_2fb2` | 36 |  | replaced |
| 287f:2fd6 | 0x2b7c6 | `FUN_287f_2fd6` | 6 |  | replaced |
| 287f:2fdc | 0x2b7cc | `FUN_287f_2fdc` | 36 |  | replaced |
| 287f:3000 | 0x2b7f0 | `FUN_287f_3000` | 38 |  | replaced |
| 287f:3026 | 0x2b816 | `FUN_287f_3026` | 424 |  | replaced |
| 287f:31ce | 0x2b9be | `FUN_287f_31ce` | 124 |  | replaced |
| 287f:324a | 0x2ba3a | `FUN_287f_324a` | 43 |  | replaced |
| 287f:3275 | 0x2ba65 | `FUN_287f_3275` | 43 |  | replaced |
| 287f:32a0 | 0x2ba90 | `FUN_287f_32a0` | 85 |  | replaced |
| 287f:32f5 | 0x2bae5 | `FUN_287f_32f5` | 19 |  | replaced |
| 287f:3308 | 0x2baf8 | `FUN_287f_3308` | 46 |  | replaced |
| 287f:3336 | 0x2bb26 | `FUN_287f_3336` | 32 |  | replaced |
| 287f:3356 | 0x2bb46 | `FUN_287f_3356` | 122 |  | replaced |
| 287f:33d0 | 0x2bbc0 | `FUN_287f_33d0` | 415 |  | replaced |
| 287f:356f | 0x2bd5f | `FUN_287f_356f` | 17 |  | replaced |
| 287f:3580 | 0x2bd70 | `FUN_287f_3580` | 234 |  | replaced |
| 287f:366a | 0x2be5a | `FUN_287f_366a` | 180 |  | replaced |
| 287f:371e | 0x2bf0e | `FUN_287f_371e` | 75 |  | replaced |
| 287f:3769 | 0x2bf59 | `FUN_287f_3769` | 17 |  | replaced |
| 287f:377a | 0x2bf6a | `FUN_287f_377a` | 58 |  | replaced |
| 287f:37b4 | 0x2bfa4 | `FUN_287f_37b4` | 38 |  | replaced |
| 287f:37da | 0x2bfca | `FUN_287f_37da` | 70 |  | replaced |
| 287f:3820 | 0x2c010 | `FUN_287f_3820` | 21 |  | replaced |
| 287f:3835 | 0x2c025 | `FUN_287f_3835` | 63 |  | replaced |
| 287f:3874 | 0x2c064 | `FUN_287f_3874` | 110 |  | replaced |
| 287f:38e2 | 0x2c0d2 | `FUN_287f_38e2` | 27 |  | replaced |
| 287f:38fd | 0x2c0ed | `FUN_287f_38fd` | 227 |  | replaced |
| 287f:39e0 | 0x2c1d0 | `FUN_287f_39e0` | 58 |  | replaced |
| 287f:3a1a | 0x2c20a | `FUN_287f_3a1a` | 34 |  | replaced |
| 287f:3a3c | 0x2c22c | `FUN_287f_3a3c` | 34 |  | replaced |
| 287f:3a5e | 0x2c24e | `FUN_287f_3a5e` | 110 |  | replaced |
| 287f:3acc | 0x2c2bc | `FUN_287f_3acc` | 86 |  | replaced |
| 287f:3b22 | 0x2c312 | `FUN_287f_3b22` | 70 |  | replaced |
| 287f:3b68 | 0x2c358 | `FUN_287f_3b68` | 54 |  | replaced |
| 287f:3b9e | 0x2c38e | `FUN_287f_3b9e` | 24 |  | replaced |
| 287f:3bb6 | 0x2c3a6 | `FUN_287f_3bb6` | 28 |  | replaced |
| 287f:3bd2 | 0x2c3c2 | `FUN_287f_3bd2` | 10 |  | replaced |
| 287f:3bdc | 0x2c3cc | `FUN_287f_3bdc` | 44 | DOS int 21h function 0Bh: is a key waiting? | replaced |
| 287f:3c08 | 0x2c3f8 | `FUN_287f_3c08` | 100 |  | replaced |
| 287f:3c6c | 0x2c45c | `FUN_287f_3c6c` | 22 |  | replaced |
| 287f:3c82 | 0x2c472 | `FUN_287f_3c82` | 154 |  | replaced |
| 287f:3d1c | 0x2c50c | `FUN_287f_3d1c` | 40 |  | replaced |
| 287f:3d44 | 0x2c534 | `FUN_287f_3d44` | 40 |  | replaced |
| 287f:3d6c | 0x2c55c | `FUN_287f_3d6c` | 38 |  | replaced |
| 287f:3d92 | 0x2c582 | `FUN_287f_3d92` | 156 |  | replaced |
| 287f:3e2e | 0x2c61e | `FUN_287f_3e2e` | 52 |  | replaced |
| 287f:3e62 | 0x2c652 | `FUN_287f_3e62` | 98 |  | replaced |
| 287f:3ec4 | 0x2c6b4 | `FUN_287f_3ec4` | 12 |  | replaced |
