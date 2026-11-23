# Comparing the rebuild with the original

`tools/reference/` runs the original game headless with a key script, captures its screen at
marked steps, and compares those captures with the rebuild pixel for pixel. Nothing from the
game goes in the repo: the runs copy the player's own game folder into a scratch drive and
delete it afterwards.

## How the original reads keys

There is one keyboard read in the whole program, `287f:0b8a`: BIOS int 16h function 0 (wait for
a key), giving a character or minus the scan code. It sits behind `273d:0259`, which can also
record keys into a buffer or play them back from one (F:3938, F:458c: a built-in demo or
replay mechanism, not traced further). Whether a key is waiting is asked of DOS: `273d:002f`
calls `287f:3bdc`, int 21h function 0Bh. The program never hooks int 9.

Both kinds of waiting occur:

- **Blocking**: `273d:0259` alone, e.g. the yes/no question (`1000:1a13`).
- **Polling**: the main loop (`1000:002d`) asks whether a key is waiting; if not, it runs its
  idle half (`1000:01f4`, below) and asks again. Some "press a key" waits are polling loops that
  never time out (e.g. 1000:a965).
- **Flushing**: after reading a key the program often empties the buffer (`1000:2a2b`: while a key
  is waiting, read it), so type-ahead is thrown away. A key that ends a polling wait can be eaten
  by the flush that follows, and the program then waits again.

## The key feeder (tools/capture/snap.asm)

`SNAP 0 K` loads KEYS.BIN (written by `run.py`, at most `MAX_KEYS` words: 16384, a 32KB buffer that SNAP keeps resident; `run.py` refuses a
longer script, since SNAP would not load and nothing would be captured) and hooks int 16h and int 21h.
(With SNAP resident, DOSBox-X leaves 572K of conventional memory, 585,744 bytes, for the
game. BTECH.EXE's image needs about 254K, and its only run-time allocations go through
`273d:05bc`, for saved text and screen regions, which shows its own message, DG:4fe9, if one
fails. So the bigger buffer leaves the game ample room, and a shortage would show on screen.) It puts the next
key in the BIOS buffer (int 16h function 5) only when the program is waiting for one:

- int 16h function 0/10h is called and the buffer is empty; or
- int 21h function 0Bh is asked with an empty buffer for the 4th time in a row since the last key
  read. A flush asks once and stops, so it never gets a key; a polling wait asks again and gets one.

`idle:N` in a script lets N more polls go by before the next item (the main loop runs one idle
pass per poll); a blocking read skips it, since nothing idles there. In the town that gives
exact pass counts: the main loop starts with 3 passes before the first item, a key is followed
by 2 (its flush uses up one of the 4 polls), and `idle:N` adds N. `check.py` spells these out
for chr (`chr_script()`), so both sides run the same number of game ticks.

`@arm:N` before a key sets the same count, but when that key is read, and it works in a blocking
read too: the Nth poll after the key gets the next item. It is for a wait that starts right
after a blocking question, where `idle:N` would be skipped (the computer-fought rounds:
`mission` below).

Captures (`@snap:NAME`) are written at the same moments, so they show the screen the program is
waiting on. In the EGA mode a capture also holds the first 640K of memory and the game's data
segment (DS when it last asked DOS whether a key was waiting), so the game's state can be read
back: `tools/reference/memory.py CAPTURE ds:4fc0:3 "[538a]:d370:4"` prints bytes, where
`[538a]` is the segment whose number is the word at DG:538a, as the game reaches its far data.
`state.py CAPTURE` prints the state the rig compares, in the form `chr --state` prints it: the
random numbers, C-Bills, the townspeople's two tick counters (DG:57fe, the town's; DG:5802, a
mission walk's), the townspeople, the party's place, the flags, Jason's skills, the 16
character records and 8 'Mech records byte for byte, the menus' remembered items, the terrain
generator's noise table and the nine area grids (docs/wilderness.md). When the script ends SNAP writes SNAPEND.TXT and `run.py` kills DOSBox-X. Nothing
depends on the clock or on how fast the host is: two runs of the same script give byte-identical
captures (checked for the start-up, the town and the Citadel question).

## Running it

    tools/reference/run.py --game-dir DIR --out OUT "3 3 @snap:note enter @snap:infocom ..."

DOSBox-X runs with `-silent`, no sound, no PC speaker and SDL's dummy audio and video drivers: no
window and no sound, ever, so it is safe while the screen is shared (the captures come from SNAP
inside the game, not from a window). Its configuration also keeps it off the host's mouse and
focus: no mouse lock, no middle-button unlock or clipboard button, no menu bar, no wait on an
error, normal priority, no quit dialog, and no DOS mouse driver (BTECH.EXE makes no INT 33h call). A run to the town takes about 7 seconds.
Key names are as for `chr --script`, plus `esc`, digits and `@snap:NAME`.

    tools/reference/compare.py OUT/NAME.bin chr-screenshot.bmp --diff diff.png
    tools/reference/check.py --chr build/chr --game-dir DIR --data-dir DIR --work DIR

`check.py` (ctest `reference_compare`) runs the cases in it: each lists the rectangles that must
be identical and the differences still known, and the game state (random numbers, C-Bills, the
townspeople's records) must be identical too. `--full` (ctest `reference_compare_full`, labelled
`long`: `ctest -LE long` leaves it out) adds the long cases. The sequences (below) run side by
side, at most `--jobs` at once (default 4: each is a DOSBox-X process), each in its own copy of
the game folder, and their reports are printed in order: about 25 seconds for the quick cases
and two minutes with `--full` on the Mac Studio. A third tier, `--campaign`, runs only the long
unpoked run (`chain`), all its cases, which `--full` leaves out: about four and a half minutes
now, growing with the run. Its ctest `reference_compare_campaign` (label `campaign`) exists only
when configured with `-DCHR_REFERENCE_CAMPAIGN=ON`. `--sequence chain` also runs it. It skips
(exit 77) without DOSBox-X, nasm or the game.

**A rig error: the disk prompt.** When the original cannot read one of its files, its loader
shows the disk prompt (`1000:2913`) and waits for a key; each key tries the read again. Once,
in a full campaign (four jobs), three sequences hit it at the same case (the Training Center's
files, `chain_raid_greeting`), and every capture after it was the prompt: 167 cases failed
with the random numbers "differing", though the state up to there matched a clean run's exactly.
Run alone, the same sequences were clean. It is the host, not the game, and not the clock:
nothing the rig compares depends on speed. `check.py` now looks at each capture for the
prompt's key wait: the far return address `1000:29de` on the game's stack (DGROUP is 0x36db
paragraphs above segment 1000; `Memory.disk_prompt`, tools/reference/memory.py). None of
5865 clean captures has it, and every one of the stalled run's did. When it is found, the
report says `rig error` (not FAIL), keeps DOSBox-X's log and the scratch drive's file list as
`original-SEQUENCE/rig-error-N-*`, and runs that sequence once more; a second prompt ends the
sequence with a request to re-run it.

The cause (2026-11-21): macOS's temp cleaner. `dirhelper` runs daily at 03:35
(`/System/Library/LaunchDaemons/com.apple.bsd.dirhelper.plist`) and deletes items in the
per-user temp folder (`/var/folders/.../T`, where `run.py` makes its scratch drives unless
`CHR_SCRATCH` is set) older than 3 days. `run.py` copied the game with `shutil.copytree`, whose
`copy2` keeps the originals' dates, so every copy looked days old and the cleaner removed it
from under the running DOSBox-X: the game's next file read failed. In a campaign that night,
all three prompts came at 03:36-03:43, and each kept file list held only KEYS.BIN, SNAP.COM
and the captures (written that night): no game file was left. Runs not at 03:35 were never hit,
which is why it looked random. `run.py` now copies with fresh dates (`shutil.copy`); the
prompt check stays, for any other cause.

A run of the original that writes no new capture for `STALL_SECONDS` (600) is stuck: `run.py`
SIGKILLs DOSBox-X and the report says `rig error: the original stalled`, with DOSBox-X's log kept as
`original-SEQUENCE/run-dosbox.log`. A run past the whole script's limit (2400 seconds for a
campaign sequence, 3600 for `chain_finale`: check.py's `LONG_SEQUENCES`) is a `rig error` the same
way. The longest sequence, `chain_finale`, takes about 35 minutes alone and is listed first among
the campaign's, so it starts beside the others; the stall limit is what catches a stuck run early.

To stop a run, send `check.py` (or `run.py`) SIGTERM or SIGINT: it SIGKILLs every DOSBox-X it
started, removes their scratch folders and exits. DOSBox-X answers any other signal with a "Quit
DOSBox-X" dialog on screen, so never stop the DOSBox-X processes themselves, or the process tree,
any other way; a dialog must never appear while Owen is streaming.

## The original's captures, cached

The original is deterministic for a given KEYS.BIN (the `random:` pokes seed its generator), so
`check.py` keeps each sequence's captures (`tools/reference/cache.py`) and runs DOSBox-X only for
a script it has no entry for. An entry's key is a hash of the KEYS.BIN words with the capture
names, the files seeded onto the drive (`SEEDED`), and the rig: SNAP.COM as nasm builds it,
DOSBox-X's configuration (`run.RUN_CONF`) and program, every file of the game folder, and
`cache.CAPTURE_FORMAT` (raised when what `run.py` keeps from a run changes). A change to any of
them, or to a sequence's keys or case names, is a new entry; nothing has to be cleared by hand.
Only a run that gave every capture, with no disk prompt, is stored.

The cache holds the original's screens and memory, so it stays on this machine and is never
committed: `reference-cache` beside `--chr` (in `build/`, so it survives a reboot), or
`--cache DIR`, or the folder in `CHR_REFERENCE_CACHE` when `--cache` is not given (one lasting
cache for builds made in fresh folders). Each file is stored once by its SHA-256 (`objects/`),
read-only, and an entry (`runs/KEY.json`) lists its files by name; the chain sequences share
their prefixes' captures.
A cached sequence's `original-SEQUENCE` folder is filled with hard links to them, and is emptied
before the original runs into it again. `--refresh` runs the original anyway and stores the
result; `--no-cache` neither reads nor stores; `--prune-cache` first drops the entries made with
another rig and the files no entry lists. The report says for each sequence whether its captures
came from the cache or the original ran.

Two runs of the same script differ in one place only: the low word of the BIOS tick count
(0040:006c), the host's time of day, in every capture (`town`, 25 captures, two runs). Nothing
the check compares reads it.

With the captures cached, a sequence costs only its chr runs (about a second each at the end of
`chain_finale`), so the cases run side by side on all the processors, and DOSBox-X runs stay
limited to `--jobs`. `town` takes 110 seconds with the original and 3 from the cache; ctest's
`reference_compare` 83 and 17, `reference_compare_full` 232 and 62. Every sequence starts at
once, so a case that two sequences compare under the same name (training's first five, which
quiz compares too) writes its screenshot, diff and saves in `work/SEQUENCE` for all but the
first sequence in `SEQUENCES`.

**Choosing sequences.** `--only NAME...` runs the named sequences with all their cases (a
`KNOWN_OPEN` one too). `--affected [BASE]` runs the sequences that the files changed since the
merge-base with BASE (default `main`, plus the working tree and new files) exercise, by the
coarse table `AFFECTED`: docs and unit tests run nothing, a file the table does not list runs
every sequence of the `--full` and `--campaign` tiers.

The routine: per commit, ctest and `--affected`; then the full campaign from the cache. A
`--refresh` campaign only when the rig, the keys or the game copy change (those are new entries
anyway), or one night a week.

## Saved games

A case with `"saved": N` compares the saved game `GameN+1` byte for byte. The original writes
its file into its copy of the game folder (DOSBox-X's C:), and `run.py` copies any `GameN` it
finds there, in any case, to the capture folder at the end of the run; so a sequence compares the
last save of each slot. chr writes into `--save-dir WORK/CASE-saves`, emptied before each case,
never the data folder. The report says, for each part of the file (`SAVE_PARTS`, the block
map in docs/formats/save.md), how many bytes differ and the first one's offset, never the values:
`ok the saved game Game1: 0 parts differ`, or FAIL with the parts.

**A sequence that starts with a saved game** (`SEEDED` in check.py: `save_startup`, the start-up's
load) takes the `Game1` its first sequence's original run wrote (`chain_save`): `run.run(seed=...)`
puts it on the original's drive before the game starts, and check.py into each chr case's save
folder. The first sequence is added to the run when needed and always runs first; the file is
made fresh by every run and never kept anywhere else.

## The start-up, as captured

1. Text mode: "Which graphics adapter do you have?" (3: 256K EGA/VGA), then "How many disk
   drives?" (3: hard disk), then a note about where saved games go; a key goes on. The rebuild
   leaves these to the platform (docs/startup.md).
2. The Infocom/Westwood screen, then the title; a key each. Rig cases `start_infocom`,
   `start_title`.
3. The panel and window 6 with the notice and "Is this your first time playing BattleTech?"
   (`1000:50c8`). Rig case `start_first_time`. Yes goes to the town; No asks whether to load a
   saved game.
4. The town, map 1, at the start position.

The rig drives `chr` through the same start-up (no `--map`), so every case after it runs on from
the rebuilt start-up.

## Test aids: setting a flag in both programs

Some cases need game state that play would take long to reach, or that the rebuild cannot reach
yet (a training mission past the first two, before those missions are rebuilt). For those a case
can set a flag in both programs at the same point of the script: `flag:NN=VV` (hex) in a case's
keys in `check.py`. `mech:R:BB=VV` (hex) likewise writes byte BB of 'Mech record R (F:c724 +
0x7d R + BB), for damage that no case can cause yet. Both check.py and chr refuse anything
outside the eight records (R 0-7, BB below 0x7d, so F:c724-cb0b). `char:R:BB=VV` writes byte BB
of character record R (F:c614 + 0x11 R + BB), for a party the story has not given yet; both
refuse anything outside the 16 records (R 0-f, BB below 0x11, so F:c614-c723). `cbills:VVVV`
(hex, at most ffff) sets the C-Bills (F:d370, all four bytes), for money that play would take long
to earn. `random:AABBCC` (hex, three bytes) sets the random generator's state (DG:4fc0-4fc2,
docs/random.md), for a side branch that starts where a walk would have left the draws. `heat:M=VV`
(hex) sets 'Mech record M's heat (F:006e + M; M 0-7), for a stats screen with a heat bar, which no
fight the rig plays reaches with heat left. `pos:XXXX:YYYY` (hex) sets the party's place (P:a44b and
P:a44d, the two words alone), for a door far across the wilderness: nothing is loaded for it, so
the case then opens the overhead map and leaves it, whose nine-map load (docs/overhead_map.md)
loads the maps around the new place in both. None of them reaches flag 14 (F:d320) or any code. A sequence that uses them says so in its name (`_poked`).

- **The original:** run.py's `@poke:SEG:OFFSET=BYTES` (here `@poke:F:d30c+NN=VV`). SNAP writes the
  bytes into the running copy's memory in DOSBox-X at that point; nothing is written to the
  game's files. SEG is F, P or DG (docs/original.md, "Data segments"), found through the DGROUP
  word the game itself keeps it in. SNAP refuses any segment below the game's first data segment
  (the game's code and the run-time library) or above DGROUP, writes nothing and leaves
  POKEFAIL.TXT, which run.py reports as a failed run. A poke needs the game's DS; it works in
  the town's main loop, which asks DOS for keys, and at the fight's orders menu (the `kick`
  sequence's record poke).
- **The rebuild:** `set:NN=VV` in a `--script`, applied at that point by the town's main loop
  (a question or the start-up refuses one), or `chr --set-flag NN=VV`, applied as the town starts.
  `mech:R:BB=VV` and `char:R:BB=VV` are applied wherever the next key or pass is taken, the
  fight's reads included.

`chr --window X,Y` (hex, repeatable) prints the cell window built at that place from the terrain
as the script leaves it (docs/wilderness.md). check.py adds it to every state case, at the place
the original last built its own; `window.py CAPTURE...` does the same for any captures with a new
game's terrain.

`chr --walks` prints, from where the script leaves the party, the shortest walk to every tile
that stops a step and names a tile for the cache's dispatch (`Town::walks`): one line each,
`walk TILE X,Y STEPS KEYS`, with the half-cell the dispatch is given and the keys in the
script's own names, the last key being the bump. It uses the step check alone, with the window
built at each place, no question answered and no tile's function run. It plans walks; nothing
in the rig reads it (docs/cache.md, "The way through").

`chr --walk-to X,Y` (hex, repeatable) prints, by the same search, the shortest walk to the
place nearest X,Y (in steps either way, then the shorter walk): `walk-to X,Y AT STEPS KEYS`.
The search stays in the party's area, but the walk may end with one step into the next area,
the step check reading that area's terrain as the game does from the edge. A route across
the wilderness is planned an area at a time from it, with chr playing each stretch for the
encounters on the way.

What a poke may not touch: code, and anything that decides the copy-protection path. Flag 14
(F:d320), which skips the training quiz while nonzero, is refused by run.py and by chr; the
quiz's result is never in memory to poke. A case that uses a poke says so in its name or
comment, with what it stands in for (for example "stands in for passing missions 0-1"). When
the play it stands in for is rebuilt, a long case reaches the same point with keys alone, and
the poked case stays as a quick one.

## Sequences and keys only the original asks for

`check.py` has several sequences, each one run of the original from a new game: `town` (the
cases above) and `training` (training mission 2: the walk to the training center, its greeting,
the briefing, the 'Mech menu, the Locust's walk-in under the first of the fight's three questions,
the other two, and the orders menu with its box on the Locust; it pokes flags 00 and 13, standing in for passing missions 0-1
and resting in the Barracks) and `quiz` (the same walk with `chr --show-quiz`: the message, the
three questions and the greeting after them). A key written `orig:KEY` goes to the original
only: without the scene, the training center's quiz takes a key and three menu answers there
that the rebuild does not ask (docs/combat.md 7). In `quiz`, `orig:` keys move the original's
menu bar onto the right part, where the rebuild's bar starts, before each comparison. A
sequence can give chr options of its own (`SEQUENCES`). `check.py --sequence NAME` runs only
the named sequences.

`mission` is training mission 2 fought by the computer, with no combat messages and no combat
graphics (docs/combat.md 1.2.2). Once the three questions are answered nothing is drawn and no
key is read until the fight ends; the only input is the "is a key waiting?" question the fight
asks once a round (`203b:1015`, which would hand the fight back to the player). `arm:N` before a
key sets the feeder's poll count when that key is read (run.py's `@arm:N`, SNAP's item 0FFFCh),
so the Nth question after it is answered: `arm:1 n` puts the first capture after round 1, and
each `idle:1` after it is one more round. chr asks the same question once a round
(`KeySource::poll`): an idle pass answers no, a key yes, and the end of the script stops the
fight there; `arm:N` gives chr N - 1 passes after the key instead of the usual 2. The quick
cases end after round 1; `--full` adds rounds 2-8 and the end of the fight, then a key: the party
on Jason's 'Mech (17dc:134b) and the mission's walk, with the target's wreck drawn (its mark),
back to the door (a way found with chr's step check), the script's verdict, and the town. `verbose` is the
same fight with combat messages Verbose (graphics still off), so every capture compares the last
shot's line in window 4 too; its quick case is round 1 as well. `brief` (messages Brief) is
long only. `drawn` is the fight with no messages and combat graphics on: the screen at each
round's end shows the last of the shots' graphics; round 1 quick, the rest long. `chr --events`
lists what the logic showed on the way, and `--event N --screenshot F` renders the N-th.
`orders` is the mission fought by the player (Verbose, graphics on): Begin Fight with no orders
each round, each capture at the next round's orders menu (round 1 quick, the rest long), then
Flee. `moves` gives the player's move orders and fights on them (docs/combat.md 2.6).
`waypoints` (long only) gives thirteen walk orders on the unit's own cell, so the thirteenth goes
into figure 1's orders (bug 17), then a round. `weapons` opens Use Weapons, gives the first
weapon the enemy through the target picker and fights four rounds (the picker's view reaches
past the map file's east edge, into the terrain: docs/wilderness.md).
`kick` orders a kick at the enemy through the picker (its view reaches the terrain too) and fights three rounds; the long run then takes the left hip away (a record poke
standing in for a critical hit): Kick cannot, and the round drops the kick still ordered. `scan` opens Scan Unit: Friends (Next comes back to the unit), Done, Enemies (the enemy Locust
and its direction, the view reaching the terrain), Done, Cancel, and a round. `people` is mission 3 (flag 00 = 3): three to six enemy people, the orders menu and 20 rounds
(long; from round 6 the soldiers' lines of sight read the area grids before the cell window), compared with the walker lines showing figures 16-23 during the fight, as the original's
shared memory does. `course` is mission 0 with no poke (no flag set at all, as in a new game): the
Locust walks in and the course starts, three steps, then (long) the way to the southeast corner
and its message, back to the door (found with chr's step check, which the original agrees with
along it), the script's verdict (special 2), and a key: special 3 hands the 'Mech back and the
party is in town on foot, where it walks on. `spot` (long only) goes on from there to mission 1
without a poke: 2900 idle polls let flag 14 run down (255 town ticks, an idle tick every 11
polls), the training center again with the quiz on the original's side, the second mission in the
Wasp, the way to the marked spot (the same random spot in both) and back, and the town after.
The window it compares is the one chr holds, which after the spot's cell is put back still shows
tile 0x50 until the next step, as the original's does. `chain` (campaign tier only) goes on from `spot`,
still without a poke: flag 14 runs down again, the third mission's first try (flag 13 is 0: the
commandant and special 3), the Barracks and back as in `rest` (one step east first, as the
handback leaves the party a cell west of `rest`'s), the quiz again and the briefing, then the third mission in the Wasp with the computer fighting: the shell falls in round 16, the walk back, the verdict (passed) and the town; then mission 3 the same way (the last soldier falls in round 13), passed; then mission 4, where the Locust flees at the first orders menu: the walk back and the verdict (not passed); then mission 4 again, won by orders: the Wasp loses a fair fight with the Locust (both sides agree), so the Chameleon, every weapon ordered on the Locust and one waypoint; the Locust falls in round 6, then the walk back and the verdict (passed); then the next mission,
two 'Mechs, by orders in the Chameleon: two Medium Lasers and the Machine Guns on figure 12 (all
nine overheat it) and one waypoint; 12 falls in round 8, the remaining three weapons go on 13 with a
waypoint south where it can be seen, and 13 falls in round 15; the walk back and the verdict
(passed). This run found two differences, both fixed: special 43's townsperson stays inside while
flag 2d is set, and the fight's figure drawer draws the map's marks (a fire changes frame each
drawing). Then mission 6, whose draw lands on the raid: Begin Fight round after round, the
story lines and map 11, until the Jenners bring the Chameleon down, and the ending text, which
covers the orders screen's XORed box (a third fix: the box went on showing over it). The party is
then in the ruined town, outdoors (flag 04). It walks out north, 244 steps found by a search in
chr, into area 0xbc: the grids move a row and the new area's terrain is drawn. Four random
encounters come up on the way (flag 24 is 0x3f), none in reach, so the soldiers are rolled and
placed and the party walks on. One step west from there meets a fifth in reach: four soldiers
("Attacking force: 4 humans."). Declining doesn't evade them, the computer fights with no
messages and no graphics, and Jason, alone on foot, is killed in round 5; the chain ends at the
game-over box. That branch is its own sequence, `chain_death` (campaign tier too), from the same
captured prefix. `chain_mech_poked` (campaign tier) branches from the same prefix after the
fourth walk out: Jason is poked into a 'Mech (the raid's wrecked Chameleon, record 0, gets a first
letter, armour 0x20 in every location, structure 0x10, working left actuators; character 0 byte
0x0c = 0). Those are our own test values, not the template's: they only make a working 'Mech for
the rebuild to be checked against. With a 'Mech in the party the encounter draws change; he walks
west to the first group in reach, five soldiers, and the computer fights them through the end of
the fight (2.10, 2.15). Two more poked sequences branch from it and compare only their own
tails: `chain_heal_poked` also lowers Jason's hit points (character 0 byte 0x0f), so the same
fight ends in healing; `chain_repair_poked` gives him Tech 4 (byte 9) and walks 27 steps on from
the first fight to a Wasp and one soldier. The soldier runs (2.10), a key hands the fight back to
the player, Begin Fight (Enter) runs each round with the computer still moving the Chameleon,
the Wasp falls, and the repairs run. These values are our own test values too. On foot Jason
loses to every group in reach, even with a poked rifle and the most hit points a byte allows, so
no unpoked fight hurts him and lives. `chain_salvage_poked` adds Rex as character 1 after the first
fight (name number 1, attributes, Piloting 2, on foot, hit points; our own test values too) and
walks to the same Wasp: Rex fights on foot as figure 5, and afterwards he is a pilot without a
'Mech, so the salvage question comes, and Jason salvages the Wasp into record 1.
`chain_town` (campaign tier) is the unpoked chain carried on from the ruined town towards map 2
(docs/scripts.md, "After the raid"). It branches at `chain_raid_key_57`, so `chain_death` and the
poked sequences keep their prefix. Map 11's shops cannot be entered, and of the idle counts tried
there before leaving (0-255) none lets the walk to map 2 avoid every encounter in reach, so Jason
flees those (2.7). With no idle the walk out is the chain's own (shared, not compared again); the
first segment is `chain_death`'s encounter declined, the three questions, Flee from the orders menu,
the escape, its key and the return to the position before the fight. Each later segment is planned
in chr from the state the original reached (`flee: escaped` / `flee: failed` and `encounter: eluded
without a fight` in chr's log tell the outcomes apart), then captured and compared: segment 2 is 16
steps north-west to five soldiers in reach, declined and evaded (2.12, one draw); segment 3 is 64
steps west and north-west with every encounter out of reach; segments 4 and 5 walk up-left to
groups of five and three, declined and evaded; segment 6 walks up-left until map 4 is loaded into
the grids (the townspeople's headings there are left as the last encounter set them); segments
7-10 go on north-west and north, three groups evaded, until map 2 is loaded; segments 11-14 reach
map 2's only door on foot (docs/scripts.md, "After the raid"), with a flee and two evades, and end
at its question; then the garage on foot (special 11, flag 18), which puts the party inside, and
out again by its front door (special 1d, flag 18 cleared) towards map 3's jail. Before that,
`chain_town_wait` idles 1300 passes inside map 2. Outdoors only steps draw, one each for the
encounter check, on a single-cycle generator, so idling or a different step pattern only moves
where the walk starts on the sequence: the walk north met five soldiers at a fixed draw that
Jason could neither evade nor flee (Flee failed in rounds 1 and 2, then he was shot). Swept 0-2500
idle passes (0-63 by 1, 100-1600 by 100, 1210-1290 by 10, 1700-2500 sparsely): up to 1270 the same
fight comes, a step earlier per 11 passes; from 1280 its draw is used up in town and the walk
meets only groups that can be evaded. 1300 leaves a margin of 20. Segments 15-17 then go north
into the row of map 3's area and east along it. In segment 18 most step patterns meet three
soldiers that can be neither evaded nor fled; two steps east then one up-right is in among map 3's
buildings by then and comes to the Armor Shop's door instead. No there, then 23 steps inside the
town to the jail's door (entrance 6), and in.

`chain_party` branches from `chain_town_garage_out_2`, inside map 2, towards the first companion
(docs/scripts.md, "The first companion"): three groups evaded on the way to the Clothes Shop,
the purchase (flag 1e), PARTY's first visit (flag 1c, the timer flag 1d = ff), a wait, then off
the door and back on for the second visit. The timer drops one a game tick, one per 11 idle
passes, and is 0 after 2792 passes in chr whatever the random numbers; the case waits 2800, a
margin of 8. The ticks make encounter checks too (map 2 counts as outdoors): every group they
meet up to 3000 passes is out of reach, which needs no key. Then special 1e (combat.md 1.6):
the companion joins and five soldiers attack in the alley. The computer fighting gets Jason
killed in chr; Flee from the first orders menu escapes on this draw, both unhurt, and the party
is walked back to the alley's entry position with the companion.
From there the walk west to the Repair Center (map 2 entrance 3, 96 keys, no group in reach on
the way; the door is entered from the row below the entrance), its first menu item and the
recruit (new_member 5, scripts.md "A new member"). The recruit's key wait starts right after the
script's blocking read, so the feeder's key comes after 3 empty polls (`arm:4`), and each one
draws a number; on this draw he is not the double agent. Then the menu again: the apprentice
course (specials 0f and 10) is not rebuilt, so the case leaves (the last item), and the party
stands at the map's second position for entrance 3.
Then the walk to the garage (entrance 0) and its fee: with the Commando held there, leaving
costs 150 and the party has 135, so the manager keeps the 'Mechs (docs/scripts.md). The money
comes from a won fight at the garage door. Flag 24 is 7f by then, so an encounter can start one
tick in 128 (about one per 1400 idle passes), and most are out of reach. The planner waits for
each group in reach, at most 33,000 passes for each and 12 groups in all, and lets the computer
fight it (Yes, Yes, no messages, no graphics; a pass for each round's poll). It keeps the first
fight that ends with all three alive and at least 150 C-Bills, and evades the others. Here it
evades five groups (two and four soldiers; each fight would have cost Jason or Edward) and wins
against the sixth, two soldiers, after 34,352 passes of waiting in all: ten rounds, 32 C-Bills
from their pockets (167), and two weapons offered (17dc:15e6). Enter alone would never end the
offer: Jason takes the second weapon and his first comes back to him, and round again; the case
gives the second to Edward instead. Then the healing and the walk back.
Then back onto the garage's door ("down up"), Yes, and Yes to the fee: 150 paid (17 left),
flag 18 clear, the party put outside at 0a00,8039, and special 2e gives the Commando back and
opens the crew screen (docs/scripts.md). Rex already pilots it from the alley, so Done (the last
item) ends the screen at once.
Then out to map 3's Hospital with the Commando. The on-foot route's first step north is
blocked: moving up, the 'Mech's figure checks the cells two half-cells ahead and beside it
(docs/movement.md), and the garage's edge is there. The planner's search, with chr judging each
step, goes by the positions the on-foot route passed (097e,7058, 0a3e,7018, 0b2c,7018,
0c6c,7018, 0d0c,700e), one at a time, best-first on the distance; bound 8000 runs, used 4564.
It evades four groups in reach and ends at the Hospital's question after 516 keys. Inside:
Search medical records, the nurse and the doctor, and new_member 6 (its wait after the scene's
blocking read, so arm:4 as at REPAIR): Russ, riding in Rex's Commando; not the double agent on
this draw (flag 26 still clear). Healing costs 25 and the party has 17, so it leaves.
Then the jail (map 3 entrance 6), 20 keys from the Hospital (the planner's search, 176 runs).
This walk found the step check's slots: with Rex in the Commando, the original checks Edward at
the second slot's offset, not his own (docs/movement.md); chr asked at the jail a step early
until that was fixed. Inside: ask the jailkeeper about Rex's friend (menu 0x1f, item 07dc), the
scenes, and new_member 4 (arm:4 after the scene's read): Rick, on foot. On this draw he is the
double agent: flags 24 = 0x1f, 25 = 4, 26 = 1, 27 = 1 on the original. The question about the
impounded 'Mech gets Yes, the jail break (mode 9, docs/combat.md 1.7): Jason alone walks to the
bays on row 702d, 23 steps, well before the first fight (the 81st tick), so no fight is involved;
the first two bays won't start, the third does. The escape, then the crew screen with everyone on
foot: Jason pilots the STINGER, Rex the COMMANDO. No is kept as its own short sequence,
chain_jail_no (the shared prefix not compared): the 'Mech given up, the party outside.

Open: a walk from map 3's jail door to its Hospital (entrance 1, 0d11,700b) stopped short of the
door on both programs, so that Hospital has not been seen yet.
`chain_orders_poked` is that fight up to the hand-back, then Next Unit to Rex and his orders: the
person menu, Use Weapon (the override question, the target picker from the Wasp, Cancel), Scan
Unit (Friends: the Chameleon and Rex's details; Enemies: "humans only" and the enemy person),
Clear Moves, Computer, a two-cell Move, Begin Fight and two rounds. `chain_weapons_poked`
branches from the same hand-back, with Jason's Chameleon the current unit: the player's Use
Weapons (`1d43:0004`), heat poked to 30 (shut down), sensor hits poked to 1 and 2, a destroyed
weapon chosen, and an ammunition byte poked to 0 and that weapon chosen. `fourth` (long only) is mission 4 (flag 00
poked to 4): no computer question (F:0090), the Locust flees at the first orders menu, the walk
back, the verdict and the town. `raid` (long only) is mission 7 (flag 00 poked to 7), the
Kurita raid: the computer question (only mission 4 skips it), then Begin Fight round after round
through the five story lines and the load of map 11, until the Jenners bring the Locust down,
then the results and the script. `rest` (long only) is the third mission's first try with
flag 13 not poked (flags 00 = 2 and 01 = 1 stand in for missions 0-1): the commandant sends
Jason away and takes the 'Mech back (special 3), he walks to the Barracks, sleeps (flag 13 + 1,
the story, flag 14 = 0) and walks back the same way, steps turned round, for the quiz, the
greeting and the briefing. `handover` (long
only) lets the computer fight six rounds, hands the fight back with a key at
the round's poll, and flees and fails. The training
cases at its start are captured but not compared (`"compare": False`): the `training` sequence
compares them. These cases compare the fight's tables too: a case with
`"fight": True` runs `chr --fight-state` and `state.py --fight` (the 24 figures' places,
active and destroyed counts, headings, facings and frames, their orders, targets and paths, what
the figure drawer wrote for each (on screen, terrain, tile, the 'Mechs' tile above and half-row,
screen place), the parked facings, where each is in its script (DG:01f6), the heat, the burning
counts, DG:014a and the map marks).

## Open items

- **The new-game question after the game over** (`chain_death`, docs/town_loop.md step 7): the
  original's capture after the key that closes the game-over box still shows the box, even with
  `idle:2` after the key, so the question is not compared. To come back to when a new game from
  there ("play again", Yes) is rebuilt.
- **A key ignored at "Some of the enemy humans have fled!"** (`chain_repair_poked`, docs/combat.md
  2.10): with the computer fighting, `arm:1 n`, then `idle:1` (round 1 ends at the box), the
  original never takes the next key (Enter): its captures after it still show the box, with the
  same random state, while chr reads the key and plays on. The box waits through `273d:0259`, the
  same blocking read that other boxes take keys at. The sequence goes round it by sending the key
  before round 1 ends, which hands the fight back to the player, then Begin Fight each round.
  The same happens at the double agent's box after round 1 (`chain_agent`, docs/combat.md 2.9),
  and there the cause is the feeder: the box's `273d:086a` waits display frames (no poll), then
  flushes (`1000:2a2b`, one int 21h 0Bh). The computer-fought round asks for no key in between,
  so `arm:1` before `n` hands the next key to that flush, which reads and drops it; the box's
  own read (`273d:0259`) then takes the key after. Without `arm:1`, or with `arm:2`, the
  original takes the first key at the box (flags and the agent's emptied record match chr).
  After that key the next one comes where the feeder gives it: the 4th poll in a row, or the
  one `arm:N` names, so the original plays on through rounds before its next capture, while
  chr, with no item left, stops at its first poll between rounds (`ScriptKeys::poll`). That is
  where the captures are taken, not a difference in the round loop: `chain_agent` gives the
  box's key `arm:2` and matches the original through four more keys. The "all fled" box
  (DG:38e0, `chain_return_fight`) is the same: with `arm:1 n` the original's next capture
  still shows the box (the key went to the box's flush), with the same game state; `arm:2 n`
  gives the box's read the key, as in chr.
- **Where the original blanks the panel's column** (`Town::draw_panel`): chr blacks out columns
  0-12 before drawing the party box, which `1000:4cac` itself never does. The heal's `4cac(0)`
  leaves the column as it is (the direction lines stay, `chain_return_heal_done`), so it skips
  the blackout. The other callers keep it, and their captures match; which of the original's
  code clears the column on those paths is not yet found.

- **A fleeing enemy person whose target byte is ff** (docs/combat.md 2.10, bug 21): rebuilt
  against figure -1 (docs/sound.md). `chain_entrance` fights the forced 'Mech encounter on the
  walk from the HUT to ENTRANCE (2 'Mechs, 3 people, at 0471,4059) with the computer and matches
  it to the win, but no party figure dies, so no enemy person's first target is ff there.
  `chain_flee` (in the campaign) is the same fight with party 'Mech 1's armour and structure
  bytes poked to 1 (our own test values): 'Mech 1 falls in the fight's first key, the people aimed
  at it get ff, all of them flee, and both programs show the fled message (DG:38bc) at the end
  of that key, with the same state. Then three things split them, none of them in the fight:
  - **The feeder's passes after the fled message.** Its key is read by `273d:0259` with no flush
    after it (`203b:0d7c`), so the original lets three of the round's polls (`203b:1015`) go by
    before the fourth gets the next key; `chr_script()` gives two passes after a key, which
    assumes a flush. chr stopped a round early and stayed a key behind from then on (the same
    random states and damage, one key later). Key 1 is now `arm:4 enter`: three passes for chr,
    and the feeder's own count for the original.
  - **Bug 23 in the repairs after the win** (docs/combat.md 2.15). The original's count for item
    0x20 starts at the low byte of DGROUP's segment (0xe9 under the rig), so it mends the SRM 6
    that 'Mech 0 lost in the fight. chr with `fix_weapon_count` on did not: 'Mech 0's slot byte
    0x53 differed from key 6 on, and nothing else. The bug is now kept by default (the switch
    off), as the others are.
  - **Enter on the map after the fight** (keys 11 and 12). The fight leaves grids queued for
    loading (P:09f3); the original's main loop sends every key but Space to `1000:218f`, so
    Enter is a step on the spot that works off the queue (docs/town_loop.md). chr took a key
    with no direction as nothing; it now steps on the spot too. `chain_entrance` never showed
    this: its last compared key is the one back to the map.
  With the bug kept, chr matches all 13 fight keys: screens, state and window. A poke that makes
  the fall land in a later key does not exist here: 'Mech 1 is fired on only in the first key (with every armour
  and structure byte at 2 or more it never falls), so the keys' states were compared directly.
- **A picture's waits** (`Town::show_picture`, docs/scripts.md, "The picture"): the frames'
  delays, the 0x32 after the first frame and mode 0's 0x3c are retrace counts (`273d:0006`)
  that read no key, so the rig, which captures only where a key is read, cannot see them. They
  are checked against the disassembly and in `chr --events` (one event a frame, held for its
  delay), not by a sequence.

## Findings

Every case in `check.py` is identical over the whole screen and in the game state:

- quick: the town start, 9 idle passes later, 120 more (a townsperson walking in view), the
  Citadel question;
- full: 250 game ticks to the second money step, a walk west and south through MAP1 (146 steps,
  90 into walls), 400 idle passes, a walk back east and round, then waiting by the Barracks door
  while a townsperson goes in and, 1,200 passes later, comes out;
- training: the walk to the training center (flags poked, above), its greeting, the briefing,
  the 'Mech menu, the walk-in with the fight's three questions, and the orders menu;
- quiz: the same walk with `--show-quiz`, the quiz's message and its three questions over the
  whole screen, and the greeting after it.

docs/town_loop.md has what the main loop does and when, docs/townspeople.md the townspeople.

Found by the comparison along the way:

- The training walk found two rules of the townspeople's step (docs/townspeople.md): the party's
  places are recorded right after its step, before the townspeople move (`1000:0081`), and a
  person's tile is read from the 24x24-cell window around the party, so past its side edge the
  cell of the next or the previous row decides.

- The move log ("North" in the upper box): the main loop prints the direction (DG:01aa) in
  window 4 after each walking key unless a message came up. Once the box is full it scrolls a
  line at a time (each entry is a line break, then the word).
- The main loop's pace is the display's vertical retrace, not the timer (docs/town_loop.md).
- The party's figure is sprite slot 0xa6, not a 'Mech: its frame offset starts at 0x10.
- The random numbers at the town start: 256 + 8 draws before map 1 is loaded, 8 for the
  townspeople, 1 more before the first pass (docs/townspeople.md).
