# The rules this project keeps

BattleTech: The Crescent Hawk's Inception (1988) is commercial software that
nobody sells any more. It is not shareware and not public domain: it is still
under copyright. Infocom published it, Westwood Associates wrote it, and
BattleTech is a trademark of its owners. This project exists to preserve how
the game worked and to let people play it on modern systems. These rules keep
it on the right side of that line. Every change is checked against them.

1. **We never distribute the original's files.** No executable, data, maps,
   graphics, sound or game text goes into this repository, its releases or
   its installers. One exception is the game's generic interface text
   (menus, prompts, labels and rule messages), listed ID by ID in
   docs/text.md and kept in data/text/interface.json; story, dialogue, names,
   descriptions and every .BLD text stay imported from your own copy only.
   You supply your own copy of the game, and the rebuild reads it from your
   disk.
   The other is a few small screenshots of the rebuild running
   (docs/screenshots/), to show what it is.
   This rule can bend where the situation calls for it. The project's owner
   decides each case.
2. **No decompiled code in the repository.** We study the original program to
   understand what it does, then write our own C++ that does the same thing.
   Comments may name the original function's address and the name the
   disassembler gave it, so readers can cross-reference
   (`// BTECH.EXE 1000:0e34 (Ghidra FUN_1000_0e34)`). The decompiler's output
   itself is never pasted in, cleaned up or not.
3. **No cracks.** We publish nothing that patches or bypasses the original
   program's copy protection. The rebuild is new code and has no protection
   check; where the original needed the manual, the rebuild gives you that
   information in our own words, never in copies of the manual's pages.
4. **Unofficial.** This project is not affiliated with or endorsed by
   Activision, Electronic Arts, Microsoft, Topps, Catalyst Game Labs or any
   other holder of rights in Infocom, Westwood or BattleTech. We use none of
   their logos or branding.
5. **No money.** Nothing is sold. There are no paid builds and no donation
   links.
6. **If a rights holder asks, we comply.** If anyone holding rights in the game
   objects, we take down what they object to and talk to them.

## How the rebuild is built

The original's hardware is replaced, not emulated. Its graphics drivers, its
speaker and sound-chip code, and its BIOS and DOS calls give way to SDL3 for
the window, drawing, sound, input and files. Nothing in the rebuild models a
chip, a port or a timer of the old machines; where the original drove the
hardware, a note beside the function that stands in for it says what the
original did.
