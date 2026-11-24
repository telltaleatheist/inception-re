# The small tables (tables.json)

The layouts, offsets, masks and scripts built into BTECH.EXE that the screen, the town and the
fight read. The prefixes DG: (DGROUP, file segment 46db), P: (2c6c) and 37e8:/385b: are the
program's segments as loaded at 0x1000 (see [combat.md](../combat.md)).

**None of these tables is shipped.** The importer ([import.md](../import.md), import version 10)
reads them from the player's own BTECH.EXE, each through a hashed span `tables.<name>` in the
import table (`src/formats/exe_data.cpp`), and writes `tables.json` in the player's data folder
(`formats/tables.h`). The game reads only that file, so a mod can change any of them. The repo
holds the names, addresses and shapes on this page, never the values. A test checks that the file
gives back exactly what was read from the program.

## The file

```
{"import_version": N,
 "tables":       {"<name>": {"address": "ssss:oooo", "values": [...]}, ...},
 "colours":      {"<name>": {"address": "ssss:oooo", "values": ["#rrggbb", ...]}, ...},
 "windows":      {"address": "385b:0000", "records": [{"x", "y", "width", "height", "fg", "bg", "col", "row"}, ...]},
 "menus":        {"address": "385b:0092", "records": [{"first_row", "width", "items", "colour", "highlight"}, ...]},
 "frame_styles": {"address": "46db:4fa4", "by": "frame_pieces", "values": [...]}}
```

A value is read back only if it fits the way the program stored it (a byte, a signed byte or a
word); anything else is refused, and the game asks for a new import. The `colours` are what the
copy shows for each colour number (DOS's numbers), "#rrggbb": for DOS, the standard EGA colours
(formats/palette.h). Another version's copy has its own.

## The tables

| Name | Address | Shape | Read by |
|---|---|---|---|
| `palette` | 37e8:0000 | 16 bytes, read as 16 colours | the game's colours by colour number: the program stores EGA colour numbers (formats/palette.h) |
| `endmech_palette` | 3858:0000 | 16 bytes, read as 16 colours | the same, for ENDMECH.CMP only (special 45, docs/formats/cmp.md) |
| `windows` | 385b:0000 | 11 records of 8 words | the text windows: inside's position and size, colours, cursor (game/text.h) |
| `frame_pieces_0..2` | 385b:0320/0338/0350 | 12 words each | a frame's corners, then its edges, each run ended by 0xff (2656 frame drawing) |
| `frame_styles` | DG:4fa4 | 5 far pointers | which piece table each frame style uses; written as the table's number |
| `menus` | 385b:0092 | 41 records of 16 bytes | a menu's first row, bar width, item count, colour (+8), bar colour; +6, the item last chosen, is state and not imported (docs/scripts.md, "Menus") |
| `seen_bits` | DG:029a | 8 bytes | the seen map's bits by x's bits 4-6 (docs/wilderness.md, "The seen map") |
| `money_loss_mask` | DG:02a2 | 3 words | per investment, the bits that must all be clear for a loss (game/money.h) |
| `money_rate` | DG:02a8 | 3 words | per investment, the rate |
| `animated_tiles` | DG:04b0 | 10 bytes | the tiles that cycle through ANIMATE.ICN (game/tile_animation.h) |
| `walking_scripts` | 37e8:0270 | 0x80 bytes | the figures' walking frames, 'Mechs then characters ([town_loop.md](../town_loop.md)) |
| `walking_keys` | DG:0160 | 8 words | the walking key for each direction, north first |
| `formation_x`, `formation_y` | DG:0464/047c | 8 words each | the k-th character's half-cells from the party (game/movement.h) |
| `party_water` | DG:0332 | 8 x 4 words | the k-th figure's water quarter bits by half-cell parity (1000:051b) |
| `place_pictures` | DG:141a | 26 bytes | by script number, the O<n>.ANM picture a place opens with, or 0 |
| `picture_chance` | DG:0ac4 | 3 words | by flag 4f, the mask a random number must clear for a place's picture |
| `treatment_cost` | DG:4f6e | 8 signed words | HOSPITAL's treatment by flag 1a (1-7), in C-Bills (special 24; 0 unused) |
| `upgrade_price` | DG:1d8c | 9 signed words | the 'Mech upgrade by case (flag 11, 0-8; special 19 sets F:0076 from it) |
| `upgrade_armour` | DG:1e24 | 11 bytes | upgrade case 3's armour bytes 0x11-0x1b, full and current (DG:1e13 + byte) |
| `ammo_price` | DG:2060 | 7 signed bytes | a shot of ammunition for slot codes 0x1a-0x20 (special 44 reads DG:2046 + w; code 0x18 is 2 in the code) |
| `direction_of` | P:0240 | 16 bytes | 287f:0971's four tests (north 8, south 4, east 2, west 1) to a direction |
| `step_x`, `step_y` | DG:311a/312a | 8 words each | one half-cell in each direction (the townspeople) |
| `turns` | DG:310a | 16 bytes | the turns 1e31:0006 tries, read from the end, two mirrored patterns |
| `person_water` | DG:032e | 4 bytes | a townsperson's water quarter bits by parity |
| `mech_step_x`, `mech_step_y` | DG:045c/0474 | 4 words each | the k-th 'Mech's half-cells from the party, in the step check (1000:1c12; docs/movement.md) |
| `marked_spot_x`, `marked_spot_y` | DG:1632/163a | 8 bytes each | training mission 1's spots, a cell column and row in the area (17dc:0678; docs/combat.md 1.3) |
| `mech_formation_x`, `_y` | DG:3a26/3a2a | 4 bytes each | the k-th 'Mech's half-cells from the party in a fight |
| `mech_cell` | DG:02de | 4 words | the window cell under the k-th 'Mech (1000:051b) |
| `mech_water` | DG:02e6 | 4 words | its water quarter bits |
| `mech_cell_odd_x`, `_odd_y` | DG:02ee/02f6 | 4 words each | added to the cell when the party's x or y is odd |
| `enemy_offset_x`, `_y` | DG:1642/164e | 12 bytes each | 17dc:0d49's places for the enemy 'Mechs |
| `los_step_x`, `_y` | DG:328a/329a | 8 words each | 1e31:1bfe's line-of-sight step in each direction |
| `los_area_x`, `_y` | DG:32aa/32ba | 8 words each | added to x or y when that step crosses into another area |
| `los_row` | DG:32ca | 8 words | how far a y step moves through the cell window |
| `step_direction` | DG:2ecc | 11 bytes | by (dy + 1) x 4 + dx + 1, the direction of a path step (22e8:000c) |
| `pose_scripts` | DG:3ef0 | 0x108 bytes | the scripts of the shot's poses: a 'Mech firing and kicking (two sprite sets), a person firing, the missile (22e8:12c7) |
| `mech_poses` | DG:2d58 | 40 far pointers | into them by direction: fire, fire with the other sprites, kick, kick with the other sprites, missile |
| `person_poses` | DG:3ff8 | 120 far pointers | a person's fire pose, 8 directions for each weapon 0-0x0e |
| `reaction_script` | DG:2e3c | 6 bytes | a 'Mech or person hit: its reaction (it loops) |
| `explosion_script` | DG:41d8 | 4 bytes | the explosion over a target hit |
| `spectator_script` | DG:4dd8 | 3 bytes | the Arena's spectator: one frame, held (17dc:0d49) |
| `encounter_templates` | DG:13e2 | 11 far pointers | 'Mech templates by index (docs/formats/mech.md); the Arena uses 3-6 |
| `encounter_sprite_sets` | DG:140e | 11 bytes | the sprite set for each |
| `arena_wall` | DG:2b8e | 38 bytes | the cells 1d43:0c72 writes over map-store cells 0xc00-0xc25, 0 = left alone |
| `muzzle` | DG:2d28 | 48 bytes | a line shot's start by direction, x then y: a 'Mech, a 'Mech with the other sprite set, a person |
| `line_steps` | DG:41dc | 32 bytes | by octant (1e31:1ea2): the straight step's x and y, then the diagonal step's |
| `person_quarters` | DG:0372 | 4 words | by parity, the tile bits of a person's cell (1000:0e4b) |
| `path_arrows` | DG:3b06 | 12 bytes | by (dy + 1) x 4 + dx + 1, the glyph of a step of the path the orders screen shows (203b:1774) |
| `stats_bar_structure` | DG:1306 | 11 words | the 'Mech stats screen (15ab:1afe): each bar's structure byte in the 'Mech record, 0 for none (docs/town_loop.md) |
| `stats_bar_x`, `stats_bar_y` | DG:131c, DG:1332 | 11 words each | each bar's x and bottom row (15ab:174c) |
| `stats_palette` | DG:1348 | 16 bytes, read as 16 colours | the stats screen's colours, set every 16th frame |
| `stats_pulse` | DG:1378 | 4 bytes, read as 4 colours | colour 4, step by step |
| `world_heights` | 2c6c:0b0b | 256 bytes | one height per area of the 16 x 16 world, row by row (287f:104e; docs/wilderness.md) |
| `terrain_templates` | 2c6c:0c1d | 1024 bytes | 16 blocks of 8 x 8, the block store's first 16: a terrain block's tiles by its neighbour mask (287f:13d9) |
| `plain_block` | 2c6c:209d | 64 bytes | the one block every type-0x10 terrain block shows |
| `mirror_east_west` | 2c6c:211d | 32 bytes | a template byte (or a mask) with its east and west swapped, for a mirrored block |
| `mirror_north_south` | 2c6c:213d | 32 bytes | the same with north and south |
| `overhead_colours` | 2c6c:215d | 3 x 250 bytes | the overhead map's cell colour by tile: the land's, then cache levels 0 and 1 (P:2257, 2351; 1b5d:0327 swaps them in; docs/overhead_map.md) |
| `overhead_block_x`, `_y` | DG:0a38/0a46 | 13 bytes each | by map - 1, the block column and row the overhead map puts the map at in its grid (1000:3fae) |
| `overhead_blocks_across`, `_down` | DG:0a54/0a62 | 13 bytes each | by map - 1, its size in blocks |
| `overhead_cell_bytes` | DG:0a70 | 13 words | by map - 1, how many bytes of cells the overhead map reads from the map's file, and how far its store moves on (bug 39) |
| `sound_lists` | DG:5008 | 314 words | the 18 sound effects' entries back to back, ending with a 0 word (27c5:0002, docs/sound.md) |
| `overhead_pointer_column`, `_row` | DG:0a8a/0a92 | 8 bytes each | by direction, the text cell of the overhead map's pointer |
| `path_quarters` | DG:3b12 | 4 bytes | by the half-cell's parity, the tile bits that make a path step cost more (203b:193b) |
| `leg_actuator_bit`, `_keep` | DG:316a/3172 | 8 bytes each | by a d6, a leg actuator a critical hit takes out, and the mask that clears it (1e31:11ab) |
| `arm_actuator_bit`, `_keep` | DG:317a/3182 | 8 bytes each | the same for an arm |
| `location_slot_first`, `_count` | DG:318a/3192 | 8 bytes each | by internal location - 0x1c, its item bytes in the 'Mech record |

## What stays in the code, and why

- **The sprite cut list.** The sprite slots of MECHSHAP.CMP are cut by a run of calls to
  1527:0044, each with its rectangle as operands (formats/sprite_sheet.h). It is code, not a
  table, so the rebuild keeps it as logic; a mod replaces the sprites through the asset provider.
- **The .BLD file names** (`kBldFiles`, formats/bld.h): the names of the player's files, like any
  asset ID, not something the game shows or a rule.
- **Single numbers** set by code, such as the blocking tile (DG:0150, set by 1000:2da8) and the
  sprite bases (F:d562, set at 1000:4f0f). They are operands of the instructions that store
  them, documented beside the function that stands in for that code.
- **Tables the rebuild works out itself.** DG:02ae/02be/02fe (where the k-th figure is drawn) are
  filled by the program from the formation table; the rebuild computes them from `formation_x/y`.
- **Our own IDs**, such as the town's text ID list (game/town.cpp): they name texts in
  `text/english.json` and are not part of the program.
