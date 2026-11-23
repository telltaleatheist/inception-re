# Sound effects

The game plays its effects through one call, `1000:19bf(n)`. With sound on (DG:015c set) it hands
the number to the player in segment 27c5 (`27c5:0002`); with sound off it does nothing. The
player drives the PC speaker itself and busy-waits while it plays, so the game stops for as long
as the effect lasts. It also leaves words behind in the far data, and the game reads one of those
back (combat.md 2.10, bug 21).

Rebuilt in `Town::play_sound` (src/game/sound.cpp). The logic records the effect as an event and
updates the player's state (`SoundState`, src/game/sound.h). With sound on, it also works out
what the effect sounds like in plain terms: square-wave tones in Hz and silences, each with a
length in ms on a chosen machine (`ShownEvent::segments`). The platform holds the event for that
length, which stands in for the busy-wait. Nothing in the rebuild models the timer chip, port 61
or the speaker: the hardware is replaced, not emulated (CLAUDE.md, goal 2). The platform's own
audio output, and a mod's file in place of any effect, are still to come.

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

## What an effect plays

The player's routines, in the rebuild's terms (`effect_steps`, `SoundStep`). Each entry's routine
first sets the timer for a square wave (`287f:001c`, from `27c5:046e`) and ends with the speaker
off (`287f:0051`, from `27c5:04e4`). All arithmetic is 16-bit and signed where the original
compares signed.

- **A note** (`27c5:02a3`) plays `27c5:047b(divisor, length)` code times. If the divisor is not 0
  the speaker sounds at 1193182 / divisor Hz (`287f:0030`); a divisor of 0 leaves it as it was
  (silent, after the entry's start). Then it waits 50 times int16(length x DG:5006) passes of
  its wait loop (none when that is not above 0).
- **Kind 2** (`27c5:0345`, `27c5:0643`): for each of its repeats, j counts up from 0 while
  int16(step x j) is below int16(2 x r); each step plays the divisor c - r + step x j
  (`27c5:06f6`) and waits int16(length x DG:5006) passes, the same loop as a note's. A divisor of
  0 counts as 65536 (the timer's own rule).
- **Kinds 3 and 4** (`27c5:03a8` / `0747` down, `27c5:040b` / `07da` up): v starts at the
  first word and moves by the fifth, while it is above (kind 3) or below (kind 4) the second.
  Each v is a burst (`27c5:04f1`) with a = v, b = v - the third word, and the fourth word's
  number of flips.
- **Kind 1** (`27c5:02eb`, `27c5:059a`): count bursts with the entry's own a, b, flips and seed.
- **A burst** turns the speaker off (`287f:0067`), waits DG:5006 - 1 passes of a short loop, then
  flips the speaker's data bit (`287f:007d`) the given number of times. After each flip,
  `287f:00a9` runs DG:5006 times; each runs a LOOP of (r & a) | b counts, where r is the value
  `287f:007d` leaves in DX: its seed + 0x9248, rotated right by 3. Kinds 3 and 4 always seed it
  with 0x3e8, which gives 0x12c6. The seed is the same at every flip, so nothing carries over
  from one flip to the next, and the flips make a square wave: each half-period is one flip's
  work plus its delays. A count of 0 makes the LOOP run 65536 times.
- Any other code plays nothing; the walk steps over it.

So the rebuild has no noise generator: every effect is square-wave tones and silences. The steps
are kept in the original's units (divisors, passes, counts, flips), so the rig can compare them
exactly, and turned into Hz and ms only for a machine (below).

## Lengths

The wait loops count passes, so how long an effect lasts depends on the machine. DG:5006 makes up
for most of that: at start-up `15ab:0b95` counts passes of a test loop (a tick check and 200
steps of DEC/OR/JZ/JMP) over 4 BIOS timer ticks, and stores the count x 455 / 10000 in F:3ff4
(a far word: its segment is DG:5424). `15ab:0c8f` then sets DG:5006 = max(1, (F:3ff4 - 4) / 6),
both divisions rounding down.

The rebuild works the lengths out for one named machine (`SoundMachine`): its clock and what each
loop costs in cycles. Every effect's three costs (a tone-wait pass, a short-wait pass, a delay
pass) come from that one setting, as do the speed test's pass and so DG:5006. The default is an
IBM PC AT at 8 MHz, with the 80286's documented cycle counts:

| loop | cycles |
|---|---|
| one pass of the speed test (`15ab:0b95`) | 3065 |
| one pass of the tone wait (`27c5:047b`, `06f6`) | 57 |
| one pass of the short wait before a burst | 29 |
| one call of `287f:00a9`, its LOOP aside | 151 |
| one pass of that LOOP | 10 |
| the rest of a burst's step (the flip and its loop) | 184 |

**DG:5006 on the AT.** 4 ticks are 4 x 65536 / 1193182 s = 219.70 ms = 1,757,613 cycles: 573.4
passes, so the count is 574. F:3ff4 = 574 x 455 / 10000 = 26 (26.12 rounded down). DG:5006 =
(26 - 4) / 6 = 3 (22 / 6 = 3.67 rounded down). It is 3 for any count from 484 to 615 (F:3ff4
from 22 to 27): it would drop to 2 at 483 or fewer passes, 16% fewer than worked out here, and
rise to 4 at 616, 7% more. On the rig (DOSBox-X's default core), F:3ff4 is 36 and DG:5006 is 5.

**What the counts leave out.** The documented counts assume no wait states, no memory refresh
and a full prefetch queue. A real AT's memory wait state, the refresh cycles and prefetch stalls
only add cycles. They slow the speed test too, but DG:5006 stays 3 while the test's count stays
above 484, so the waits take at least as many cycles as worked out: **the lengths are a lower
bound** for the 8 MHz AT, as long as a pass of the test takes no more than 18% longer than
documented (574 / 484). How much slower a real one runs has not been measured.

Rebuilt in `sound_calibration` and `sound_segments`: a tone is int16(length x DG:5006) passes
(times 50 for a note) of the tone wait; a burst is DG:5006 - 1 short passes of silence, then
flips x half-period at clock / (2 x half-period) Hz, the half-period being 184 + DG:5006 x
(151 + 10 x count) cycles. Segments of the same pitch next to each other are joined.

## Comparing with the original

The rig compares the effects with the original's speaker (`check.py --sound`,
`tools/reference/sound.py`; docs/reference.md, "Sound"). A DOSBox-X build of our own logs every
write to ports 40-43 and 61 with the emulated time and the writer's address. The player's writes
are found by their offsets in its routines (`287f:0029` to `287f:0092`); the music driver's are
left out. Against chr's `--sound-log` (the steps, in the units above):

- every tone's divisor and every burst's number of flips must match exactly, in order;
- the times are compared through one straight line per run, fitted by least squares: tone length
  against its passes, and the time between flips against DG:5006 x count. Each must lie within
  5% of the line. The emulator's ms are not a real machine's (its core counts about one cycle
  per instruction), so only the shape is compared, not the ms.

On the rig the delay fits to within 0.012 ms (1.3%) over counts from 519 to 1703; the points
further out land where the emulator's timer interrupt falls inside a wait. Compared so far:
effects 12 (a story picture), 1 and 4 (the soldiers' shots), in `sound_people`.

## Open items

- The other 15 effects against the original: they need cases that play them (the weapons, the
  cache puzzle and door, a 'Mech that won't start, a wreck, the spectator, a person crushed).
- The title music. A separate driver in segment 284b plays it from the timer interrupt (PIT
  channel 0, seen in the port log at boot). It is to be rebuilt the same way: its events in game
  terms (notes in Hz and ms), with no interrupt model.
- The platform's audio output (SDL3 audio streams, playing while the logic waits), and a mod's
  file in place of any effect. chr opens no audio device yet (SDL is started for video only); the
  output will open none under the rig or ctest, with a test for that.

## The rebuild

`SoundState` holds `on` (DG:015c) and `sweep`, the last kind-2 entry's five words (F:0000,
F:39f4, F:4000, F:4034, F:0062). `Town::play_sound(n)` records the event (`ShownEvent::sound`)
and, with sound on, walks the lists as above, keeps the last kind-2 entry's words and attaches
the effect's segments on the chosen machine (`ShownEvent::segments`). With sound off the event
has no segments and takes no time, as in the original. `SoundState::minus_one_y()` gives the
F:4034 word for the people's run. chr's `--sound-log` prints each effect's steps for the rig.
