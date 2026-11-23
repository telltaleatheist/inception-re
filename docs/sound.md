# Sound effects and the title music

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
or the speaker: the hardware is replaced, not emulated (CLAUDE.md, goal 2). The platform plays
the segments, or a mod's file in place of any effect ("Playing it", below).

## Sound on and off

DG:015c is 1 in the program image and nothing at start-up changes it. The game settings
(`1000:3bd0`, docs/town_loop.md: item 2, at `1000:3ce4`, flips DG:015c's low bit) is the only
writer, and it shows which state the sound is in (DG:08c9 / DG:08cc). It is not in the saved game
(the save block is F:c614..d557), so every run starts with sound on. Rebuilt: `SoundState::on`,
flipped by `Town::game_settings`.

With sound off, `1000:19bf` returns before calling the player, so there is no wait either: the
busy-wait is inside the player's loops. The rebuild's sound event then has no segments and takes
no time.

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

## Playing it

The platform plays each effect as the event loop reaches it (src/platform/sound_player.cpp), on
one SDL3 audio stream (16-bit, one channel, 44100 samples a second; SDL converts for the
device). The segments become a square wave at a quarter of full scale that keeps its phase from
one segment to the next (`square_wave`, src/platform/sound_wave.cpp, plain C++). The event loop
then holds the event for the effect's length, which stands in for the player's busy-wait; the
stream plays on its own meanwhile.

**Mods.** `chr --sound-mod DIR` plays `DIR/N.wav` in place of effect N (any WAV SDL reads,
converted to the stream's format). The seam is `SoundProvider` (src/platform/sound_wave.h), like
the pictures' asset provider. The game still waits the original effect's length, so a mod's
sound can't change how the game plays.

**Never under a script.** The player is made only for a run with neither `--script` nor
`--screenshot`, so the rig, the tests and any scripted run open no audio device. `chr
--audio-report` prints whether one was ever opened; ctest's `no_audio_when_scripted` and
`no_audio_when_scripted_bare` check that it was not.

## Comparing with the original

The rig compares the effects with the original's speaker (`check.py --sound`,
`tools/reference/sound.py`; docs/reference.md, "Sound"). A DOSBox-X build of our own logs every
write to ports 40-43 and 61 with the emulated time and the writer's address. The player's writes
are found by their offsets in its routines (`287f:0029` to `287f:0092`); the music driver's are
left out. They are compared with chr's `--sound-log` (the steps, in the units above):

- **Exactly:** every tone's divisor and every burst's number of flips, in order. When the last
  capture is a poll (a computer-fought round asking whether a key is waiting), the original runs
  on and plays more; events after chr's last are reported, not compared.
- **The times**, against the passes chr works out. The emulator runs about one cycle per
  instruction, so the time per instruction is fitted over the run, from the bursts (the delay
  loop is one LOOP per count); a tone-wait pass is six instructions, and a kind-2 tone's fixed
  cost was measured from effect 1 (85 instructions). The emulator's ms are not a real machine's,
  so only the shape is compared.

The emulator's clock is not even, and the same in every run (a fixed cycle setting gives the
same log): a single interval can be up to 0.014 ms off, which is 8 delay counts or about one
tone unit, and a whole burst up to 0.9% short. A wrong step in chr is wrong in every play, so:

| what | judged by | tolerance |
|---|---|---|
| a burst, or a run of equal tones | the value at least two of its intervals share | - |
| one step, over all its plays (three or more) | the median | 1.5 delay counts; half a tone unit |
| each play alone | its own value | that, or 1% of its length if more |
| a burst or tone with one interval, and every interval | itself | 0.02 ms |

Fitted over the bursts' medians, the runs so far come within 0.87 counts. Checked with altered
chr logs: a divisor 1 off, a burst a flip short, a count 2 too high in every play, one burst's
count 2% too high (4 counts at 220), and every tone of one length 1 unit too long are each
reported. A count 1 too high, and one lone tone 1 unit off (10 units: 0.01 ms, inside one
interval's jitter), are not. All seven sequences below match. A note's fixed
cost is not measured yet (no note has been logged), so notes are judged only against each other.

Compared: `sound_people` (effects 12, 1, 4), `sound_drawn` (2, 3, 4), `sound_raid` (1, 2, 4, 8),
and the campaign's `sound_crush` (18), `sound_arena_theft` (7), `sound_weapon2` (5, 6) and
`sound_cache_open` (10, 14, 15, 16, 17): each plays a sequence the rig already has, up to the
case where its last new effect is first played.

## The title music

The title plays a tune of its own while it waits for a key (`1000:476d`, docs/startup.md). It
has its own driver, segment 284b, which runs from the timer interrupt instead of busy-waiting,
so the title's key check goes on while it plays.

**What `1000:476d` does.** It reads WWOODBT.SIF whole into 2c6c:244b, puts four 0 bytes after
it, and has `284b:0048` hook int 8 and set the timer's count to 0x0fff, 1193182 / 4095, about
291.4 ticks a second. Then it starts the tune by the display (DG:4fba):

- **Tandy graphics (1):** `284b:02e4`, with a step every 20 ticks, each reading four bytes: three
  for the Tandy sound chip's voices (its own pitch table, 284b:019e) and one for the speaker.
  Not rebuilt.
- **Otherwise (CGA, EGA):** `284b:0306`, with a step every 4 ticks (13.728 ms), each reading one
  byte for the speaker.

Both take a number: 0 stops the tune, 1 starts it (the routines in the tables at 284b:0328 and
284b:0332, each with the step routine it installs). Each turns the speaker on first
(`284b:00bd`), so a stop turns it on and at once off. The title then waits: it clears F:3938
and loops until `273d:002f` reports a key (or F:3938) or `284b:033c` says the step routine is
back to the idle one (the tune's end). Then it stops the tune and `284b:0091` puts int 8 and the
timer's count back.

**The file.** The bytes are rows of four voices. On the speaker they are played one after
another, a byte a step, so the four voices come as a fast run of single notes, and a row lasts
16 ticks instead of the Tandy's 20: the speaker plays the tune about a quarter faster.
WWOODBT.SIF has 4352 bytes and no 0 byte, so on the speaker it is 4352 steps, 59.7 s.

**A step on the speaker** (`284b:0298`, not a function in Ghidra). Every 4th tick it reads the
next byte:

- 0: it reads the byte after it instead, and a second 0 is the end: the idle step routine
  (`284b:0186`) is put back and the speaker turned off (`284b:0179`).
- 0x80 or more: a rest. It sets divisor 14 (about 85 kHz, far above hearing) and leaves the
  speaker on.
- Any other: a note (`284b:0139`). The octave is the byte / 12, the pitch the byte % 12. The
  divisor is 1193182 over the pitch's frequency in the top octave (a table of 12 words at
  284b:0121, `284b:00cf`), shifted left by 8 less the octave. The shift's count is a byte, so an
  octave above 8 shifts everything out and leaves 0 (65536 to the timer). The tune's octaves
  are 3 to 6.

It writes the divisor to timer channel 2 (`284b:00b2`). The first step comes on the first tick
after the start.

**The clock.** The handler (`284b:0020`) calls the step routine on every tick, and passes only
every 17th tick on to the BIOS's own handler. While the tune plays, the DOS clock gets about
17.1 ticks a second instead of 18.2 and falls behind. Nothing in the game reads it there, and
the rebuild has no such clock.

**In the rebuild** (src/game/music.cpp). `tune_steps` reads the file's bytes as the step routine
does, with the pitch table imported from the player's BTECH.EXE (`tables.music_note_hz`), and
gives each step's divisor or a rest. `music_segments` turns them into tones and silences of
13.728 ms each. `Town::title_tune()` reads WWOODBT.SIF from the game folder, and
`Town::title_tune_playing()` is true while the title waits for its key. The platform plays the
tune on an audio stream of its own while that is true and cuts it off when it ends
(`SoundPlayer::play_music`, `stop_music`). A mod's `title.wav` replaces it (`--sound-mod`). No
timer, interrupt or port is modelled. The Tandy path is not rebuilt: the rebuild's machine has
the speaker.

**Compared.** `sound.py` takes the driver's writes from the port log (the segment writing port
42 at 284b:00b5) and checks them against chr's `--sound-log` (`music-timer`, `music-step`
lines). The timer count must match, and every divisor the original played, in order. Each step
must come the step's length after the last, within 0.1 ms: the log strays up to 0.018 ms, and a
wrong rate would be off by a tick (3.43 ms). Every sound sequence starts with the title, but the
first key ends the tune after a few steps. `sound_title` holds the title instead: the title's
key check is the feeder's poll, so it gives 14 x `idle:30000` (the feeder's count is a signed
word) before the key. The original then plays 4126 of the 4352 steps, and all match. Altered
chr logs (one divisor off, the timer count off, 5 ticks a step, a short tune) are each reported.
The whole tune is not run, since its end starts the demo.

## Open items

- Effects 9, 11 and 13 against the original. They are weapon shots (`22e8:152b..`, by weapon:
  13, 3, 5, 6, 11, 1, 9, 2, 4; combat.md 3), and no sequence the rig has fires those weapons.
- The title music's end: the run stops short of it, since the demo would follow (see "The
  title music"). The end mark is read as the code reads, and the tests check it.
- The title music on Tandy graphics (its three voices), not rebuilt.

## The rebuild

`SoundState` holds `on` (DG:015c) and `sweep`, the last kind-2 entry's five words (F:0000,
F:39f4, F:4000, F:4034, F:0062). `Town::play_sound(n)` records the event (`ShownEvent::sound`)
and, with sound on, walks the lists as above, keeps the last kind-2 entry's words and attaches
the effect's segments on the chosen machine (`ShownEvent::segments`). With sound off the event
has no segments and takes no time, as in the original. `SoundState::minus_one_y()` gives the
F:4034 word for the people's run. chr's `--sound-log` prints each effect's steps for the rig.
