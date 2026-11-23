# The setup and the launcher contract

The rebuild installs the same way as the rest of the family's rebuilds. The
shared parts come from re-setup-kit (`extern/re-setup-kit`, a submodule, MIT;
its DESIGN.md has the contract): it finds a copy of a game in whatever the
player points at (the program, its folder, an archive it came in, at any
depth), copies it safely, and answers the launcher. The kit knows no game.
This game's part is the recipe in `src/setup/setup.h` (no SDL) and the
program's side in `src/platform/sdl_setup.h`.

## Names

| What | Value |
|---|---|
| `--status`'s game id | `inception` |
| The launcher's key (the game folder's mark, `rebuild-game-folder.txt`, `game=chi`) | `chi` |
| The player's folder | `SDL_GetPrefPath("BattleTech", "Inception Rebuild")` |
| A game folder made inside a picked one | `Inception Rebuild` |
| The game's files, unless the player picks another folder | `<player's folder>/install` |

## The player's folder

It holds the import (docs/import.md), the saves (`Game1` to `Game6`,
docs/formats/save.md) and `settings.ini`. It is `--data-dir DIR`, else the
`CHR_DATA_DIR` environment variable, else SDL's per-user folder for the names
above (on macOS `~/Library/Application Support/BattleTech/Inception Rebuild`).

The folder had two earlier names, each straight in the per-user base:
`inception-re`, and before it `crescent-hawks-rebuild`. On each start, when
the new folder has nothing in it, the newest earlier folder with something in
it is renamed to the new name once (the kit's `move_once`: never a copy and
never merged). An older one beside it is left as it is. If the rename fails
(another drive, a file in use), the old folder is used where it is for that
start, and the move is tried again at the next one.

`settings.ini` holds `game_folder=<the game's files>`, written when the setup
chooses the folder. Other lines in it are kept.

## Where a start gets the game

1. `--game-dir DIR`, else the `CHR_GAME_DIR` environment variable: used as
   given, with no setup.
2. `settings.ini`'s `game_folder`, while it is still the game.
3. The folder set when the program was built (`CHR_GAME_DIR` in CMake), while
   it is the game. Packages build with it empty.
4. The player's own `install` folder.
5. The setup screen (the kit's): a copy the search found is offered first
   (never taken unasked); otherwise the player finds or picks one. It is
   copied into the install folder: staged as `install.new`, then swapped in,
   so a failure or a cancel leaves nothing behind. The player's original
   (folder or archive) is only read.

A run that no player is at never opens the screen, a picker or a window that
takes focus. That covers `CHR_HIDDEN`, SDL's `dummy` or `offscreen` video
driver, `--script` and `--screenshot`. If such a run has no game, it fails
with a message. On macOS these runs are background apps
(`SDL_HINT_MAC_BACKGROUND_APP`), set before any `SDL_Init`.

## What counts as the game

The walk looks for two clues:

- `BTECH.EXE`, judged by the importer's own check (docs/import.md, "How it
  reads the program"). The layout and every imported piece must match DOS
  1.03. MechWarrior 1's program has the same name, so a name match alone
  means nothing.
- `MAP1.MTP`, which only this game has. This lets a copy of another version
  still be found as this game, ranked below a known `BTECH.EXE`.

A folder is:

- **ready** when its `BTECH.EXE` is DOS 1.03 and every data file the
  rebuild reads is there: the 26 `.BLD`, 15 `.MTP`, 22 `.ANM`, 5 `.ICN` and
  7 `.CMP` files, `WWOODBT.SIF` and `DEMOFILE`. `$VERIFY.EXE` is not needed.
- **incomplete** when its `BTECH.EXE` is another version (beside `MAP1.MTP`)
  or when data files are missing. The launcher's PLAY stays on.
- **missing** otherwise.

The search looks in the usual folder names for a DOS game under the system
drive, the home folder and any Wine prefix (`setup::hints`), two levels down,
and in a folder dropped beside the program.

## The launcher contract

- `chr --status` prints one JSON line (the kit's `status_json`: `game`,
  `version`, `files.state`, `files.where`, `files.data`, the missing files and
  a note) and exits 0. It makes and writes nothing (it works out the player's
  folder from the environment, as SDL would name it) and opens no window.
  With another version's `BTECH.EXE`, the note says that the rebuild needs
  DOS 1.03.
- `chr --setup` opens the setup screen whatever is found, plays nothing, and
  exits 0 (ready), 1 (cancelled) or 2 (failed, with `{"error": ...}` on
  stdout).
- `--status` reports the program's version as `CHR_VERSION` (CMake; `dev`
  unless a package sets it).

## The checks

- `setup_test` (`tests/setup_test.cpp`) runs the recipe on folders it makes:
  the folder move (each order, a failed move), `settings.ini`, the verdicts
  (another game's `BTECH.EXE`, another version's copy), `--status` writing
  nothing, and installs refusing a folder that isn't ours. Given the game
  folder, it also installs from a folder and from a zip built in the work
  folder, where an entry that climbs out of the game's folder is left out.
- `status_setup` (`tests/status_setup_test.py`) runs the built program with a
  made-up home, its own player folders and SDL's dummy drivers. It covers
  `--status`, `--setup` hidden and through the kit's screen (driven by its
  `REKIT_SETUP_*` hooks), `CHR_SETUP_PICK` (a test's stand-in for the
  player's pick), a start playing from the remembered folder, another
  version's `--status`, and the screen's CHANGE FOLDER with its
  `game=chi` mark.

Both remove their work folders. They print names and counts only.
