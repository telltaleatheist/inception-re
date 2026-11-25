# The start-up

From running BTECH to the town, in a new game. Rebuilt in `src/game/startup.cpp`, and compared
with the original by the rig's `start_*` cases (docs/reference.md): all match pixel for pixel.

## Order

1. **Hardware questions, text mode.** The graphics adapter, the disk drives, then a note about
   saved games. These choose the original's drivers and disks. The rebuild has neither, so they
   are not rebuilt.
2. **The Infocom/Westwood screen** (in `1527:0044`, at 1527:0131..0216): INFOCOM.CMP over the whole
   screen. The keyboard is emptied, then the screen stays up for at most 700 retraces,
   one `273d:0006` wait each, or until a key (`273d:002f`, which only looks).
3. **The title** (`1000:46a7`): BTTITLE.CMP with its palette.
4. **`1000:50c8`**, the rest of the start-up:
   - 256 random numbers fill a table at P:09fb, the wilderness noise (docs/random.md);
   - `1000:320b` loads ANIMATE.ICN and BTTLTECH.ICN for later (nothing shows);
   - `1000:476d` plays the title's tune, WWOODBT.SIF, until a key or the tune's end.
   - **A key:** the keyboard is emptied, `1000:4dc7` starts a new game (the party, map 1, the
     panel), and window 6 gets its frame and the notice: text IDs `intro.notice.0`–`3`, then
     `intro.first_time` and the Yes/No question (`1000:1a13`, Yes first).
     - Yes: the town (`1000:0000(0)`, the main loop, docs/town_loop.md).
     - No: `intro.load_old_game` and Yes/No again. Yes loads a saved game (`1000:32b3`, the
       command menu's load, docs/formats/save.md) over the new game, then the town; No goes to
       the town as after Yes.
   - **No key by the tune's end:** the demo. The keyboard is replaced by DEMOFILE's recorded
     keys, the random numbers are set to 25 13 90 (docs/random.md), and the main loop runs with them
     (`1000:0000(1)`). Afterwards the title is shown again and the loop repeats.

## The new game (1000:4dc7)

The start-up calls it after the title's key, and the game-over question's Yes calls it from
the main loop (`1000:0502`), which then goes straight on to its key half (`1000:002d`), with
no notice and no question. In order:

1. F:d452-d455 (the 'Mechs held at the garage) ff; byte 0 of the eight 'Mech records
   (F:c724 + 0x7d k) ff; byte 0 of character records 1-7 (F:c614 + 0x11 i) ff, so only Jason is
   left; Jason's bytes 4-10 0.
2. The script flags F:d30c-d36f 0; the 33 bytes at F:45de (the cache doors' used items) 0.
3. Jason's name number 0 and byte 0x0c 8 (on foot); C-Bills (F:d370) 20; the three investments
   (F:d374-d37f) 0; the next name number (F:d456) 1.
4. Bits 0-4 of area 0xcc's byte in rows 2-7 of the seen map (F:d138 + 0x10 r) set
   (docs/wilderness.md, "The seen map"); the rest of the seen map is not cleared.
5. Jason's bytes 1, 2, 3 = 8, 9, 7, 0x0f (hit points) 0x50, and 0x0b, 0x0d, 0x0e, 0x10 0.
6. The sprite bases (F:d55e): figure 4 0x96, figures 16-23 0xfe; then for each of the 'Mech
   figures 0-3 and 12-15 a draw (`287f:0bc0`), and an odd one makes its base 0x92. These are the
   8 draws after the start-up's 256. An even draw leaves the base as it was, so in a second
   game it is the first game's.
7. F:d33f-d340 (the money step's floor) 50; F:d450, d451 (the care's kits) 0; the marks' next
   slot (F:d557) 0 and their areas and places 0 (their codes stay).
8. `287f:1fbe`, the panel (`1000:4cac(1)`), the window cleared (`2656:0388`).
9. The party at 0c45,c019; `287f:104e(0xcc)`, map 1 into area 4 (`1000:2da8(4, 1)`, which
   places the townspeople), `287f:1da8`, the window built and drawn (`287f:1314`, `18ef`).
10. Figures 0-3: frame (F:409a) and F:40a6 0, direction (F:396c) and F:3978 ff; figures 4-11
    the same with 0x10 for the frames. `1000:051b` draws the figures.
11. Area 0x34's map (27e8:0064) 0 and area 0xcc's (27e8:00fc) 1, undoing VIEWDISK's and the
    raid's; DG:01a8 (the game won) 0; F:398e (the jail's bays drawn) 0; F:374a (the double
    agent found) 0; DG:014a 1.

What it leaves alone carries over into the new game: the random numbers, the menus' last
items, the main loop's counters, the marks' codes, the parked 'Mechs (F:3248), the combat
settings, and the other bytes of the 'Mech records and characters 8-15.

## In the rebuild

`Town::start_up()` begins a Town with the start-up. `chr` does it unless `--map` is given.
The full-screen pictures come through the asset provider (`assets::kInfocomLogo`,
`assets::kTitleScreen`), and the texts through their imported IDs.

`Town::new_game` is the new game from the game-over question, step by step as above, except
the bytes at F:40a6 and F:3978, which nothing rebuilt reads. Step 4's seen bits are set at the
first start too (`Town::preset_seen`). The map it draws goes
to the page, and nothing flips it to the screen until the main loop's next tick: until the next
key the screen's map is black (`287f:1fbe`), with the panel drawn. The rig compares that moment
(`chain_new_game`), a walk after it and an idle wait; the random numbers match, so no draw is
missing between the new game and the loop. The start-up's own new
game is the Town's construction: the same values as members' starting values, and the 8 draws
inside `Random::at_town_start`.

The title's tune plays while the title waits (docs/sound.md, "The title music"). When it ends
(59.7 s on the speaker) with no key, the demo plays ("The demo", below).

The wait (`1000:4874`) is the tune's own length. It ends when a key is waiting or when the tune's
step routine is the idle one again (`284b:033c`), and nothing else counts: no polls and no BIOS
ticks. The tune runs from the timer interrupt, so its length is 1 + 4 x 4352 = 17,409 ticks from its
start, 59.748 s (the first step comes after 1 tick, and the step that reads the end mark puts the
idle routine back). Then `1000:5131` asks for a key once more, which would start a new game, and
with none the demo starts. The rebuild (`title_wait_ms`, src/game/music.cpp) uses the tune's ticks,
compared in milliseconds in interactive play, and DOS's tune's length for a copy that has no
WWOODBT.SIF, Amiga's or a DOS folder without the file (a wait taken from its tune alone would be
none, and the demo would start at once). No run of the original compares the wait's length: `sound_title` stops after 4126 steps, and
the rig's `demo` gives chr its `tune-end` from the script. A unit test checks the 17,409 ticks.

### The demo

When the title's tune runs out with no key, the game plays its demo: the keys in the player's
DEMOFILE, read at run time. The repo holds nothing of the file but the counts below.

- **The start** (`1000:51f9`): the disk prompt (`1000:28cc(2)`, nothing to do here), then
  DEMOFILE read into the key buffer (at most 1023 bytes, at 2892:27b0, `273d:0814`). Its read
  position (F:39f8) starts at 10000, the buffer's offset from F:00a0. The random numbers are set
  to 25 13 90 (`1000:522c`, docs/random.md), F:3988 = 0xffff (the new game clears it again, so it
  changes nothing), and F:3938 = 1: the demo plays. Then a new game (`1000:4dc7`) and the main
  loop (`1000:0000(1)`), whose first act is its usual random draw (`1000:48b7(0x0f, 0)`).
- **The keys** (`273d:0259`, the one key read). While F:3938 is set it takes the next byte from
  the buffer instead of the keyboard:
  - 0x48 is a pause of 30 retraces (`273d:0006(30)`), and the byte after it is read;
  - a key the player pressed meanwhile is read and thrown away (`287f:0b8a`), and DG:0152 is set;
  - then one retrace (`273d:0006(1)`), and the byte is the key, sign-extended (an arrow is minus
    its scan code, as the rebuild's key codes are);
  - 0x50 is the end mark: it sets DG:0152 too, and is still the key (to the town, a step on the
    spot).
- **Polls.** `273d:002f` reports a key whenever F:3938 is set, and takes none; so every pass of
  the main loop is a key half, with the demo's next key, and the flush `1000:2a2b` throws nothing
  away. The main loop ends after the pass in which DG:0152 was set (`1000:050d`). F:3938 is
  cleared by the title's tune (`1000:476d`), and the title and the tune come again (`1000:46a7`,
  `476d`). A key at the title then starts a new game again (`1000:4dc7`) before the questions.
- **What the game does differently under F:3938** (each with its own check):

  | Where | Linear | What |
  |---|---|---|
  | `1000:2a2b` | 0x12a36 | the key flush does nothing |
  | `1000:4578` (in `1000:3fae`) | 0x14578 | the overhead map stays up 601 frames (a counter of 600 from `1000:4449`, tested before it is lowered), one random draw for the party's dot each while it is on the view, then reads the demo's next key |
  | `15ab:22c5` (in `15ab:1afe`) | 0x17d75 | the 'Mech stats screen shows 600 frames (the counter from `15ab:21b7`, lowered before the test), then reads the demo's next key |
  | `17dc:0008` | 0x17e28 | the training quiz is not asked (nor while F:458c is set) |
  | `17dc:04a8` (in `17dc:01c0`) | 0x18268 | script op EA shows no picture (docs/scripts.md) |
  | `19b8:0dd6` (in `19b8:0d58`) | 0x1a956 | a joining member's wait for a key is skipped; the demo's next key is read |
  | `203b:100d` (in `203b:000a`) | 0x213bd | with the computer in control, no poll for a key that would hand the party back |
  | `24d3:00a4` (in `24d3:0004`) | 0x24dd4 | the training center's walk-in shows no picture 0 |
  | `2656:0ba6` (in `2656:0b5e`) | 0x27106 | every menu starts on its first item (and while F:458c is set) |

- **The file**, by counts: 1023 bytes, the end mark at byte 781; before it 64 pauses and 717
  keys of 7 kinds (697 arrows, 18 Enters, 2 others). The party walks the town, is offered two
  shops and says no, goes through the training course, and looks at the overhead map. C-Bills go
  from 35 to 40.

**The recorder (F:458c), not rebuilt.** The same key read records when F:458c is set: each key
the player types is stored at the buffer's position and returned, a typed `h` is stored as the
pause mark 0x48 and not returned (the read goes round again), and the menus and quiz behave as
under playback (`2656:0b5e`, `17dc:0008`); the start-up skips the notice and questions after the
title (`1000:5149`). Nothing sets F:458c: its only write is the 0 at `1000:51f0`, and the
program's image holds 0. No write of the buffer to a file was found among its sites. It was
the developers' tool for making DEMOFILE, unreachable in the shipped game.

**The rebuild.** `DemoKeys` (src/game/town.h) plays the bytes as above, and keeps the demo's
time: each read waits its frames (one, and 30 for each pause before it), the overhead map and
the stats screen wait theirs, and the time comes from the key script's idle items (interactive
play feeds the frames that pass). A key in the script while the demo plays is one the player
pressed. The script item `tune-end` says the tune has run out at the title; interactive play
pushes it when the title has lasted the tune's length. One difference: past the file's data the
original would read whatever the buffer holds; the rebuild ends the demo there. The shipped file
reaches its end mark long before.

**Compared** against six captures of a run of the original left on the title (taken at buffer
positions 47, 198, 374, 563, 713 and 782, the last after the demo ended), with chr given the same
frames: the state (random numbers, C-Bills, party, walkers, flags, 'Mechs, characters, menus)
matches line for line at five; at 563 the capture fell inside a pass, one step before chr's.
The rig's `demo` sequence (docs/reference.md, "Waiting") compares it with a run of the original.
