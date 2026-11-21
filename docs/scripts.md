# The place scripts

What happens inside a place: the interpreter for the .BLD scripts (the file is in
[formats/bld.md](formats/bld.md)). `tools/bld.py --data DATA ORIGINAL/CITADEL.BLD` (DATA: the imported data folder) lists a script with these
names and its text IDs.

## Entering a place (BTECH.EXE 17dc:0008)

Answering Yes at an entrance calls `17dc:0008(n)` with the map's place number:

1. Sets flag 0a (F:d316) to 1, and stores n at F:4584 (used on leaving, below).
2. Maps n to a script number through the map's own table (the .MTP's `place_scripts` at file
   offset 0x205, loaded at F:4602; docs/formats/mtp.md). Numbers 0x12 and 0x16 and up are used
   as they are. On map 1, entrance 1 is the Citadel.
3. Asks for the right game disk (1000:28cc). The rebuild drops this: there are no disks.
4. **The training center's quiz.** For script 0 (the entrance already turned into its script, so
   the training center only, not entrance 0 of every map), while flag 14 (d320) is 0 and no demo is
   playing (F:3938, F:458c), `1c67:08a8` runs first; if it returns nonzero, the place isn't
   entered at all. A failed quiz sets flag 14 to ff, and so does TRAINING.BLD as each mission
   starts; it counts down in the town's ticks, so the quiz comes back on the first visit after it
   runs out (docs/combat.md, "The copy-protection check"). The rebuild has no check: it makes the
   quiz's draws and, only with `--show-quiz`, shows the scene.
5. Loads and deciphers the script (`17dc:1d30`), shows the place's picture if it has one (byte
   table DGROUP:141a by script number, through `1000:48b7` with mode 1, below), and runs the
   script from offset 0 until `end`.
6. Runs special 14 (the end-of-visit bookkeeping; see Specials).
7. Two follow-on places:
   - leaving the garage (8) with flag 30 set and flag 0e set, and flag 32 clear, shows a text
     (DGROUP:1384, 13b6) and runs place 12 (VIEWDISK);
   - leaving place 13 (BARRACK2) with flag 0e set runs place 12 too.
   In both cases 27e8:0064 is set to 0c: area 0x34's entry in the area-to-map table (27e8:0030 +
   area, docs/wilderness.md) becomes map 12, the HUT. VIEWDISK sets flag 32 at its end (0dce), the only write
   to that flag: no other script sets it, and the program's only indexed flag writes are the new
   game's clear (1000:4e4f) and the script ops (1000:80c9, 811f). A load with flag 32 set makes
   area 0x34 map 12 again (1000:3429). ENTRANCE (map 13) tests flag 32 first, and FROB's last
   branch tests it too.
8. Puts the screen back: window 4 cleared, window 3, the panel redrawn (`1000:4cac(1)`).
9. Leaving place 14 (ENTRANCE) with flag 36 set clears the 8 party records' first byte and calls
   `1b5d:0004`.

## The machine

- The script, deciphered, with a program counter (pc) from offset 0.
- **Flags.** A byte array at F:d30c, indexed by a signed byte, all 0 at a new game except
  the money floor. The scripts and the rest of the program share them (src/game/flags.h):

  | flag | byte | what |
  |------|------|------|
  | 04 | d310 | outdoors (clear on a town map) |
  | 08 | d314 | the course chosen (special 05) |
  | 09 | d315 | special 05's answer: 1 enrolled |
  | 0a | d316 | set on entering a place |
  | 0d | d319 | which place, for "talk to others" |
  | 0e | d31a | members besides Jason (special 14) |
  | 14–16, 1d, 29, 37–39 | | timers the game tick counts down (docs/town_loop.md) |
  | 17 | d323 | the money step's countdown |
  | 2d, 2e | d339, d33a | the let-out case of "talk to others" |
  | 33–34 | d33f | the money floor, a word: 50 at a new game |
  | 4f | d35b | how often a place's picture shows |
- **The current text window and its cursor** (docs/text.md).
- **C-Bills**, F:d370 (32-bit).
- **The party's map place**, P:a44b (x) and a44d (y).

Execution: read the byte at pc and step past it. An opcode E4–FF runs; any other byte is stepped
over. It loops until `end`. Operands are read in order after the opcode. A byte operand is
sign-extended (cbw) wherever the table says signed. A word operand is little-endian, read as an
unsigned 16-bit value. Jumps go to an absolute script offset.

## Opcodes

| op | name | operands | what it does | state |
|----|------|----------|--------------|-------|
| E4 | sound | b n | plays sound effect n (1000:19bf; only with sound on, DGROUP:015c) | |
| E5 | earn | w amount | C-Bills += amount (sign-extended to 32 bits); redraws the C-Bills (`1e31:1fdf`) | d370 |
| E6 | set_party_place | w x, w y | puts the party at map place (x, y) | a44b, a44d |
| E7 | if_party_x | w x, w to | if the party's x == x, goto to (only x is compared) | reads a44b |
| E8 | if_random | b mask, w to | r = random (287f:0bc0); if r AND mask (sign-extended) ≠ 0, goto to | random |
| E9 | new_member | b skill | a new member joins (`19b8:0d58`, below) | party records |
| EA | picture | b n, b mode | shows place picture n, O<n>.ANM, in the picture box (`1000:48b7`); not while a demo plays | |
| EB | if_d451 | w to | if F:d451 ≠ 0, goto to | reads d451 |
| EC | if_d450 | w to | if F:d450 ≠ 0, goto to | reads d450 |
| ED | if_skill | b skill, b level, w to | if any party member (record byte 0 ≠ ff) has skill byte `skill` ≥ level (signed), goto to | reads c614+ |
| EE | pay | w amount | C-Bills −= amount, or to 0 if it is more than they have; redraws the C-Bills | d370 |
| EF | if_cbills | w amount, w to | if C-Bills ≥ amount (unsigned 32-bit compare), goto to | reads d370 |
| F0 | cursor | b col, b row | the cursor to (col, row) in the current window (signed) | 3748, 374e |
| F1 | add_flag | b f, b n | flag f += n (byte, wraps) | flags |
| F2 | pause | | waits 50 ticks (`273d:0006`), then empties the keyboard buffer (`1000:2a2b`) | |
| F3 | switch_flag | b f, words… | goto the word at table + 2·flag f (signed) | reads flags |
| F4 | set_flag | b f, b v | flag f = v | flags |
| F5 | special | b n | runs special action n (below) | many |
| F6 | if_yes | w to | prints the Yes/No question (`1000:1a13(1)`, Yes first); if Yes, goto to | |
| F7 | if_flag | b f, w to | if flag f ≠ 0, goto to | reads flags |
| F8 | goto | w to | goto to | |
| F9 | menu | b m, words… | menu m (`2656:0b5e`, below); goto the word for the chosen item | menu record +6 |
| FA | frame | b style | draws frame style `style` round the current window (`2656:0004`, docs/text.md) | |
| FB | wait_key | | waits for a key (`273d:0259`) | |
| FC | print | text, NUL | prints the text in the current window, word-wrapped (`2656:03f5`, docs/text.md) | cursor |
| FD | clear | | clears the current window, cursor to its corner (`2656:0388`) | cursor |
| FE | window | b n | makes text window n current (`2656:0281`) | window |
| FF | end | | stops the script; back to 17dc:0008 | |

Every original script starts `window 6, clear, frame 0`: the big text box on the right.

### The menu (BTECH.EXE 2656:0b5e)

The script prints the menu's words first, as ordinary text. The menu record (385b:0092, imported into tables.json: formats/tables.md) says where
the items are: item i is row first_row + i of the current window, `width` cells wide. Then:
- The remembered item (record +6) is the start. It is reset to 0 if it is out of range, or while
  a demo plays or records.
- The bar is drawn by XORing colour `highlight` (14) over the item's 8 pixel rows (`287f:2b87`),
  and drawing it again takes it away.
- Up and down move the bar, wrapping at the ends. The key goes through `2656:0d1d`, which also
  maps the keypad and some letters to the arrows.
- Enter or space chooses.
- Esc returns the remembered item without remembering anything new. Any other choice is
  remembered in record +6.

So the bar comes back where it was the next time that menu is shown, in this visit or a later
one: entering a place does not reset the table (the rig's `citadel_*` cases compare all 41
remembered items). The table is game state, and the talk special changes menu 20's item count.

### The Yes/No question (BTECH.EXE 1000:1a13)

This is the same question as at an entrance (already rebuilt, docs/text.md), with Yes as the
starting answer.

### A new member (BTECH.EXE 19b8:0d58)

`E9 s`: the first free character record k (0-7, byte 0 = ff); with none, nothing but step 8.
1. The random generator's state is set from the next name number: F:d456 x 0x187, its low
   three bytes (`1000:a9de`). So the recruit's rolls depend only on which name it gets.
2. Byte 0 = F:d456, which goes up by one and from 10 back to 2 (a new game starts it at 1,
   which only the alley companion takes: combat.md 1.6).
3. Bytes 0x0b, 0x0d, 0x0e and 0x10 = 0; bytes 1, 2 and 3 = 2d6 each (`1000:19dd`); byte 0x0f
   (hit points) = 10 x byte 1; the sprite set F:d562 + k = 0; byte 0x0c = 8 (on foot).
4. The seven skills: r & 1 each; then Piloting (byte 8) = 0 and skill `s` = 3. With s = 4
   (Piloting) the hit points also lose 2 x byte 1.
5. A seat: the first 'Mech record m (0-3) in use whose second seat (byte 0x7a) is ff gets
   k there, and byte 0x0c = m.
6. "His name is " (DG:1e2f), the name, " and he will be " (DG:1e3c), then "on foot." (DG:1e4d),
   or "riding as a passenger in " (DG:1e56), the pilot's name (the character in the 'Mech's
   byte 0x79) and "'s 'Mech." (DG:1e70). The panel is drawn (`1000:4cac(1)`).
7. Unless keys are being played back (F:3938), it waits for a key by polling, **drawing one
   random number each time no key is waiting** (`1000:a960`). Then the buffer is flushed
   (`1000:2a2b`), which eats that key, and a key is read (`273d:0259`): two keys in all. Then,
   with flag 26 (F:d332) clear, one draw: if odd, the recruit is the double agent (flag 25,
   F:d331 = k; flags 26 and 27 = 1; F:d330 = 0x1f; combat.md 2.9).
   For the player: the generator moves on for as long as the "who joined" text waits for a key,
   so whether this recruit turns out to be the double agent depends on how long the player
   waits before pressing it. Only the first recruit to come up odd is chosen; after that
   (flag 26 set) no later recruit can be.
8. The party position is set from the map's second position table (F:39b4 / F:39d4, MTP
   0x1c5 / 0x1e5, formats/mtp.md) at the entrance last entered (F:4584). On map 2 entrance 3
   (REPAIR) is the one place where it is not the entrance itself: 0a13,8046 for 0a14,8044.

### The picture (BTECH.EXE 1000:48b7)

`picture n, mode` (and step 5 above) shows O<n>.ANM (docs/formats/anm.md) in the picture box, the
upper left 88×88 at (8, 8). One random number is always drawn. The picture shows when
(r AND mask) is 0, the mask from DGROUP:0ac4 {03, 0f, 3f} by flag 4f. It always shows when mode
is 2, or when n ≠ 0x10 and n is 0, 2, 6 or above 7. For n above 0x10 the file must exist, otherwise
window 4 is cleared. Picture 8 plays 7 times. In mode 0 it plays and goes: sound 12 after
picture 0, a wait of 60, window 4 cleared. Otherwise the last frame stays.

## Specials (F5 n)

`24d3:0004` is one big switch, with its jump table at 24d3:1762. "Files" says which scripts use
each one.

| n | code | files | what it does |
|---|------|-------|--------------|
| 01 | 24d3:0027 | TRAINING | the 'Mech walks in and the mission in flag 00 runs (docs/combat.md 1.2, 1.3) |
| 02 | 24d3:0187 | TRAINING | after the course: by the first letter of 'Mech record 0, DGROUP:48b3 for an L (`course.result_locust`), else 48df for a W or 48e4 for any other letter (the 'Mech's name) and then 48ee (`course.result_*`) |
| 03 | 24d3:01c2 | TRAINING | the 'Mech is handed back: record 0 emptied (byte 0 = ff), Jason on foot (character 0 byte 0x0c = 8), the party at (0x0c2e, 0xc076) outside the training center. Unless flag 04 is set, marks 0x7c and 0x7d (fires) lose their code, area and x (F:d457, d497, d4d7), keeping y |
| 04 | 24d3:0231 | CITADEL | prints the three courses' prices: for skill i (0–2), 125 × Jason's skill i + 75, right-aligned to column 39 on row 5 + i |
| 05 | 24d3:02a5 | CITADEL | takes the course in flag 08: prints DGROUP:491a and the level and course names (tables DGROUP:4ea2, 4eb6). At level 4 it prints 4948 and sets flag 09 = 0. With enough C-Bills: flag 09 = 1, the skill + 1, the price paid. Otherwise it prints 4999 and sets flag 09 = 0. |
| 09 | 24d3:09c7 | ARMOR | buys the armour chosen: "The shops", below |
| 11 | 24d3:0a4b | WEAPON | buys the weapon chosen: "The shops", below |
| 12 | 24d3:0aea | ten places | "talk to others": `17dc:17b9`, below |
| 13 | 24d3:0b26 | REPAIR, HOSPITAL | who attends: special 14, then menu 0x17 at the cursor's row, one item per present character (its name and a line end), the bar on the first; flag 0e = the chosen character's record |
| 15 | 24d3:0bfc | REPAIR | the Tech course: with 500 C-Bills, the character in flag 0e pays, gets Tech + 1 and byte 0x10 bit 0; flag 0e = 1 and the C-Bills redrawn (it ends through special 21's tail, 0c9b). Short of 500: flag 0e = 0, nothing else |
| 16 | 24d3:0be1 | REPAIR | flag 0f = byte 0x10 bit 0 of the character in flag 0e (the Tech course taken) |
| 21 | 24d3:0c5c | HOSPITAL | the seminar: as special 15, with Medical + 1 and bit 1 |
| 22 | 24d3:0c46 | HOSPITAL | flag 0f = byte 0x10 bit 1 of the character in flag 0e (the seminar taken) |
| 14 | 24d3:0af2 | ten places, and after every visit | flag 0e = how many of the party records 1–7 are in use |
| 23 | 24d3:0cf8 | healing (`1c31:000a`) | flag 19 = 1 if any party member's hit points differ from 10 x B, else 0 |
| 31 | 24d3:0eda | ARMOR | flag 1f = the armour type (byte 0x0d) of the character in flag 0e |
| 32 | 24d3:0ef3 | ARMOR | tops up that character's armour, point by point: "The shops", below |
| 30 | 19b8:104e | PARTY | the first companion joins and the alley fight (kind -1, combat.md 1.6) |
| 36 | 24d3:13b2 | JAIL | prints the double agent's name: character F:d331's name (the `character.*` table) |
| 37 | 24d3:139a | JAIL | the game is over: DG:014a = 0 and character record 0 emptied (byte 0 = ff); not rebuilt |
| 38 | 24d3:13c6 | JAIL | prints the name the next recruit will get: name number F:d456 (scripts.md, "A new member") |
| 39 | 24d3:13e6 | JAIL | the crew screen with argument 1: everyone on foot, every seat empty first (below); not rebuilt |
| 40 | 24d3:13ec | JAIL | Jason alone on foot for the impounded 'Mech (`19b8:152f`, mode 9, combat.md); then flag 28 = 1 if he fell (DG:014a = 0, the leader down: combat.md); not rebuilt |
| 45 | 24d3:15dd | ENDMECH | ENDMECH.CMP over the whole screen with its own palette (3858:0000, docs/formats/cmp.md) until a key; then STARLEAG.ICN loaded again, the standard palette, the screen set back for the town (287f:1fbe), the panel (`4cac(1)`) and the box cleared (2656:0388). CGA's colours are not rebuilt |
| 46 | 24d3:0e2c | GARAGE | the held 'Mechs back and the crew screen (below) |
| 43 | 24d3:157e | TRAINING | after mission 5 is won (+1672, with flag 2d = 1): townsperson 0 is put at the Lounge's entrance (MAP entrance 7; record +0/+2 and figure 16), inside (+9 = ff), its route 0x70 (last the Lounge, next entrance 0) and its target entrance 0, the Training Center. Talk to others in the Lounge lets it out (below) |
| others | see the table above | the other places | read with those places |

Specials 6–8, 18–20, 24–28, 33–35, 41, 42, 44 and 47 belong to the other places and are read with them. The rebuild logs an
unrebuilt special and goes on.

Special 05's texts are imported from BTECH.EXE as `course.*` (docs/import.md).

### Talk to others (BTECH.EXE 17dc:17b9)

1. The place is the first i with F:4602[i] == flag 0d.
2. Clears the window. The townspeople inside are those waiting (wait ≠ 0) whose route's place
   ((route >> 4) & 7) is this one (docs/townspeople.md).
3. Nobody: `talk.nobody`, wait for a key.
4. Otherwise `talk.you_see`, each one's name and `talk.line_end`, then `talk.done`, and menu 20
   with its item count set to the number listed + 1. Done returns.
5. The let-out case: with flag 2d set, the first one chosen being walker 0, and a 7 in the
   local that step 2's loop leaves behind (1000:9692), flag 2e = 1 and walker 0 stops waiting.
   That local is the place of the last townsperson waiting anywhere, written before it is
   compared with this place (1000:95dd), not this place itself (see the quirk below). Otherwise flag 2e = 0, and the one chosen says why it is here
   (`talk.hello`, `talk.reason.<flag 0d>`) and where it goes next (`talk.going_to`, the place's
   name from the map, `talk.end`), then a key. Outdoors it is a random rumour instead (not
   rebuilt: there are no townspeople outdoors yet).

### The shops (WEAPON and ARMOR)

Both shops print the money (special 10) on row 0x16 and take the choice from a menu whose item 0
goes back. The menus' remembered items (record +6, "The menu" above) are what the buying
specials read: the script does not hand them the choice.

**WEAPON.** Menu 0x0f picks a counter: item 0 leaves, items 1-3 set flag 0c to 0, 6 or 9 and
show menu 0x10, 0x11 or 0x12 (six, three and two weapons). After a weapon's description, Yes to
the question buys it with special 11; flag 0b then picks the shopkeeper's answer. No goes back.

Special 11 (`24d3:0a4b`): the weapon is menu 0x10's remembered item, or menu 0x11's + 6 when flag
0c is 6, or menu 0x12's + 9 when it is 9 (weapons 1-11). Flag 0b = 0. Its price is the 32-bit
entry for weapon - 1 at DG:4f44 (imported, `rules/shops.json`). If the money (F:d370, 32 bits,
unsigned) covers it, it is paid, flag 0b = 1, the C-Bills line drawn again (`1e31:1fdf`), and the
weapon goes to `17dc:15e6`, which offers it round the party like a found one (docs/combat.md):
Jason alone simply takes it, and his old weapon is gone.

**ARMOR.** Menu 0x0c: buy (0), top up (1), talk (2), leave (3). Buying: with flag 0a set, menu
0x0d (five types), otherwise menu 0x0e (four), then a description and the Yes/No question; Yes
runs special 9.

Special 9 (`24d3:09c7`): the type is menu 0x0e's remembered item, or menu 0x0d's when flag 0a is
set. Flag 0b = 0. The price is the 32-bit entry for the type at DG:4f26 (entry 0 is never read).
If the money covers it: paid, flag 0b = 1, the C-Bills line drawn again, and `17dc:13de` offers
the armour, with the type's full points (DG:4ddb, `rules.personal_armour`).

`17dc:13de`, armour offered to the party, is `17dc:15e6` for armour. Its arguments are a type and
points (character bytes 0x0d and 0x0e). With Jason alone (no other character record in use) he
takes both. Otherwise, while the type is not 0: the window cleared, `offer.armour_give`, the
type's name (`armour.<type>`), `offer.armour_to`; for each character in use, his name, column 10,
his armour type's name and a line end; `offer.armour_drop` and the points in decimal; menu 4
with one item more than there are characters. The last item drops it and ends. The one chosen
gets the type and points, and what he wore is offered next. When his old type is the new one,
`offer.armour_already` is printed at column 0, row 20 (`1000:2867`, white), then a key.

Topping up: special 14; with others in the party, `bld.armor.0c30` and special 13 choose who
(flag 0e), else flag 0e stays 0, Jason. Special 31 copies his armour type to flag 1f; type 0 gets
a refusal and back to the start. Then special 32 (`24d3:0ef3`): with type t, points p and the
type's full points f (read as DG:4ddc + t - 1, the same table):
1. p = f: `shop.topup_full`, a key (`273d:0259`), done.
2. `shop.topup_lacks`; then `shop.topup_none` if p is 0, else f - p in decimal and
   `shop.topup_points`; `shop.topup_cost`, (f - p) x the point price (byte t - 1 of DG:4f3e,
   16-bit product) in decimal, `shop.topup_cbills`; the money (special 10, by a call to the
   specials themselves); `shop.topup_ask`; Yes/No with Yes first (`1000:1a13(1)`). No ends.
3. While points are missing and the point price is no more than the money: one point bought (the
   money less the price, p + 1, the C-Bills line drawn again).
4. Then `shop.topup_short` and `shop.topup_some` (some bought) or `shop.topup_nothing`, or
   `shop.topup_done` when none is missing; a key; done.

### The crew screen (BTECH.EXE 1c67:0002)

Who pilots and who rides each 'Mech. 'Mech records 0-3 (byte 0 not ff) have a pilot (byte 0x79)
and a rider (byte 0x7a), each a character record or ff; character byte 0x0c names the 'Mech a
character is in (8: on foot). Its argument says where to start: 0 rebuilds each character's byte
0x0c from the seats (pilot and rider of every 'Mech in use); anything else first puts everyone on
foot and empties every seat.

Then, until the player is done:
1. Window 6, cleared and framed, colour from `1000:28a2`: the heading
   (DG:2622). Menu 23's first row is the cursor row. For each character record in use, in order:
   his name, then " on foot" (DG:2643), or a space, the 'Mech's name (the record's first 19
   bytes, trailing spaces trimmed) and " pilot" (DG:264d) or " rider" (DG:2655). Then "Done"
   (DG:265d); menu 23 gets as many items as there are characters, plus one.
2. From row 10, for each 'Mech in use: its name, then "Pilot:" (DG:2663) and the pilot's name or
   "None" (DG:2670), "Rider:" (DG:2675) and the rider's or "None" (DG:2682), and a line end
   (`24d3:181e`). The names of the people are in colour 2 and the labels in 15 (colour codes
   in the strings); after each 'Mech the colour is set to 15. So the first 'Mech's name is in
   step 1's light green and the others in white; on CGA (F:4fba = 0) each is in colour 1.
3. Menu 23 (`2656:0b5e`).
4. **Done**: every 'Mech in use with a pilot ends the screen. Otherwise, in window 4:
   that not every 'Mech has a pilot (DG:2687). If fewer characters can pilot (Piloting,
   byte 8, not 0) than there are 'Mechs: a pause and a key, DG:26af, a key, DG:26ed (the
   `24d3:17ea` box) and the yes/no question (`1000:1a13`). Yes abandons each 'Mech with no
   pilot (byte 0 = ff, anyone in it put on foot). Either way the screen goes round again, and
   Done ends it once every 'Mech left has a pilot. With enough pilots: DG:271e (assign a
   pilot to each) and a key, and round again.
5. **A character**: in window 4, his name, " is " (DG:2736), "not " (DG:273b, after the colour
   call) when his Piloting is 0, and DG:2740 (the end of "is (not) a qualified pilot"). Window 3: the vehicle
   question (DG:275b), then each 'Mech in use's name and "None" (DG:2775); menu 3 gets one
   item per 'Mech, plus one. He is taken out of every seat first. "None": on foot. A 'Mech: the
   rider's seat, unless he can pilot; then the pilot's seat, but if the 'Mech has a pilot,
   the question whether to make him the pilot (DG:277a) and No gives the rider's seat. Whoever had the
   seat: a rider goes on foot; a pilot becomes the rider, and the rider he displaces goes on
   foot. Then the seat and his byte 0x0c are set.
   **The double agent's tell**: if flag 27 (F:d333) is set and he is the agent (F:d331), the
   message box (`24d3:17c6`), DG:279d, his name and DG:27ae (a remark that he seems untrustworthy in a cockpit), a key, and
   F:374a = 1: he is found out and acts in the next fight (combat.md 2.9).
6. Window 4 is cleared, and round again.

When it ends: the held 'Mech letters F:d452-d455 are all ff, and flag 18 (F:d324) is 0.

Its callers: special 2e (24d3:0e2c), after GARAGE's fee, with 0: 'Mech records 0-3 take back
their held letters (byte 0 = F:d452 + i), the sprite bases F:d55e + i are 0 for an 'L' and 0x92
otherwise, then the screen. And the Space command menu's second item (1000:2c50, `1000:4d57`):
if none of the nine blocks around the party (P:07a4) has bit 0x80, the screen with 0, the panel
(`1000:4cac(1)`) and window 4 cleared; otherwise DG:0ace in the message box (dismounting near a
town is refused) and a key.

## Original quirks

- **WEAPON's unreached block.** A `goto 0220` at 015d always jumps over 0160–021f, which has
  another line from the shopkeeper. The player never sees it. It is kept (not imported, not
  shown); a fix would need to know what the jump was meant to test.
- **ARMOR's four-type list is never shown.** Flag 0a is written only by entering a place (=1)
  and the new game's clear, and read only by ARMOR's script and special 9. Inside the shop it is
  always set, so the script always shows menu 0x0d and special 9 always reads it; menu 0x0e and
  its branch are dead. Kept as it is: nothing to fix that the player sees.
- **The armour offer's "already" test compares types only.** Handing a worn suit to someone
  wearing the same type prints `offer.armour_already` though the points differ, and the swap
  still happens.
- **The let-out test reads the wrong place.** Talk to others (17dc:17b9) tests for place 7
  with the place of the last townsperson waiting anywhere (any index, any place), not with
  this place. On map 1 the Lounge is entrance 7, and while Rick is the only one waiting the
  two agree. With someone waiting elsewhere after him, the test follows that one's place: the
  Lounge's let-out can fail, and on a map whose Lounge has another entrance it can still pass.
  Kept; `fix_talk_let_out` (bug 38, off) compares this place.
- **if_party_x** compares only x. No original script uses E7, so nothing shows the effect.

## After the raid: what the scripts offer

Read from the scripts' control flow (`tools/bld.py`) and the specials' code; not yet run in the
rebuild except where a section above says so.

**Map 11** (the ruined town, area 0xcc) has map 1's entrance table except entrance 0, which runs
BARRACK2 (0x13) instead of TRAINING. But only entrance 0 has a position (0x0c11, 0xc009): the
other fifteen are (0, 0), which no step lands on, so BARRACK2 is the only place the party can
enter there. Its money stays as the raid left it, and town ticks still draw random numbers. What
the same scripts offer where their entrances are open (map 1, and the other towns that use them):

| place | what it offers | specials |
|---|---|---|
| BARRACK2 | one of two texts by the party count (special 14, flag 0e) | 0e |
| CITADEL | the skill courses (special 4's prices, special 5) | 04, 05, 0c |
| COMSTAR | the money and a list of three 32-bit amounts at F:d374 (special 6); menus over them (7, 8) | 06, 07, 08, 0c |
| WEAPON | buying a weapon ("The shops") | 0a, 0b, 0c |
| ARMOR | buying armour and topping it up ("The shops") | 09, 0a, 0d, 0e, 1f, 20, 0c |
| REPAIR | apprenticeship: the Tech course for 500, once per character, who chosen by special 13 (specials 16, 15; Tech + 1, character byte 0x10 bit 0); with a party and flag 18, a recruit (`new_member 5`, flag 20 once, or Tech 3+ and a random bit). Repairs (12), the speed shop (13, 14: the upgrades, 19b8:0925) and ammunition (2c) need a 'Mech: special 11 counts the 'Mech records in use, special 1d (with flag 18) the letters at F:d452..d455 | 0a, 0c-0f, 10-14, 1d, 2c |
| BARRACKS, LOUNGE | talk, and the flags of the Lounge's let-out (2d, 2e, 2f) | 0c |

Special 0a prints the money (F:d370, 32-bit). At the end of the unpoked chain Jason has 170.
None of these places pays the party: money comes from fights (scrap, 2.15) and the town's money
step (docs/town_loop.md), whose 15 C-Bills come only in a town; the ruined town counts as outdoors
(flag 04), where the step changes only the three amounts.

**Where the story goes next.** The recruits are HOSPITAL (`new_member 6`; maps 2-9), REPAIR
(`new_member 5`, which needs flag 18, set only by GARAGE: map 2) and JAIL (`new_member 4`; map 3).
The only code that writes a new 'Mech record outside the fights and the shops is reached from
ARENA's specials 21, 22 and 23 (`17dc:1c9b`, `17dc:1a26`, `17dc:1b41`) and PARTY's special 1e
(`19b8:104e`), both on map 2 (area 0x8a: four areas north and two west of map 11). Map 3
(area 0x7d), with the jail and mode 9, comes after that. The towns by area: 0x28 map 9, 0x4b 8,
0x62 10, 0x66 13, 0x7d 3, 0x8a 2, 0xa6 7, 0xb9 4, 0xc2 5, 0xcc 1 (and 11), 0xd5 6
(`tables.area_maps`).

Reaching map 2 on foot: its south and east sides are walls and its north edge is blocked too
(on map 2 a step onto a tile from 0x55 is blocked, docs/movement.md). The only opening that leads
anywhere is the west side at rows 27-31 (cells 0-5), and it reaches 25 cells and one door: entrance
10, the second entrance of the 'Mech Garage (GARAGE, which sets flag 18). The rows 50-51 gap on the
same side is a pocket closed by the compound's wall. The rest of the town is reached from inside.

What map 2 offers Jason alone, inside (after the garage on foot, flag 18 set, 170 C-bills; every
door is reachable from the rear exit, 24 to 100 steps; `if_flag` jumps when the flag is not 0):

- HOSPITAL's recruit (menu item 3, 0623): flag 18 set goes to a refusal and back to the menu;
  with it clear, special 0e must find a member after Jason. Alone, never.
- REPAIR's recruit (0c1a): special 0e must find a member; alone it offers the Tech course (500).
- ARENA (03a0): entering costs 250 (`if_cbills 0xfa`); the prize (`earn 0x1f4`) and the 'Mech
  record specials 21-23 are all behind it.
- JAIL (map 3) is not open to him either (an earlier reading had the branch backwards). Flag 2a
  (set by its recruit) goes to 0277; flag 27 (a traitor in the party, F:d333) to 0063; otherwise
  special 0e with a member goes to 01fb, the jailkeeper's menu, whose item 07dc leads to
  `new_member 4`, and alone to 0277: Jason is recognised and executed. On the original the
  script ends in "Jason has died" and the play-again question (seen on the chain, past the
  jail's door).
- After the alley (combat.md 1.6) the companion's Commando is held at the garage (F:d452 = 'C'),
  so GARAGE with flag 18 set no longer just lets the party out (0010: special 1d, flag 10 = 1):
  the manager asks 150 C-Bills (0074, `if_cbills 0x96`), and short of it he keeps the 'Mechs
  and the party stays inside (flag 18 still set). The money floor is 50, so a party with 50 or
  more gets no 15 C-Bills a tick. Inside, only a fight's loot ((r & 0xf) + 3 per soldier beaten)
  or scrap adds money (ARENA costs 250 to enter). On the chain the party has 135 here, after
  CLOTHES' 35: so HOSPITAL's recruit (flag 18 clear), the other towns and the jail all wait on
  a won fight or 15 C-Bills found some other way. (Seen in chr; on the original, not yet.)
- The script op E9 (19b8:0d58) adds a member only behind those menus, and special 0e
  (24d3:0af2) counts character records 1-7 whose first byte is not ff, as rebuilt. A new game
  has none.

**The first companion: CLOTHES, then PARTY.** Outside 19b8:0d58 one routine fills a party
record: 19b8:104e, called only by special 1e, which only PARTY.BLD uses (map 2, entrance 8).
It writes record 1 by its fixed address (F:c625, its first byte from the counter at F:d456,
which it then increases), with fixed skill and stat bytes, gives the party a 'Mech slot ('C' at
F:d452, with its 'Mech record), sets the walkers' records and puts the party at 0a69,805e. The
other writes to records 1-7 put back values saved earlier (F:3fe9, mode 9 and ARENA's 23), and
the only whole-table uses are loading and saving a game (1000:32b3, 1000:35d3). The way there
for a party of one:

1. CLOTHES (map 2, entrance 9): the purchase (`pay 0x23`, 35 C-Bills) sets flag 1e.
2. PARTY, first visit (flags 1c and 0e clear): its opening, then with flag 1e set, 025b sets
   flag 1c = 1 and flag 1d = ff.
3. Flag 1d is a timer: each game tick lowers it while it is not 0 (1000:02ba, with 14-16;
   docs/town_loop.md).
4. PARTY again with flag 1c: while flag 1d is not 0 the visit just ends (178e). At 0, a long
   scene, then special 1e and the end. With a member already (flag 0e), every visit ends
   (17ba).
5. Special 1e does not end with the companion: 19b8:104e also fills enemy people, records 8
   up to 10-13 (2 + a random 0-3 of them, on foot, random stats, a 2d6 roll), placed around
   0a72/0a73,805d/805e by the offsets at DG:1e7a / DG:1e82, then calls the fight routine
   `203b:000a` with kind -1. On the original the scene's last screen stays up and the fight
   begins at "Do you want the computer to fight for you?", with no encounter box: Jason and
   the new companion (record 1) against the soldiers. Kind -1 counts as not 0 where the routine
   tests for kind 0 and as below 1 where it compares (docs/combat.md, not written up yet).

So every recruit (HOSPITAL, REPAIR, JAIL) comes after this one.

**The double agent closes the town.** Flag 27 (a traitor in the party, set by `new_member`'s
draw) is the first test in ARENA, ARMOR, CLOTHES, LOUNGE, REPAIR and WEAPON, and GARAGE's with
flag 18 clear (02c6), HOSPITAL's at 040d: each prints a refusal and ends. No script clears it.
Only a fight does (docs/combat.md 2.9), once F:374a is set: by Scan Unit showing him on foot in
a fight, by his tell on the crew screen, or by `1000:378d` from the Space menu. After a fight's
round with F:374a set he acts and is removed, flag 27 = 0, flag 24 = 0x7f. Until then
those places stay shut, and the encounter mask stays at 0x1f (four times the encounters). So
ARENA's test of flag 28 at 06a7, after its own fight, is never reached with a traitor aboard.

**JAIL after its recruit: one chance at the impounded 'Mech.** The recruit's branch (07dc) sets
flag 2a before anything else, and the script's first test (0005) sends every later visit with
flag 2a to 0277, the execution branch: whichever way flag 1e and flag 2a steer its text (0331,
037d), it reaches 0402 and then 01f6, special 37 (Jason's record emptied, the game over). So the
jail can be entered safely only once. The impounded 'Mech's question (106c, `if_yes 050c`,
Yes first) comes at the end of that one visit:

- **Yes** (050c): the jail break, special 40 (`19b8:152f`, mode 9). If Jason fell (flag 28) the
  script ends (10c4); otherwise the escape scene and special 39, the crew screen with
  everyone on foot (0703-07db).
- **No** (106f): a short scene and the end; the party is outside with flag 2a set.

Special 40 is used nowhere else (every script searched for specials 37-40 and flags 28 and
2a: only JAIL, plus ARENA reading flag 28 at 06a7). So No gives up the impounded 'Mech for
good, and coming back to the jail afterwards ends the game. (Read from the script, whose ops chr
runs as the original does; the return visit itself is not captured.) The chain takes Yes.

## Not rebuilt yet

- **EB/EC** read F:d451 and d450 (`Town::kits_`), which only a load or the original's
  24d3:0dc6/0dd3 (two of the specials, 24d3:0004) set; those are not rebuilt, so outside a loaded game the rebuild never jumps. UnBattletech names them "training complete" and a milestone.
- **E4** (sound) is recorded, the event and the player's state (docs/sound.md), but no audio
  plays.
- Specials 6-8, 18-20, 24-28, 33-37, 41, 42, 44 and 47 (in decimal, as in the table above and
  chr's log; the scripts and `tools/bld.py` write them in hex).
- Talk outdoors (`17dc:17b9`): one of eight rumours (DGROUP:4e5e) at random. No rebuilt place
  is outdoors; the rebuild makes the draw and logs it.
- The garage on foot is rebuilt (special 11, then flag 18 and the party put inside at the rear
  exit); its 'Mech paths (specials 1c, 1d, 2e) are not. Both follow-ons into VIEWDISK (step 7 of
  "Entering a place") are rebuilt; the main chain runs the second barracks' one.

## Other descriptions

The opcode set agrees with Elthial/CrescentHawksTools `BldDisassembler.cs` (MIT) in every operand
size. velteyn/UnBattletech `docs/formats/bld-bytecode.md` (GPL-3) agrees on most meanings, with
these differences, all checked against the program:
- **E6/E7.** They call these cursor X/Y. They are the party's map place (a44b/a44d, the same words
  the walk code uses, docs/movement.md).
- **FD/FE.** They call these no-ops. They are clear-window and select-window.
- **E4.** They call it write-char. It plays a sound.
- **The header.** Their 8-byte header is covered in formats/bld.md.
