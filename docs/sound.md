# Sound effects

The game plays its effects through one call, `1000:19bf(n)`. With sound on (DG:015c set) it hands
the number to the player in segment 27c5 (`27c5:0002`); with sound off it does nothing. The
player drives the PC speaker itself; that part is hardware and is not rebuilt yet (the audio is
left for later). What the rebuild keeps now is what the player leaves behind in the far data,
because the game reads one of those words back (combat.md 2.10, bug 21).

Rebuilt in `Town::play_sound` (src/game/sound.cpp): the logic records the sound's number as an
event and updates the player's state (`SoundState`, src/game/sound.h). It plays nothing.

## Sound on and off

DG:015c is 1 in the program image and nothing at start-up changes it. The game settings
(`1000:3bd0`, docs/town_loop.md: item 2, at `1000:3ce4`, flips DG:015c's low bit) is the only
writer, and it shows which state the sound is in (DG:08c9 / DG:08cc). It is not in the saved game
(the save block is F:c614..d557), so every run starts with sound on. Rebuilt: `SoundState::on`,
flipped by `Town::game_settings`.

## The lists (DG:5008)

One word list holds all 18 effects, back to back, ending with a 0 word (314 words with it,
`tables.sound_lists`). An entry starts with a code word; its size depends on the code:

| code | entry | words |
|---|---|---|
| 1..0x3e8 | a note: the code and two words (F:3984, F:e48c), played by `27c5:02a3` | 3 |
| 0x3e9 (kind 1) | the code and five words (F:4612, F:006c, F:3776, F:4312, F:398a), `27c5:02eb` | 6 |
| 0x3ea (kind 2) | the code, a count (F:4612) and five words (F:0000, F:39f4, F:4000, F:4034, F:0062), `27c5:0345` | 7 |
| 0x3eb, 0x3ec (kinds 3, 4) | the code, a count (F:4612) and five words (F:398c, F:39a2, F:39f6, F:3ff2, F:009c), `27c5:03a8` / `27c5:040b` | 7 |
| anything else | the code alone | 1 |

An entry whose next two words are both 0 ends an effect. It is still stepped over by its code's
size. A note's code also goes to F:4612 first.

**Finding effect n** (`27c5:0002`). n - 1 effects are skipped: from the first word, each entry
whose next two words are 0 counts one effect done, and each entry is stepped over by its size.
The walk stops at a 0 code word. If it stops there before n - 1 effects were counted, nothing
plays. For n = 19 it stops at the final 0 with the count done, and the end test reads the two
words after the table (DGROUP memory the rebuild does not hold); the rebuild plays nothing. The
game's own calls use 1..18; only a script's E4 byte could ask for more.

**Playing it.** From there, until an entry whose next two words are both 0: the entry's words are
stored where the table above says, and its routine runs.

Kind 2 (`27c5:0345`) runs `27c5:0643` count times with the five words. `27c5:0643` stores its
five arguments back in the same five places (so they keep the entry's values) and runs a sweep
on the speaker. Nothing else calls `27c5:0643`. Nothing in 27c5 changes F:4034 after that: the
segment's only other reference to it is a compare in `27c5:0643`.

Which effects have which entries: kind-2 entries are in effects 1, 2, 3, 10 and 11; notes in 6,
15 and 16; kind 1 in 13 and 18; kinds 3 and 4 in the others.

## What the game reads back

**Figure -1.** The figure position arrays are x at F:4004 and y at F:4036, 24 words each, so
figure -1 is the word before each: x at F:4002 and y at F:4034. F:4034 is the fourth of a kind-2
entry's five words, so figure -1's y is that word of the last kind-2 entry played with sound on
(0, the image's value, before the first). F:4002 is 0 in the image, and nothing writes it:

- In the game segments (1000..284b), every indexed store into F:4004 (14 of them, all
  `[si+0x4004]`) indexes by a loop counter from 0, a constant, a counter plus 0x10, or a table
  byte that must equal 8. The two in `22e8:12c7` index by its argument, and its only caller
  (`22e8:000c`) skips a negative target byte before the call.
- There are no stores through another base register at that displacement, none at 0x4001..0x4003,
  and no string instructions in those segments. Every indexed store whose displacement is in
  0x3e00..0x4003 is into a byte array at F:3ffa (index 0..3) or F:3fe9 (index 1..7).
- Every block read with a far pointer into that region (`1000:2fbf..301e`) stays below F:4002:
  0x20 bytes at F:39b4 and at F:39d4, 8 at F:3768. (A fourth read, 0x10 bytes, goes to F:4602.)
  The saved game is F:c614..d557.
- In 27c5 no instruction refers to 0x4002.
- Not traced: writes through far pointers kept in variables (`les`, `lds`), inside the run-time
  library (287f). No far pointer to that region is built from an immediate in the game code.

F:4000 (figure -2's x) is the kind-2 entry's third word. Nothing reads figure -2: the people's
run takes the fixed path for any target byte above 0x7f other than ff (combat.md 2.10).

The other places the player writes (F:0000, F:0062, F:006c, F:009c, F:3776, F:3984, F:398a,
F:398c, F:39a2, F:39f4, F:39f6, F:3ff2, F:4312, F:4612, F:e48c) are recorded only as far as the
kind-2 words go. None of the others is known to be read by the game.

## The calls

`1000:19bf(n)` is called from:

| call | in | n | rebuilt in |
|---|---|---|---|
| `1000:4a7f` | `1000:48b7`, a story picture: picture 0 | 12 | `Town::show_picture` |
| `17dc:05a7` | `17dc:01c0`, the script op E4 | the script's byte | `Town::run_script` (docs/scripts.md) |
| `19b8:1739` | `19b8:16b2`, the 'Mech that won't start | 14 | `Town::failed_start` |
| `1b5d:0977` | `1b5d:0913`, a puzzle cell turns | 15 | `Town::cache_puzzle_cell` |
| `1b5d:0a33`, `0a58` | `1b5d:0980`, the puzzle solved / not | 16, 17 | `Town::cache_puzzle_check` |
| `1b5d:0c6b` | `1b5d:0ab6`, a cache door opens from a step | 10 | `Town::open_door` |
| `1e31:1a13` | `1e31:16ab`, a person crushed | 18 | `Combat` (the place test) |
| `22e8:152b..` | `22e8:12c7`, a shot: by weapon, the missile, after the line, a hit (combat.md 3) | 13, 3, 5, 6, 11, 1, 9, 2, 4 | `Combat` (its `sound` hook) |
| `22e8:1c7a` | `22e8:12c7`, a 'Mech's wreck mark | 8 | `Combat` |
| `22e8:1ca3` | `22e8:12c7`, the spectator destroyed | 7 | not rebuilt (combat.md 3) |

## The rebuild

`SoundState` holds `on` (DG:015c) and `sweep`, the last kind-2 entry's five words (F:0000,
F:39f4, F:4000, F:4034, F:0062). `Town::play_sound(n)` records the event (`ShownEvent::sound`)
and, with sound on, walks the lists as above and keeps the last kind-2 entry's words.
`SoundState::figure_minus_one()` gives (0, the F:4034 word) for the people's run.
