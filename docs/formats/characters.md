# Characters and figures

The 0x11-byte character record (a member of the party, or an enemy person or 'Mech pilot), the
24 figure slots of a fight and the arrays kept per figure. How they are used is in
[combat.md](../combat.md); the prefixes DG:, F: and P: are defined there. The 'Mech record is in
[mech.md](mech.md).

## The character record

Sixteen records at F:c614 + 0x11 p:
- p 0-7: the party. Character 0 is Jason (F:c614). On foot, character p is figure 4 + p.
- p 8-15: the enemy side: the pilots of enemy 'Mechs 4-7 and the enemy people. On foot,
  character p is figure 8 + p (figures 16-23).

| offset | meaning | evidence | confidence |
|---|---|---|---|
| 00 | name number, ff = empty slot. The name is the far pointer at DG:01ca + 4n. Encounter soldiers get 1, mission opponents 0 | everywhere | confirmed |
| 01 | the first of the panel's three numbers (Jason 8). Sets the hit-point maximum: 10 x this byte. New enemies: 2d6 | the panel, `15ab:0d3d`, `17dc:0d49`, the after-fight healing test | confirmed use; name a guess (body) |
| 02 | the second number (Jason 9). Walking MP on foot: 3/4 of it, 3 to 8. Encounter soldiers: 2d6 | `203b:2474` | confirmed use; name a guess |
| 03 | the third number (Jason 7) | the panel | meaning unknown |
| 04 | skill Bow & Blade (0-4) | the stats screen's order (DG:07a3); the weapon table's skill number 0 | confirmed |
| 05 | Pistol | skill number 1 | confirmed |
| 06 | Rifle | skill number 2 | confirmed |
| 07 | Gunnery: 'Mech weapons and the people's missiles | skill number 3; raised by missions 5-7 | confirmed |
| 08 | Piloting: the Kick; whether a companion stops a traitor; who may salvage | skill number 4; training; `15ab:04f9` | confirmed |
| 09 | Tech: repairs and salvage | `15ab:0002`, `15ab:04f9` | confirmed |
| 0a | Medical | the stats screen | name confirmed, use not read |
| 0b | personal weapon: an index into the weapon table DG:2ed8 (0-0x0e). Encounter soldiers: DG:2cf4[sum of 7 draws of r & 3]; mission people: r mod 14 | the fight, `15ab:18e8`, loot | confirmed |
| 0c | the 'Mech the character is in (0-7), or 8 = on foot (any value from 8 counts as on foot) | placement, ejection, salvage | confirmed |
| 0d | armour type: 0 None, 1 Flak Vest, 2 Flak Suit, 3 Lt Env Suit, 4 Hv Env Suit, 5 Ablative (names: far pointers DG:4e8a). Above 1 halves the walking MP | `15ab:18e8`, `203b:2474` | confirmed |
| 0e | armour points left; the maximum for each type is DG:4ddb[type] (a Flak Vest 25). 0 shows the ruined-armour word (`unit.ruined`) | the person damage step, `15ab:18e8` | confirmed |
| 0f | hit points; 0 = killed | the person damage step, the panel's red part | confirmed |
| 10 | | | unknown |

Skill practice: a party person who fires the same skill 256 times in a row gains a point, up to 4
(combat.md 3.5). The counters are F:d358 + figure and F:d360 + figure.

Saved copies: F:3fe9 + p (the first bytes of characters 1-7 while Jason is alone: mode 9 and the
Arena), F:d390 + 0x1a i (the townspeople's records, docs/townspeople.md).

## Party and enemy slots

| side | 'Mech records | 'Mech figures | characters | figures on foot |
|---|---|---|---|---|
| party | 0-3 (F:c724 + 0x7d m) | 0-3 | 0-7 | 4-11 |
| enemy | 4-7 | 12-15 | 8-15 | 16-23 |

A 'Mech and its crew point at each other: character byte 0x0c names the 'Mech, 'Mech byte 0x79
the pilot and 0x7a a second occupant. A party 'Mech destroyed puts both on foot (byte 0x0c = 8)
at its place. Enemy crews stay inside. Jason's 'Mech is F:c620 (character 0 byte 0x0c).

Outside a fight, figures 16-23 are the townspeople (docs/townspeople.md); the fight saves and
restores their positions.

## Positions

A figure's position is two words in the map's packed form (docs/movement.md): x = area column in
bits 8-11 and a half-cell in bits 0-6, y = area row in bits 12-15 and a half-cell in bits 0-6.
ffff in both means "not on the map". A 'Mech covers three half-cells by three: the columns x-1 to
x+1 and the rows y-2 to y. A person covers one.

## The per-figure arrays

s is the figure slot 0-23. Every address is in F.

| address | size per figure | meaning | set by | confidence |
|---|---|---|---|---|
| F:4004 + 2s | word | x | placement, movement, ejection | confirmed |
| F:4036 + 2s | word | y (ffff with x = not placed) | as x | confirmed |
| F:406a + 2s | word | active (in the fight) | placement (incremented), kills, end tests | confirmed |
| F:393c + 2s | word | destroyed / beaten: 1 when a 'Mech is destroyed or a person killed; read after the fight | `1d43:0a35`, `22e8:12c7` | confirmed |
| F:3920 + s | byte | heading 0-7 (0 north, clockwise; -1 none): the way it last stepped, used by the stepper | movement, the planner, `203b:193b` | confirmed |
| F:396c + s | byte | facing for the hit side; ff = parked in F:45b6 + s while the figure shoots | the walking script, the planner (contact) | confirmed use; who writes it in movement is a guess (the script's fd code) |
| F:45b6 + s | byte | the parked facing | `22e8:12c7` | confirmed |
| F:409a + s | byte | walking frame (0x10 people, 0 'Mechs at set-up; 0x7e a body) | the walking script | confirmed |
| DG:01f6 + 4s | far pointer | the figure's walking script (in 27e8 or DG) | set-up, movement, `22e8:12c7` | confirmed |
| F:d55e + s | byte | sprite base: 0 or 0x92 for the two 'Mech sets, fe for townspeople and enemy people, 0x96 for Jason on foot | set-up, salvage, end of round | confirmed values; set names a guess |
| F:32c6 + 0x30 s | 48 bytes | move orders: 12 entries of 4 bytes (below) | `203b:1c1f`, the planner | confirmed |
| F:40b4 + 0x18 s | 24 bytes | the path: up to 12 steps (below) | `203b:193b` | confirmed |
| F:0078 + s | byte | the number of path steps taken this round | `22e8:000c` | confirmed |
| F:3800 + 12s | 12 bytes | target per weapon slot (below) | Use Weapons, the picker, the planner | confirmed |
| F:3994 + s | byte, s < 12 | 1 = the computer moves this party figure | the planner; cleared by player orders | confirmed |
| F:32ae + s | byte | a terrain value under the figure: to-hit (>> 3 for 'Mechs, >> 1 for people), the course count, water cooling | not traced | use confirmed, meaning a guess (cover) |
| F:3750 + s | byte | the tile under the figure (below 0x10 water, 0x10-0x3f can burn) | not traced | guess |
| F:45ce + s, F:4554 + s | byte | used for the mark after a miss (a blocking tile at the target, a height) | not traced | guess |
| F:42f6 + s | byte | on screen | the drawing code | guess |
| F:324c + s, F:327c + s | | screen x and y | the drawing code | guess |
| F:d358 + s, F:d360 + s | byte, figures 4-11 | shot count and the skill last used (they lie in the flag array, flags 0x50-0x5f) | `22e8:000c` | confirmed |

### Per 'Mech record (m 0-7)

| address | meaning | confidence |
|---|---|---|
| F:006e + m | heat, 0-30 (signed byte); 30 = shut down. For figures 12-15 the code reads F:0066 + s, the same byte | confirmed |
| F:0092 + m | weapon heat built this round, added at the end of the round | confirmed |
| F:d576 + m | Inferno: rounds left burning (+6 heat each) | confirmed |
| F:323e + m | the first letter of a 'Mech destroyed in that record | confirmed |

### A move order (F:32c6 + 0x30 s + 4k)

| byte | meaning |
|---|---|
| 0 | type: 0 walk, 1 run, 2 jump (a person always 0); ff = end of the list (the rest is ff too) |
| 1 | the area: (y & 0xf000 \| x & 0x0f00) >> 8 |
| 2 | x & 0x7f |
| 3 | y & 0x7f |

Entry 0 is the current order. All of a 'Mech's entries share one type. Party orders move up when
a waypoint is reached; enemy orders never do. Type + 1 is the attacker's to-hit term and the
movement heat. The meaning of the types is read from the menu (Walk, Run, Jump) and fits the
board game's numbers. The fight clears all 24 lists to ff.

### A path step (F:40b4 + 0x18 s + 2k)

Two signed bytes (dx, dy), each -1, 0 or 1 half-cell. dx (or dy) = 2 ends the path; the whole
row is filled with 2 first. (0, 0) is a blocked step: in the round it stops the figure.

### A target byte (F:3800 + 12s + w)

The figure number that weapon slot w aims at, or ff. Bit 0x80: fired this round (cleared at the
end of the round), or held (Use Weapons clears it when listing). A person uses slot 0; a 'Mech's
slots 0-10 are its weapons in item order and slot 11 is the Kick.

## Other fight state

| address | meaning |
|---|---|
| F:3770 | movement points of the figure being planned, or left after the simulation |
| F:4592, F:377c | heat loss and leg damage, the two reasons the inhibited-movement line (`orders.inhibited`) names |
| F:3992 | the party fled (kinds 1-3), or the computer retreated; read by the mission driver |
| F:374c | an enemy person wants to run (kind 0) |
| F:374a | the player has seen the traitor |
| F:3772 | the raid |
| F:e48e | the Arena 'Mech fight (figure 13 is the spectator) |
| F:009e | the computer fights for the party |
| F:0090 | no "computer" options (mission 4) |
| F:e484, F:4586, F:3986 | this shot: 'Mech destroyed, critical hit, arm destroyed |
| DG:014a | the fight goes on; 0 when character 0 or 1 is killed |
| DG:2e38, DG:2e3a | message level (0 None, 1 Brief, 2 Verbose), combat graphics (yes/no) |
| DG:015e | message pause in frames / 12; 5 or more waits for a key |
| DG:2b20 | the target picker's current candidate (12 at the start of a fight) |
| P:a44b, P:a44d | the party position, used as the "current figure" by most helpers |
