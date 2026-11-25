# Random numbers

The original has one random generator, and the rebuild keeps it exactly: the same state, the
same step and the same seeds. There is no modern generator and no seed option. This document
gathers what the other documents say about it and links to them for each use.

## The generator: 287f:0bc0

`287f:0bc0` keeps three bytes at DG:4fc0 (s0, s1, s2), a shift register. One call:

1. s2 shifts left, taking bit 1 of s0 in at the bottom.
2. s1 shifts left, taking s2's old top bit in at the bottom.
3. t = (s0 >> 2) - s0 - (1 - s1's old top bit), in 8 bits.
4. s0 shifts right, taking t's low bit in at the top.
5. The call returns s0 XOR s1, one byte.

Callers take what they need from the byte: `r & 7` for a place, `r & 1` for a coin toss,
`1000:19dd` for a 2d6 (two d6, each drawing again while `r & 7` is above 5), and so on. Nothing
uses the Microsoft C run-time library's `rand()`: its multiplier (0x343fd) appears nowhere in
the program.

Rebuilt in `src/game/random.cpp` (`Random::next`).

## The seeds

The state is never taken from the clock.

- **A new game** starts from the state in the program image, 04 03 02.
- **The demo** (no key before the title tune ends) sets 25 13 90 first (`1000:522c`), so the
  recorded keys in DEMOFILE replay the same game every time (docs/startup.md).
- **A new character record** (`1000:a9de`, while a recruit is made) sets the state from the next
  name number (F:d456, which runs 2-9 and wraps) times 0x187. So a recruit's rolls depend only
  on which name it gets. Not traced further yet.

## The 256 start-up draws

Start-up (`1000:50c8`) draws the first 256 numbers, before anything else draws one, into a table
at P:09fb (docs/startup.md). Nothing writes the table again. The wilderness generator takes all
its noise from it (docs/wilderness.md, "The noise table"), and it starts from the fixed image
state. So the land outside the towns is the same for every player and every game: the same place
always gets the same terrain. The rebuild computes the table the same way
(`Terrain::start_up_noise`).

A new game then draws 8 more in its set-up (`1000:4f41`), 8 while placing the townspeople, and
one more as the main loop starts (`1000:0024`, docs/town_loop.md, "Random numbers").

## When the game draws

- **Every game tick, one draw** (docs/town_loop.md, "The game tick"). Outdoors it may start a
  random encounter (docs/combat.md 1.1). A tick runs on the first idle pass on a map, then every
  11th idle pass (one pass is a retrace), and after every key.
- **The money step**, every 256th tick: three draws, outdoors or when the money is below the
  floor (docs/town_loop.md, "Money").
- **The townspeople**, every third tick: their routes and waits (docs/townspeople.md).
- **The training quiz**, the copy-protection check: which parts it asks (docs/combat.md 7).
- **Fights**: the opponents' set-up, the encounter, to-hit rolls, hit locations, damage and
  the enemies' choices. docs/combat.md 5 lists every draw in a fight in order.
- **Scripts and places**, through their specials (docs/scripts.md).

## Why every game is different

The seed is fixed, but the number of draws before any event depends on the player. Each idle
pass counts toward the next tick, each key is a tick, and each tick draws. So how long the player
waits at a question, or how many steps they take before a fight, moves the sequence along by a
different amount. With the same keys at the same moments, the game plays the same, as the demo
shows.

Outdoors, away from any town, the order of the draws does not depend on timing. Each tick draws
one number (every 256th tick the money step draws three more), and nothing else draws between
ticks, so the random encounters come in a fixed order:
the same soldiers, from the same draws, whenever they come. Waiting only changes when the next one
starts. Where the party stands decides whether it is in reach (docs/combat.md 2.3), and so whether
it ends in a fight or only uses up its draws. A search of 64 walks out of the ruined town after the
raid met the same three groups in reach every time: four soldiers, five, and one.

## Why the rebuild keeps it

- **Faithful outcomes.** The original's rolls, from a fight's hits to a recruit's skills, come
  out as the original's for the same input, not just with the same odds.
- **The same world.** The wilderness noise comes from the start-up draws, so the terrain matches
  the original's everywhere.
- **The reference rig.** The rig (docs/reference.md) plays the player's own copy and chr with the
  same keys and compares memory byte for byte, the generator's state included (`state random`).
  A different generator would make every comparison past the first draw meaningless.

The rebuild counts retraces as the original does (docs/town_loop.md, "Pace"), so the
same timing gives the same draws.
