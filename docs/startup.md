# The start-up

From running BTECH to the town, in a new game. Rebuilt in `src/game/startup.cpp`, and compared
with the original by the rig's `start_*` cases (docs/reference.md): all match pixel for pixel.

## Order

1. **Hardware questions, text mode.** The graphics adapter, the disk drives, then a note about
   saved games. These choose the original's drivers and disks. The rebuild has neither, so they
   are not rebuilt.
2. **The Infocom/Westwood screen** (in `1527:0044`, at 1527:0131..0216): INFOCOM.CMP over the whole
   screen. The keyboard is emptied, then the screen stays up for at most 700 display frames,
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

The title's tune plays while the title waits (docs/sound.md, "The title music"). Not rebuilt
yet: the demo at the tune's end. After the tune (59.7 s on the speaker) the rebuild's title
waits silently for a key however long.
