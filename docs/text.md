# Text on the screen

The program writes all its text and box frames into a 40x25 grid of 8x8-pixel cells (the
320x200 screen). Rebuilt in `src/game/text.{h,cpp}`; drawn by `src/platform/text_layer.cpp`.
The game logic only says which glyph or frame piece goes in a cell, in which colours; the
renderer looks each one up by asset ID (`{"FONT", c}`, `{"BTBORDER.CMP:8x8", n}`), so a
makeover can replace the font and the frames. The 16 colour numbers are asset IDs too
(`{"COLOUR", n}`, a 1x1 picture, below).

## Colours

A cell's colours are one of two things (`src/game/text_colour.h`):

- **A role**, when the game logic picks the colour: what the text is (ordinary text, a heading,
  a label, an amount of money, a worn or destroyed part, a combat message about the party or an
  enemy, a file name, a warning, the background behind written text). The logic never names a
  colour number. The renderer maps each role to its look; the original look's table, with the
  original address each value comes from, is in `src/platform/text_layer.cpp`. Several roles share
  one original colour (`1000:28a2` sets light green for headings, labels, sums and reports alike),
  so a makeover can tell them apart.
- **A data colour**, when the colour comes with imported data: the window table's colours and the
  colour codes inside a text (0x02, 0x06 below). Those are colour numbers in the data; a mod
  changes them there.

Whatever the source, the renderer ends with a colour number 0-15, and paints it with screen
colour `{"COLOUR", n}` from the asset provider (`assets::colour`,
`platform::screen_colours`). The default provider answers with the original's palette (the
import's EGA palette, formats/tables.md), so the screen is as before. A mod provider may give any
number another colour, and it applies to everything the renderer paints by number: text, the
panel's bars, shot lines, the overhead map and the orders screen's boxes. The original's
pictures (.CMP, .ICN, .ANM, the sprites) keep their own colours, as does the stats screen's
cycling palette (`stats_palette`); a mod replaces those by their own IDs.

The original also picks a second set of colours when DG:4fba, the display, is 0 (CGA). The table
notes them; the rebuild draws the EGA display.

The menus' highlight bar (`287f:2b87`) XORs the menu's highlight colour into a row of cells; the
same call again takes it away, and painting a cell takes it away too. In the rebuild the logic
only says which menu's bar is on which cells (`TextScreen::menu_bar`, `TextCell::bars`); the
renderer draws it, and the original look XORs the menu record's highlight colour (tables.json) in.
A box over text on the map (combat.md 2.6) is kept the same way, as its marks
(`TextCell::box_marks`).

## Text windows

A table of 11 records of 16 bytes at 385b:0000 describes the windows: x, y, width and height (in
cells), foreground and background colour, and the cursor's column and row. The values are
imported into `tables.json` (formats/tables.md). `2656:0281` selects a window (keeping the old
one's colours and cursor), `2656:0388` clears it. The windows the rebuild uses so far: 0 is the
whole screen, 3 and 4 are the panel's party box and upper box, 6 holds the new-game text, 7 is
the message box over the map, and 8 is the quiz's column of part names.

## Frames (2656:0004)

A frame goes one cell outside the current window. Each style names one of three piece tables
(DG:4fa4); a piece table lists the four corners (top-left, top-right, bottom-left, bottom-right),
then the top, left, right and bottom edges, each a run of pieces ended by 0xff that repeats along
the edge (`2656:01e7`). The pieces are the 8 cells the start-up cuts from the top-left of
BTBORDER.CMP (docs/formats/font.md). The tables are imported into `tables.json`.

The panel (`1000:4cac`) frames window 3 with style 3 and window 4 with style 4, so the two boxes
share the middle bar: 13 cells, 104 pixels wide, beside the 216-pixel map.

## The printer (2656:03f5)

Prints a piece of text into the current window, word-wrapped, a word at a time. The text uses
these bytes:

| Byte | Meaning |
|---|---|
| top bit set | the last character (the rest of the byte is the character) |
| 0x00 | end |
| 0x0d | new line |
| 0x02 n | background colour n |
| 0x06 n | foreground colour n |
| 0x09 n | cursor to column n (column 0 if n is past the width) |
| 0x13 n | spaces up to column n, counted from the cursor |

A word that does not fit goes on the next line, leading spaces dropped; a word as wide as the
window is broken. Each line goes out through `2656:07cb`: below the last row it scrolls the
window up a row, clears the bottom row to colour 0 and writes there.

`273d:00d5` writes a string at a cell position without wrapping (the panel's names).

## The yes/no question (1000:1a13)

Prints a new line and then the two answers, with the picked one in black on yellow (colour 0 on 14).
Y or N answer at once; left picks Yes, right picks No; space or Enter accept the pick. After
every key the line is printed again over itself. The window's foreground colour is put back at
the end.

## Messages

`24d3:17c6` opens the message box: window 7, cleared, frame style 0.

## Text IDs

Every text the game shows has an ID and comes from the text provider (`src/game/text_provider.h`),
never from a literal in the code. The importer (docs/import.md) writes the program's texts with
their IDs; an override file can replace any of them.

| ID | Address | What it is |
|---|---|---|
| walk.too_deep | 46db:0410 | the message when water blocks the step |
| walk.enter_question | 46db:0430 | the enter-a-place question's first part, ending in a new line |
| walk.enter_question_end | 46db:0444 | its closing mark |
| walk.mech_enter_question | 46db:0446 | the same first part, the copy asked of a 'Mech |
| walk.mech_enter_question_end | 46db:045a | its closing mark |
| walk.direction.north ... northwest | 46db:006c - 00a7 | the eight compass directions' names, each after a new line, north first and clockwise |
| ui.answer_yes_picked | 46db:03ee | the yes/no line, Yes picked |
| ui.answer_no_picked | 46db:03fe | the yes/no line, No picked |
| panel.party_heading | 46db:0aca | the party box's heading over the three bar columns (9-11) |
| character.0 - character.7 | 46db:00b2 - 00d9 | the eight characters' names, Jason first |
| mapN.place.E | MAPN.MTP | the place behind entrance E |
| mapN.person.P | MAPN.MTP | the map's people |

The combat texts (import version 7) are grouped by the function that prints them; the import
table in `src/formats/exe_data.cpp` gives each one's address:

| IDs | Printed by | What |
|---|---|---|
| mech.&lt;id&gt;, weapon.0 - weapon.32 | (the rule tables) | the 'Mech templates' and weapons' names, read from inside the tables |
| fight.* | 203b:000a | the encounter, the engage question, the double agent's story (fight.agent.0-16), the end of a fight, the raid's five story lines (fight.raid.0-4) |
| orders.*, options.*, scan.* | 203b:14c3 and its helpers, 15ab:1467 | the orders menus, movement messages, the movement modes (orders.mode.0-2), the combat options, the Scan browser |
| weapons.*, pick.*, destroyed.* | 1d43 | the Use Weapons screen, the range bands (weapons.range.0-3), the target picker, a 'Mech destroyed |
| shot.* | 22e8:000c, 22e8:12c7, 1e31 | the pieces of a shot's message, hit and miss, critical, kills |
| location.0 - location.10 | 1e31:1b8f | the hit locations, by location index (armour byte 0x11 + index), not the program's name order |
| condition.0 - condition.10 | 1e31:031e | a person's condition, by hit points |
| armour.0 - armour.5 | (DG:4e8a) | the personal armour types |
| unit.*, after.* | 15ab | the unit panel; repairs, scrap, salvage and loot after a fight |
| mission.* | 17dc:0629 | the training missions' messages |

The question for an entrance is `walk.enter_question` + `map1.place.1` +
`walk.enter_question_end`. The yes/no keys are still Y and N whatever the language (to be made
data with the input layer).

## Interface text and imported text

Owen's rule (2026-11-15): generic interface text ships in a default text file in the repo,
`data/text/interface.json`, layered under the import. Story, dialogue, descriptions and every
.BLD text stay imported only. This list says which IDs go where. A new ID is imported only unless
it plainly fits the interface rule; when in doubt, imported.

**The rule used.** *Interface* covers menus, prompts and questions, labels and headings, the
small pieces a message is built from (punctuation, line breaks, a shot line's verbs and joining words), and messages
about the controls or a rule's state (out of ammo, overheated, can't kick). *Imported only*
covers names (people, places, 'Mechs, weapons, armour, 'Mech parts), story and dialogue,
narration of what happened, descriptions and flavour, and the original's own notices.

### Interface: in the repo default (151 IDs)

| IDs | Count | What |
|---|---|---|
| walk.too_deep, walk.enter_question, walk.enter_question_end, walk.mech_enter_question, walk.mech_enter_question_end, walk.direction.* | 13 | walking messages, the move log |
| ui.answer_yes_picked, ui.answer_no_picked | 2 | the yes/no line |
| panel.party_heading, panel.cbills_label | 2 | panel labels |
| intro.first_time, intro.load_old_game | 2 | the start-up's two questions |
| talk.nobody, talk.you_see, talk.done, talk.end, talk.line_end | 5 | the talk menu's frame and pieces |
| after.repair.break, after.scrap.plural, after.scrap.end, after.salvage.question, after.loot.swap_question, after.loot.swap_for, after.loot.swap_end | 7 | the questions after a fight and their pieces |
| fight.computer_question, fight.engage, fight.attackers, fight.attackers_mech, fight.attackers_and, fight.attackers_human | 6 | the encounter's questions and the attackers line |
| orders.* (with orders.mode.0-2), options.* | 28 | the orders menus, movement messages, the combat options |
| scan.* | 11 | the Scan browser |
| unit.possessive, unit.weapon_armour, unit.ruined, unit.none | 4 | the unit panel's labels |
| weapons.* (with weapons.range.0-3), pick.*, destroyed.* | 34 | the Use Weapons screen, the target picker, a 'Mech destroyed |
| shot.* but shot.squashed, shot.arena_damaged, shot.urbanmechs | 26 | the pieces of a shot's message, hit and miss, critical, kill |
| location.0-10 | 11 | the hit locations, as labels |

### Imported only (791 IDs)

| IDs | Count | Why |
|---|---|---|
| bld.* | 424 | the places' scripts: story and dialogue (all .BLD text) |
| mapN.place.E, mapN.person.P | - | names from the map files, read from the player's .MTP at run time (not in the import); every map's at start, since a map is loaded mid-game as the party nears it |
| character.0-7 | 8 | names |
| mech.*, weapon.0-32, armour.0-5, quiz.part.0-19 | 67 | names of 'Mechs, weapons, armour and parts |
| quiz.message, quiz.question | 2 | the quiz scene, which belongs to the copy protection and the manual |
| intro.notice.0-3 | 4 | the original's trademark and copyright notice: ours would not be theirs |
| course.* | 16 | the teacher's dialogue |
| talk.hello, talk.going_to, talk.reason.0-11 | 14 | the townspeople's dialogue |
| place.follow_on.0-1 | 2 | the narration before VIEWDISK after the garage (17dc:0008) |
| space.items.0-2, space.items.allocate, space.no_dismount | 5 | the Space command menu and its refusal (1000:2c50, 4d57); menus, but kept imported: interface.json holds only what Owen approved |
| after.* but the seven above | 14 | narration: repairs, scrap, salvage, loot |
| unit.suspicious | 1 | the double agent's clue (story) |
| fight.* but the six above | 35 | the encounter descriptions, the double agent's story, how a fight ended |
| shot.squashed, shot.arena_damaged, shot.urbanmechs | 3 | narration of story events |
| condition.0-10 | 11 | a person's condition, as flavour descriptions |
| mission.* | 10 | the training missions' story |
| cache.door_wants.0-2 | 3 | the cache's doors asking for the items they need (1b5d:0ab6): story |
| cache.item_kind.0-2, cache.item_take, cache.item_number, cache.item_take_end, cache.item_used | 7 | the cache's terminals offering their items (1b5d:03aa): story |
| cache.wall_opened, cache.screen, cache.hint | 3 | the cache's text tiles (1b5d:01e9, 02a8, 02d2): story |
| cache.way_down, cache.way_down_2, cache.puzzle_done, cache.way_up, cache.puzzle_solved, cache.puzzle_wrong | 6 | the cache's second level and puzzle (1b5d:055a, 079c, 0980): story |
| cache.out_closed, cache.out_puzzle, cache.out_findit | 3 | the cache's way out refusing (1b5d:04ab): story |
| heal.* | 17 | healing (1c31:000a): who treats, with what care, and who is still hurt |
| crew.* | 21 | the crew screen (1c67:0002); a menu, but kept imported: interface.json holds only what Owen approved |
| recruit.* | 5 | a new member joining (19b8:0d58) |
| offer.give, offer.to, offer.drop, offer.already | 4 | a found weapon offered to the party (17dc:15e6) |
| offer.armour_* | 4 | armour offered to the party (17dc:13de); its own copies of the first two |
| weapon2.trade, weapon2.trade_end | 2 | WEAPON2's question around Jason's own weapon's name (24d3:140d, special 41) |
| repair.* | 34 | REPAIR's repair shop (19b8:0002, special 18): each repair's count, price, question and shortfall; repair.system.0-2 by DG:1b3e's pointers |
| ammo.* | 10 | REPAIR's ammunition (19b8:1762, special 44) |
| upgrade.* | 6 | REPAIR's speed shop (19b8:080a and 0925, specials 19 and 20) |
| over.leader.0-1, over.killed, over.new_game | 4 | the game over when the leader dies (1000:04a4), and its question |
| ui.press_key, ui.plural_s, ui.full_stop | 3 | message pieces, kept imported: not on Owen's approved list |
| shop.cbills_end | 1 | the end of a shop's price line, kept imported: not on Owen's approved list |
| comstar.* | 26 | ComStar: the investments' names, the account, investing and withdrawing (24d3:037a, 0725, 04ad, 17dc:19e1); kept imported: not on Owen's approved list |
| arena.* | 2 | ARENA's own 'Mech: the chooser's prompt and the separator between a pilot and a 'Mech (17dc:1a26, 1c67:0b98); kept imported: not on Owen's approved list |
| shop.topup_* | 11 | ARMOR's top-up (special 32, 24d3:0ef3); kept imported: not on Owen's approved list |
| save.which, save.failed, save.refused | 3 | the save's slot question, its failure and its refusal in the cache (1000:35d3); kept imported: not on Owen's approved list |
| overhead.caption, overhead.caption_outdoors | 2 | the overhead map's caption (1000:3fae); kept imported: not on Owen's approved list |
| load.which, load.not_a_save, load.not_found, load.not_found_end | 4 | the load's slot question, a file without the mark, and a file that cannot be opened (around its name) (1000:32b3); kept imported: not on Owen's approved list |
| settings.* | 9 | the game settings and their questions (1000:3bd0); kept imported: not on Owen's approved list |
| inspect.* (with inspect.skill_level.0-4) | 23 | the inspect screen's labels, questions, armour lines, kit answers and skill levels (1000:378d, docs/town_loop.md); kept imported: not on Owen's approved list |
| stats.* | 16 | the 'Mech stats screen's labels, crew placeholders, weapon locations and actuator states (15ab:1afe, docs/town_loop.md); kept imported: not on Owen's approved list |

### The cases decided by hand

- **location.0-10** are rule terms, the record sheet's hit locations: interface.
- **destroyed.mech, destroyed.eject, shot.killed** are combat feedback: interface.
- **talk.nobody, intro.load_old_game** are about the game's own controls: interface.
- **fight.not_evaded, fight.no_escape, fight.eluded, fight.eluded_2, fight.some_fled,
  fight.all_fled, fight.return** narrate how a fight ended: imported only.

### How it works

- `data/text/interface.json` (id, address, text) holds exactly the interface IDs above, in the
  original's words as the import writes them. The build copies it next to the program
  (`game::kInterfaceTextFile`).
- Layers, first found wins: the override files, the import, the map files' names, then the repo
  default. The import still wins wherever it has an ID.
- Tests (tests/test_interface_text.cpp): the file holds the IDs on a hand-kept list in the test
  and nothing else, so a story text can't slip in; with the game folder, each text equals the
  imported one. Every ID added to the file goes on that list.
- This document quotes only interface texts.
