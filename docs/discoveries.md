# How The Crescent Hawk's Inception really works

Findings from taking apart `BTECH.EXE`, the DOS version 1.03 of 1988, to rebuild it. They are
written for players. Each one comes from the original program: the address (in the unpacked
program, `segment:offset`) and the document with the full technical write-up are given, so anyone
can check. "Likely" marks a guess or a sum worked out from the code rather than seen in play.
The rebuild keeps every original bug below by default, with a switch to fix it.

Mild spoilers: the double agent, the jail and the Star League cache.

## Luck

From the random number generator at 287f:0bc0 (details: [random.md](random.md)).

- **The dice are never shaken.** The generator is three bytes that shift and mix, and it never
  looks at the clock. Every new game starts from the same three bytes. The game doesn't use the
  C library's usual `rand()` at all: its tell-tale multiplier appears nowhere in the program.
- **The first 256 numbers become the world.** At start-up (1000:50c8), before anything else
  rolls, the game draws 256 numbers into a table, and all the land outside the towns is grown
  from that table. So every player, in every game, walks the same hills, forests and water.
- **Your hesitation is the dice roll.** The game rolls once per tick, and every key you press is
  a tick. Left alone it ticks every 11th screen refresh. So how long you pause at a question
  decides what comes next. Press the same keys at the same moments and the game plays out exactly
  the same; the demo relies on it.
- **Outdoors, the ambushes come in a fixed order.** Away from the towns nothing else rolls between
  ticks, so the next band of soldiers is always the same band. Waiting only changes when it shows
  up. In 64 test walks out of the ruined town after the raid, the same three groups came every
  time: four soldiers, then five, then one.
- **A recruit's stats are fixed by its name.** Before a new member rolls its stats, the generator
  is reset from the next name in a fixed rotation (1000:a9de). The same name always gets the same
  rolls. Hit points are ten times the first stat.
- **Whether a recruit is the traitor depends on how long you read.** While the "new member" screen
  waits for a key, the game rolls once every time it finds no key waiting (19b8:0d58). After the
  key, one more roll: odd, and this recruit is the double agent. Only the first recruit to come up
  odd can be the traitor (details: [scripts.md](scripts.md)).
- **Turning combat graphics off changes the fight's dice.** With graphics on, each shot can draw up
  to three extra numbers for its pictures, which moves every later roll (details:
  [combat.md](combat.md)).

## Time

From the main loop at 1000:0000 and the sound code (details: [town_loop.md](town_loop.md),
[sound.md](sound.md)).

- **The game's clock is your monitor.** Each pass of the main loop waits for the screen's vertical
  refresh: about 70 a second on a VGA card, 60 on EGA. So on an EGA machine the town's idle life
  runs about 14% slower than on VGA.
- **The timer chip is sped up, but only for the music.** At about 291 ticks a second it drives the
  title tune; the game loop never reads it.
- **The title music makes the PC's clock run slow.** While it plays, only every 17th timer tick
  reaches DOS, so the clock gets about 17.1 ticks a second instead of 18.2 and falls behind.
- **A rest in the music isn't silence.** It is a note at about 85 kHz, far above hearing, with the
  speaker left on (284b:0298).
- **The PC speaker plays the tune faster than the Tandy does.** The tune is four voices; the
  speaker plays them one after another as a fast run of single notes, a quarter faster than the
  Tandy's. It lasts 59.7 seconds.
- **The game stops while it beeps.** The speaker routine waits for each sound to finish, so play
  freezes for its length. With sound off there's no wait at all, so fights run faster.
- **It times your PC when it starts.** A speed test counts loops over four timer ticks (15ab:0b95)
  and stretches every sound to suit. On an 8 MHz IBM AT the factor works out to 3; that is
  worked out from the processor's cycle counts, not measured on a real AT.

## Life in town

Details: [townspeople.md](townspeople.md), [town_loop.md](town_loop.md),
[scripts.md](scripts.md).

- **Townspeople only move when you're near.** Far from the party they stand still. You can walk
  straight through them: your step check never looks at them, they just step around you.
- **One in 32 townspeople disappears for good.** Someone reaching a door goes in for a random 0 to
  31 rounds. Roll a 0 and the countdown never runs, so they never come out (1000:24c2).
- **The town keeps you from going broke, up to a point.** Every 256th tick in town, if your money
  and investments together are under a floor of 50 C-Bills, you get 15. A new game starts with 20:
  that's 35 after the first tick and 50 after the 257th, and no more come that way (the training
  center's script can raise the floor).
- **The three investments are a gamble, and the third is a wild one.** They change outdoors (or in
  town while you're under the floor), with rates 96, 90 and 48. Likely, from the code: the first grows
  about 4% almost every time; the second grows 11% seven times in eight and otherwise drops 18%;
  the third is a coin toss between doubling (x2.08) and losing more than half (x0.44). Over a win
  and a loss it ends at about 91%, and it's cut to a quarter after a win above 18,000.
- **The theater never takes your money.** It turns you away without 5 C-Bills, but never charges.
  The Amiga version does charge.
- **The salesman will sell you the holoviewer twice.** He never checks whether you already bought
  it, so you can pay 7,000 C-Bills again. The terrain mapper's seller does check.
- **Paying for one repair can fix two weapons, then charge you for nothing.** At the repair shop,
  300 C-Bills mends every destroyed weapon of the chosen type but only counts one. A 'Mech with two
  of a type destroyed sees the type offered again, and paying again mends nothing.
- **Renting a 'Mech for the Arena is cheaper than it says.** The repair bill after a win is only
  text: nothing is paid.
- **A new game doesn't quite start fresh.** The explored map isn't cleared, so a second game in the
  same sitting keeps the first one's. The dice, the menus' last choices and the combat settings
  carry over too (1000:4dc7).
- **The 'Mech stats screen flickers differently every time.** Two counters behind its colour cycle
  are never given a starting value, so they hold whatever was left in memory. The red pulse comes
  at different moments from run to run on the same keys (15ab:1afe).
- **The party's dot on the overhead map gets a fresh random colour every frame.**

## The demo

From 1000:51f9 (details: [startup.md](startup.md)).

- **Leave the title alone and the game plays itself.** When the title tune ends with no key
  pressed, the game plays the keys recorded in `DEMOFILE`: 1,023 bytes, 717 keys (697 of them
  arrows) and 64 pauses. The party walks the town, turns down two shops, runs the training course,
  looks at the overhead map, and goes from 35 to 40 C-Bills. The dice are set to their own fixed
  start first, so it plays the same every time.
- **The developers' demo recorder is still inside.** The same key reader can record keys, and a
  typed `h` stores a pause. But the only thing that ever sets its switch is a zero, and nothing
  writes the recording to disk. It was the tool for making `DEMOFILE`, left unreachable.

## Fights

From the fight code in segments 1e31, 203b and 22e8 (details: [combat.md](combat.md)).

- **The computer's tactics are simple and never random.** Its planner rolls no dice. It fires every
  weapon at every range whatever its heat, and always tries a kick. A computer 'Mech runs when its
  heat is 0, walks otherwise, and never jumps (1e31:03ab).
- **Enemy 'Mechs kick terribly, likely by mistake.** An enemy kick looks up its own leg actuators
  in the wrong place, past the 'Mech records and into a blank part of memory. So every enemy kicker
  counts as missing all six: +6 to the roll it needs.
- **Every hit to a 'Mech's insides causes a critical.** In the board game, a roll under 8 gives
  none. Here it gives one; 10-11 give two and 12 gives three (1e31:11ab).
- **Ammunition never explodes.** A critical on an ammo slot just destroys it like any other part.
  At heat 30 a 'Mech can't move or fire until it cools, and there's no shutdown roll.
- **Enemy 'Mechs have no heat limit.** The end of the round caps heat at 30, but checks the wrong
  'Mech, so only yours are capped. An enemy past exactly 30 can even move again (1e31:0c63).
- **Let the computer fight with graphics off, and terrain doesn't exist.** A figure's ground
  (water, cover, fire) is only worked out when it is drawn. When the computer fights for you with
  combat graphics off, nothing is drawn, so no figure gets terrain bonuses, water cooling or
  fires. With graphics on, only figures in Jason's view do.
- **Sound can decide whether enemy soldiers ran away.** A soldier whose target is gone measures
  its escape from a figure that doesn't exist. That figure's position is partly a number the
  speaker routine left behind from the last sweep sound played with sound on.
- **'Mechs squash people.** A 'Mech stepping beside an enemy on foot in the same row crushes them.
  The code treats your first companion's death like Jason's: either one ends the fight.
- **Pistols at a 'Mech do nothing at all.** No roll, no damage, no message.
- **Distance is measured oddly.** The game counts the shorter side plus half the longer, so 10
  half-cells straight is 5 but 10 by 10 diagonally is 15 (1e31:0bb5).
- **Running is easy.** Declining an ambush escapes it 3 times in 4, and so does Flee. The Arena and
  the jail break still roll the flee dice, then refuse anyway.
- **A random ambush never brings more 'Mechs than you have.**
- **In the jail break, the third bay always works.** Whichever bays you try, in any order, the first
  two 'Mechs won't start and the third always does. It's a count, not a place.
- **Practice makes perfect, slowly.** A party member on foot who fires 256 shots with the same skill
  gains a point, up to 4. Switching skills starts the count over.
- **The double agent shuts the town and brings ambushes.** While he's aboard and undiscovered, the
  Arena, the shops, the Lounge and more turn you away, and an ambush can start one tick in 32. After
  he betrays you, one in 128. Seeing through him isn't saved: load a game and he's hidden again.

## After the fight

From 15ab:0002 (details: [combat.md](combat.md)).

- **Scrap money goes by your tech's place in the party, not their skill.** Each wreck is worth a
  random 90-217 C-Bills, multiplied by the best tech's position in the party list. If Jason is the
  best tech, he's number 0, and you get nothing. Likely a mistake.
- **Free SRM 6 repairs, thanks to leftover memory.** The repair code never clears its spare count
  for one weapon, the SRM 6, so the count starts with leftover data. With a tech of skill 3 or more,
  every destroyed SRM 6 gets mended whether or not the wrecks had one.
- **A good tech mends every heat sink.** With Tech 2 or more, all destroyed heat sinks are fixed,
  even when the wrecks had none.
- **Jason is offered the soldiers' weapons even when they had none.** The check reads the wrong
  records, which are seldom empty.

## Saved games

Details: [formats/save.md](formats/save.md).

- **A saved game is a memory snapshot.** 3,913 bytes: one mark byte, a block copied straight from
  memory, and the party's position. The dice and the sound switch aren't saved, so every run
  starts with sound on.
- **Saving inside the Star League cache loses your 'Mechs.** On the way in, the game puts the
  'Mechs aside in memory the save doesn't cover. Load that game and the party comes out with none.
- **A save that fails half way loses two games.** The game empties the slot's old file before
  writing the new one.

## The world

Details: [wilderness.md](wilderness.md), [cache.md](cache.md),
[overhead_map.md](overhead_map.md).

- **The world is grown as you walk.** It is 16 by 16 areas of 64 by 64 cells. Outside the towns,
  each area has one height, and the land is filled in by repeatedly splitting squares and nudging
  their middles with noise from the start-up table (287f:104e).
- **A slip in that maths shapes the whole map.** On one side of each square, the size meant to
  scale the nudge comes out as 0, so the whole random number is added instead of a small nudge,
  and the point usually ends up at 0 or high (287f:0df8). Fixing it would change every outdoor area in the world, so the rebuild keeps it.
- **The cache is one long fetch quest.** It has 33 items, one per terminal, and 11 locked doors.
  Each door takes three items, one of each kind, and every item is used exactly once. The shortest
  route found opens the doors in the order 1, 2, 0, 6, 10, 8, 4, 5, 3, 9, 7, in 5,670 steps.
- **The cache's floor puzzle punishes mistakes.** Its tiles flip in pairs when stepped on. The
  check passes only when exactly the right seven are flipped, and a wrong check resets them all
  (1b5d:0980).
- **Two small maps break the overhead map.** Maps 12 and 13 move the map store on by 512 bytes
  though they hold only 64, so a map drawn after them in the same view starts 448 bytes early, and its
  last 7 blocks are never shown (1000:3fae).

## The program and its files

Details: [original.md](original.md), [formats/](formats/).

- **It's small.** The program ships squeezed with Microsoft's EXEPACK at 152,429 bytes and unpacks
  to 260,528. The game itself is about 100 KB of code in about 210 functions, around 500 bytes
  each. For scale, MechWarrior 3 has about 3,850. All four display drivers (CGA, Tandy, EGA/VGA
  and MCGA) are in the one file.
- **The font lives inside the program.** It is 1,024 bytes in `BTECH.EXE`: 128 characters, 8 by 8.
  Characters 2 and 6 are tiny pictures, an arrow and a check mark, but inside printed text those
  same bytes mean "change colour".
- **The story scripts are lightly scrambled.** Each place's script (`.BLD`) is enciphered with a
  simple byte-by-byte cipher and run by a small script interpreter. The biggest, the hut's, is
  8,499 bytes in a 9,000-byte buffer; anything longer would have overflowed it.
- **A shopkeeper's line nobody has seen.** The weapon shop's script always jumps over one block
  (`WEAPON.BLD` 0160–021f), so one of the shopkeeper's lines can never appear. It is his answer
  to a customer who won't buy: he looks a little crestfallen, as if your thrift might cost him his
  supper, puts the suit away and shows you the rest of his stock. Likely the reply to turning down
  a suit, from its wording; whatever was meant to lead to it isn't in the script. The same block
  holds a second heading for the stock menu that nothing else prints.
- **EGA colours are shuffled.** The title screens' palette shows colour 1 as black and swaps
  5 and 9. On CGA the game fakes extra colours by switching between two colour tables on alternate
  rows, a dither.
- **The training 'Mech is hollow.** The Chameleon you train in has no internal structure in five
  of its locations, and its model number makes every location count as intact for repairs.
- **The PPC's minimum range is in the data but never used.** It matches the board game's 3, and no
  code reads it.

## The other versions

Details: [versions.md](versions.md), [formats/amiga.md](formats/amiga.md),
[formats/c64.md](formats/c64.md), [formats/rdos.md](formats/rdos.md).

- **The Amiga squeezes its dialogue into letter pairs.** The 128 most common pairs each take one
  byte, making the scripts about a third smaller: 55,861 bytes against DOS's 85,379. Laid back
  out, 24 of the 26 scripts match DOS byte for byte.
- **The Amiga has no font of its own.** It never loads one, so it prints in the machine's built-in
  system font.
- **The Amiga trades the speaker for 19 recorded sounds**, played on its four channels in turn, so
  they overlap.
- **The Amiga has a music player that's never switched on.** It holds a three-channel tune player
  with nothing to play and nothing that starts it, so its title screen is likely silent.
- **The Commodore 64 and Apple II towns were redrawn from scratch.** Same places, same ingredients
  (the walled park with its lake), but new buildings, roads and layouts: none of 78 entrances is
  where DOS has it. Only the Star League cache survives, 4,045 of its 4,096 cells the same.
- **The Apple II disks have their own little disk system.** They aren't Apple DOS 3.3 but "RDOS
  3.3", which numbers each track's sectors in reverse.
