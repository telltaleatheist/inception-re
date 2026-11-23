# The 'Mech record and the combat tables

The 0x7d-byte 'Mech record, the eight 'Mech templates and the other tables in BTECH.EXE that
combat reads. How they are used is in [combat.md](../combat.md); the prefixes DG:, F:, P: and
27e8: are defined there.

**None of these tables is shipped.** The importer (docs/import.md) reads them from the player's
own BTECH.EXE (version-checked, at the addresses below) and writes plain data files in the
player's data folder: the templates to `rules/mechs.json`, the weapon table to
`rules/weapons.json`, the rest to `rules/combat.json` (`formats/rules.h`, import version 7). The
repo holds only the layouts and addresses on this page. The few numbers quoted are examples.

**The files.** Every entry carries its address in the program. In `mechs.json` each template has
an `id` (locust, wasp, stinger, commando, chameleon, jenner, spectator, urbanmech, in the
program's order), its 16 name bytes as stored (`record_name`, part of the record) and one key per
field of the table below: `tonnage`, `armour`, `structure`, `actuators`, `heat_sinks`, `ammo`,
`walk_mp`, `jump_mp`, `slots`, `max_armour`, `max_structure`, `start_actuators`, `start_ammo`,
`engine_hits`, `gyro_hits`, `sensor_hits`, `byte_78`, `pilot`, `second_pilot`, `model`,
`byte_7c`. In `weapons.json` each weapon has its `number`, `personal`, then `damage` (or, for
people's weapons, `damage_fixed` and `damage_d6`), `shots` (the cluster column for missiles),
`heat`, `min_range`, `short`, `medium`, `long` and `skill`: the packed bytes split into their
nibbles and bit fields. `combat.json` has `hit_locations` (location 0-10, not the armour byte),
`clusters`, `facing_sides`, `target_movement`, `difficulty_armour` and `personal_armour`. The
names the player sees are texts, `mech.<id>` and `weapon.<n>`, in `text/english.json`. The game
keeps the records as the original's bytes in memory; a test checks that the files give those
bytes back exactly.

## Where the records live

Eight records at F:c724 + 0x7d m:
- m 0-3: the party's 'Mechs (figures 0-3);
- m 4-7: the enemy's (figures 12-15).

A record is filled by copying a template whole (training special 1, `17dc:0d49`, `15ab:0d3d`,
`17dc:1c9b`) or a wreck (salvage, `15ab:04f9`). Byte 0 = ff marks an empty slot. Saved copies:
F:3780 ('Mech 0 during the Arena), F:d452 + m (first bytes while Jason is alone, mode 9),
F:323e + m (the first letter of a destroyed 'Mech, read by salvage).

## The record, field by field

| offset | size | meaning | evidence | confidence |
|---|---|---|---|---|
| 00 | 1 | first letter of the name ('L' Locust, 'W' Wasp, ...); ff = no 'Mech | salvage copies F:323e here; the training code tests 'L' | confirmed |
| 01-0e | 14 | the rest of the name, padded with spaces | the scan list prints the record as a string; salvage appends it | confirmed |
| 0f | 1 | 0, ends the name | templates | confirmed |
| 10 | 1 | tonnage (the Locust 20) | the Kick does tonnage / 5; matches the canonical weights | confirmed use, name a guess |
| 11-1b | 11 | armour: 11 LA, 12 LT, 13 LL, 14 head, 15 CT, 16 RA, 17 RT, 18 RL, 19 LT rear, 1a CT rear, 1b RT rear | the location table and names (DG:3242); the transfer table | confirmed |
| 1c-23 | 8 | internal structure: 1c LA, 1d LT, 1e LL, 1f head, 20 CT, 21 RA, 22 RT, 23 RL | transfer (armour + 0x0b), the slot map, the board game's 20-ton values for the Locust | confirmed |
| 24 | 1 | left actuators: high nibble the arm (bits 0x80, 0x40, 0x20, 0x10), low nibble the leg (bit 8 the hip, then 4, 2, 1); set = working | movement points, Kick, criticals | confirmed mechanics; which bit is which actuator a guess |
| 25 | 1 | right actuators, the same layout | as 24 | as 24 |
| 26 | 1 | heat sinks in the engine (not in slots) | end-of-round heat; with the slot heat sinks every template makes 10 | confirmed |
| 27-30 | 10 | ammunition of weapon slots 0-9 in item order; ff = unlimited (energy weapons), 0 = out | `1e31:10a2`, Use Weapons (the full and out labels), decremented when firing | confirmed |
| 31 | 1 | walking MP (the Locust 8) | `203b:22bc` | confirmed |
| 32 | 1 | jumping MP; 0 = no jump jets | `203b:22bc`, the no-jump-jets line (`orders.no_jump_jets`) | confirmed |
| 33-55 | 35 | critical slots (below); item code, bit 0x80 = destroyed, 0 = empty | `203b:273d`, `1e31:11ab`, repairs | confirmed |
| 56-60 | 11 | maximum armour, at 0x11-0x1b + 0x45 | repairs | confirmed |
| 61-68 | 8 | maximum structure, at 0x1c-0x23 + 0x45 | repairs | confirmed |
| 69, 6a | 2 | the original actuator bytes (copies of 24, 25) | templates | guess |
| 6b-74 | 10 | starting ammunition (the maximum for 27-30) | templates; the UrbanMech has stray values here | guess |
| 75 | 1 | engine hits; 3 destroys the 'Mech, blocks salvage (unless Tech 4); + 5 heat each round per hit | criticals, salvage, heat, the stats screen's three pips | confirmed |
| 76 | 1 | gyro hits; 2 destroys the 'Mech, blocks salvage (unless Tech 4) | criticals, salvage, two pips | confirmed |
| 77 | 1 | sensor hits: 1 = +2 to hit and a warning, 2 = cannot fire or scan 'Mechs | criticals, to-hit, Use Weapons, Scan | confirmed |
| 78 | 1 | 1 in every template; a head critical sets it to ff | `1e31:11ab` | unknown (life support is a guess) |
| 79 | 1 | the pilot (character number) | ejection, the to-hit skill, salvage | confirmed |
| 7a | 1 | a second occupant (character number), ff = none | ejection, the double agent | confirmed |
| 7b | 1 | model number (Locust 0, Wasp 1, Stinger 2, Commando 3, UrbanMech 6, Jenner 9, Chameleon 200); 200 makes every location count as intact for repairs | `203b:273d` | confirmed for 200; the rest is data |
| 7c | 1 | 0 in every template | | unknown |

## The critical slots

Item bytes 0x33-0x55, by location. `203b:273d` maps a slot to its location; `1e31:11ab` uses the
start and count tables DG:316e and DG:3176 (indexed by the location's structure offset).

| location | structure byte | slots | count |
|---|---|---|---|
| left arm | 1c | 33-39 | 7 |
| left torso | 1d | 3a-40 | 7 |
| right arm | 21 | 41-47 | 7 |
| right torso | 22 | 48-4e | 7 |
| left leg | 1e | 4f-50 | 2 |
| right leg | 23 | 51-52 | 2 |
| centre torso | 20 | 53-54 | 2 |
| head | 1f | 55 | 1 |

The engine, gyro, cockpit, sensors and actuators have no slots: they are the hit counters
0x75-0x77, byte 0x78 and the actuator bits. A location whose structure reaches 0 loses every
item in its slots.

## Item codes

An item byte's low 7 bits are a code; bit 0x80 marks it destroyed.
- **0x10-0x20: weapons.** The weapon is entry (code - 1) of the weapon table: 0x10 the Small
  Laser (entry 0x0f) up to 0x20 the SRM 6 (entry 0x1f). `1e31:10a2` counts these bytes in slot
  order to number a 'Mech's weapon slots.
- **0x22: a heat sink.** Each intact one takes 1 heat a round; repairs mend them.
- Other codes are not used by the templates.

## The templates

Eight records of 0x7d bytes in segment 27e8:

| address | name | used by |
|---|---|---|
| 27e8:02f0 | LOCUST | training (flag 02 = 0), the mission default, encounters, the Arena |
| 27e8:036d | WASP | training (flag 02 = 1), encounters |
| 27e8:03ea | STINGER | encounters |
| 27e8:0467 | COMMANDO | the encounter table |
| 27e8:04e4 | CHAMELEON | training (flag 02 = 2): the training 'Mech, structure 0 in five locations and model 200 |
| 27e8:0561 | JENNER | the raid, the encounter table |
| 27e8:05de | (spectator) | the Arena's figure 13: a placeholder with armour 0 or 1 and Locust-like slots |
| 27e8:065b | URBANMECH | the encounter table; the two that come after the spectator falls |

27e8:02a0, just before them, is not a 'Mech: it is the enemy 'Mechs' walking script (codes fd / fe,
docs/town_loop.md "Walking frames"). 27e8:0280 is figure 0's, 27e8:02e0 the enemy people's.

## The weapon table, DG:2ed8

33 records of 0x11 bytes, entry W at DG:2ed8 + 0x11 W.

| offset | size | meaning |
|---|---|---|
| 00 | 11 | name, NUL-padded |
| 0b | 1 | damage. A personal weapon: low nibble fixed + high nibble times d6. A 'Mech weapon: per shot (per missile for missiles); the Inferno's ff is not used as damage |
| 0c | 1 | bit 0x80 = a personal weapon, and the low bits are the shots per attack. Otherwise 1 = a single shot, 2-8 = the cluster table's column |
| 0d | 1 | low nibble: heat; high nibble: minimum range (the PPC 3, matching the board game; no code read uses it) |
| 0e | 1 | bits 7-5: S, the short limit; bits 4-0: M, the medium limit. Both x 3 for 'Mech weapons except the Kick |
| 0f | 1 | L, the long limit, never scaled |
| 10 | 1 | skill number: 0 Bow & Blade, 1 Pistol, 2 Rifle, 3 Gunnery, 4 Piloting (character byte 4 + n) |

Entries, by index (the names themselves are imported, `weapons.name.*`):

| W | what they are |
|---|---|
| 00-0e (people) | hand weapons in rising order: three melee weapons and a vibroblade, three bows, a pistol and a rifle, a machine gun, a short-range missile launcher and an inferno launcher, a laser pistol and a laser rifle, a flamer |
| 0f-1f ('Mechs) | 'Mech weapons: small, medium and large lasers, the PPC, four autocannon sizes, a machine gun, a flamer, four long-range and three short-range missile racks |
| 20 | the Kick |

A character's weapon byte (0x0b) holds W; a 'Mech item byte holds W + 1. The people's SR Missile
and Inferno are encoded like 'Mech weapons (bit 0x80 clear, skill Gunnery). The Kick's damage
byte (DG:3103) is overwritten before each kick. Example: the Medium Laser does 5 damage for 3
heat, with S 4, M 7, L 30, so short below 12, medium below 21, long below 30.

The range band names (short, medium, long and out of range): far pointers DG:2ebc to
DG:2ce0, 2ce6, 2cea, 2cef (`weapons.range.0` - `.3`). The name of item code c is at DG:2ec7 + 0x11 c.

## The other combat tables

| table | address | size | layout | used by |
|---|---|---|---|---|
| side | DG:2d0a (centre DG:2d11) | 15 bytes | index facing - direction (-7 to 7), value a row 0-3 of the location table | to-hit location, combat.md 3.9 |
| hit location | DG:2e40 | 4 rows of 11 | DG:2e40 + 11 row + 2d6 (2d6 from 2 to 12, so each row's first entry is at + 2); value an armour offset 0x11-0x1b | combat.md 3.9; the Kick reads row 0 at 3 and 11 (left and right leg) |
| cluster | DG:2e5e | 7 per row | DG:2e5e + 7 c + column, c the 2d6, column 2-8 from weapon +0x0c; value the number of hits. Columns 7 and 8 run into the next row: the bytes are laid out so that this still reads the board game's table | combat.md 3.11 |
| target movement | DG:2d1a | 13 bytes | index the steps the target has taken this round (0-12), value the to-hit modifier (0 for no steps) | combat.md 3.7 |
| heading of a step | DG:2ecc (centre DG:2ed1) | 11 bytes | index 4 dy + dx, value the heading 0-7 (0 north, clockwise) | movement |
| location names | DG:3242, DG:324e | | the name of each armour location | messages |
| critical slots | DG:316e, DG:3176 | per location | first slot and slot count | criticals |
| actuator bits | DG:316a (legs), DG:317a (arms) | by d6 | the bit a critical clears | criticals |
| difficulty armour cut | DG:165a | 7 bytes | index flag 03 (clamped to 0-6), value subtracted from each armour byte, to 0 at least (0 at index 0, 20 at index 6). The bytes after index 6 are text, so the clamp matters | `17dc:0d49` |
| encounter templates | DG:13e2 | 11 far pointers | index 2d6 - 2; a template in 27e8 (Jenner, UrbanMech, Commando, Locust, Wasp, Stinger; the Locust is the most likely, 10 in 36) | `17dc:0d49`'s branch for the Arena (combat.md 1.4): there the index is (r & 3) + 3, entries 3-6 |
| encounter sprite base | DG:140e | 11 bytes | same index; 0 or 0x92 (the two sprite sets) for the matching template | as DG:13e2 |
| random encounter templates | DG:2df8 | 3 far pointers | index r mod 3: Locust, Wasp, Stinger | `15ab:0d3d` |
| opponent offsets | DG:1642 (x), DG:164e (y) | bytes | added to the set-up place for 'Mech i or character j | `17dc:0d49` |
| party formation | DG:3a16 / DG:3a1e (people), DG:3a26 / DG:3a2a ('Mechs) | 8 and 4 bytes | dx, dy in half-cells from the party position for the k-th member placed | the fight's placement |
| blocking tile | DG:0150 | 1 | tiles at or above it block line of sight, placement and the miss mark (0x55; 0x21 on map 14) | `1e31:1bfe`, `15ab:0d3d` |

Index 11 of DG:13e2 would read the start of DG:140e as a pointer; 2d6 - 2 never reaches it.
