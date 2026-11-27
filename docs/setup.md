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

## Names on Windows

SDL's names (the arguments, the player's folder, the program's folder, the
dialogs) are UTF-8. The C library and `std::filesystem` read and write narrow
names in the program's code page. `tools/windows/utf8.manifest` makes that
page UTF-8 too (Windows 10 1903 and later; an older one doesn't read it, and
keeps the system's page), so both mean the same folder.

Version 1.0.0 had the system's page. What it may have left behind is read back
(`src/setup/old_names.h`), only where the program's page is UTF-8 and the
system's isn't, and only for a name with a byte above 0x7f:

- `settings.ini`'s `game_folder`, when it isn't UTF-8, is in the system's page
  (1.0.0 wrote a folder from `CHR_DATA_DIR` or the search so) and is read in
  it. When it is UTF-8 (a folder from a dialog or `--folder`), it is first read
  as it is, then in the system's page: 1.0.0 took those bytes for text in that
  page, so the game may be in the folder so named. The first reading that is
  the game's folder is used, else the first. A line neither reading can make
  anything of counts as none, and the setup asks. The line is written in UTF-8
  when the setup next remembers a folder. No folder is renamed.
- The player's folder itself: where Windows let 1.0.0 make one under the
  misread name, its saved games are there. When the folder of that name has a
  `Game1` to `Game6` and the player's folder has none, a start says where they
  are (`tell_left_saves`): on stderr (`chi.log`), and in a message box while
  nothing is set up. Nothing is moved or copied.

Not covered: a system page changed since 1.0.0 ran.

A name that still can't be turned into text ends a start with a message
(stderr, and a message box when a player is at it) and exit 1, or 2 with
`--setup` (`guarded`, `platform/sdl_setup.h`). `--status` answers with a note.

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

   When the search finds no copy, the screen also offers to download the
   original files from the launcher's catalog (the kit's
   `Game::original_catalogs`). The catalog is asked at
   `reverse-engineered.com`, then at its copy on GitHub, and is used only
   when its signature checks against the launcher's key. Its entry for this
   game is `chi` (`setup.h`, `kOriginalKey`). The download must match the
   catalog's size and SHA-256, and is installed at once like a picked
   archive, then deleted. A copy found on the computer is always offered
   instead, and the download is never asked about then. `setup_download`
   (tests/setup_download_test.cpp) checks this against a local catalog
   signed by the kit's test key, serving a zip it builds from
   `CHR_GAME_DIR`. It uses no network.

A run that no player is at never opens the screen, a picker or a window that
takes focus. That covers `CHR_HIDDEN`, SDL's `dummy` or `offscreen` video
driver, `--script` and `--screenshot`. If such a run has no game, it fails
with a message. On macOS these runs are background apps
(`SDL_HINT_MAC_BACKGROUND_APP`), set before any `SDL_Init`.

## What counts as the game

The walk looks for three clues:

- `BTECH.EXE`, judged by the importer's own check (docs/import.md, "How it
  reads the program"). The layout and every imported piece must match DOS
  1.03. MechWarrior 1's program has the same name, so a name match alone
  means nothing.
- The Amiga's program `battletech`, alone or in a folder `Battletech`, judged
  by the Amiga reader (docs/formats/amiga.md). It ranks below DOS's.
- `MAP1.MTP`, which only this game has. This lets a copy of another version
  still be found as this game, ranked below a known `BTECH.EXE`.

The kit's walk can't read a disk image. `find_amiga` (`setup.h`) looks for the
Amiga's disk in what was picked: an .adf, a folder, or archives up to four
deep (a download can be a zip of zips). Images with the same game files are
one copy. Of several different copies the setup takes the plain build and says
which it took; without a plain one it takes none and names them. The install
writes out the game's 18 files and nothing else on the disk, so no saved game
of whoever had it before.

The setup screen offers such a find like any other (the kit's `find` hook, asked
when its own walk finds no copy); a picked .adf is looked at itself, not only
its folder. `--game-dir` plays an .adf where it is.

A folder is:

- **ready** when its `BTECH.EXE` is DOS 1.03 and every data file the
  rebuild reads is there: the 26 `.BLD`, 15 `.MTP`, 22 `.ANM`, 5 `.ICN` and
  7 `.CMP` files, `WWOODBT.SIF` and `DEMOFILE`. `$VERIFY.EXE` is not needed.
  Without a `BTECH.EXE`, also when it is an Amiga copy (docs/import.md, "Which
  source a copy is") whose program the importer reads and whose 18 files are
  all there. An .adf itself counts the same way.
- **incomplete** when its `BTECH.EXE` is another version (beside `MAP1.MTP`)
  or when data files are missing. The launcher's PLAY stays on. An Amiga copy
  in an archive, or several different ones in a folder, is incomplete too: a
  start can't play it there, and the setup takes it.
- **missing** otherwise.

The search looks in the usual folder names for a DOS game under the system
drive, the home folder and any Wine prefix (`setup::hints`), two levels down,
and in a folder dropped beside the program. A file or folder whose name has no
text in the program's code page (Windows), or that can't be read, is passed
over, and the search goes on; what stops a look at a place is said on stderr.

## The launcher contract

- `chr --status` prints one JSON line (the kit's `status_json`: `game`,
  `version`, `files.state`, `files.where`, `files.data`, the missing files and
  a note) and exits 0. It makes and writes nothing (it works out the player's
  folder from the environment, as SDL would name it) and opens no window.
  With another version's `BTECH.EXE`, the note says which versions the
  rebuild needs.
- `chr --setup` opens the setup screen whatever is found, plays nothing, and
  exits 0 (ready), 1 (cancelled) or 2 (failed, with `{"error": ...}` on
  stdout).
- `chr --setup` followed by `--describe`, `--search`, `--look <pick>` or
  `--install <pick> [--folder <dir>]` is the setup for the launcher's own
  screens (the kit's `rekit/sdl/setup_serve.h`; `--status` says `"setup": 1`).
  See "The setup served to the launcher".
- `--status` reports the program's version as `CHR_VERSION` (CMake; `dev`
  unless a package sets it).

## The setup served to the launcher

An ask opens no window and no picker. It answers on stdout with JSON lines and
nothing else, the last one with `done`, and exits 0 (ready), 1 (cancelled) or 2
(failed). `serve_game_setup` (`platform/sdl_setup.cpp`) answers with the same
recipe as the setup screen (`kit_game`), so what is found, the words for it,
the sentence for a pick that can't be used and the install are the screen's.

- The folder a served install went to is remembered in `settings.ini`, as after
  the screen. So is a looked-at folder that is played where it is.
- The player's folder is the one a start uses: `--data-dir`, else
  `CHR_DATA_DIR`, else the system's. An install writes there by design.
  Only a test's hook (`CHR_SETUP_PICK`, `REKIT_SETUP_GO`) without
  `CHR_DATA_DIR` is refused, as everywhere.
- `--folder` names the game's own folder: the kit marks it, refuses one that
  holds other things or is another game's, and installs to its `install`. An
  `install` without our marker is never replaced.
- A `cancel` line on stdin stops a look or an install at its next progress
  step. Looking for the Amiga's disk has no such steps, so a stop during it
  takes effect when it ends. What was copied is removed; a folder named with
  `--folder` keeps the kit's mark.
- Wrong arguments get one `done` line saying what is wrong, and exit 2. (The
  kit passes over arguments it doesn't know, and would install to the default
  folder for a `--folder` without its folder.) Anything else after `--setup`
  is the usage and exit 2, as without the served setup.
- stdout goes to `chi.log` only for a start with no arguments and no terminal,
  so never for an ask. Errors besides the last line go to stderr.
- A launcher that closes the pipe doesn't stop an install: `SIGPIPE` is
  ignored.

A copy of a folder is staged in `install.new`, which has our marker from the
start, so a copy that was killed is cleared up by the next install.

## The checks

- `setup_test` (`tests/setup_test.cpp`) runs the recipe on folders it makes:
  the folder move (each order, a failed move), `settings.ini`, the verdicts
  (another game's `BTECH.EXE`, another version's copy), `--status` writing
  nothing, and installs refusing a folder that isn't ours. Given the game
  folder, it also installs from a folder and from a zip built in the work
  folder, where an entry that climbs out of the game's folder is left out.
  `CHR_AMIGA_COPY`, when set, names an Amiga copy (an .adf, or a folder or
  archive with one) for the Amiga's parts.
- `status_setup` (`tests/status_setup_test.py`) runs the built program with a
  made-up home, its own player folders and SDL's dummy drivers. It covers
  `--status`, `--setup` hidden and through the kit's screen (driven by its
  `REKIT_SETUP_*` hooks), `CHR_SETUP_PICK` (a test's stand-in for the
  player's pick), a start playing from the remembered folder, another
  version's `--status`, and the screen's CHANGE FOLDER with its
  `game=chi` mark. With `--amiga` or `CHR_AMIGA_COPY`, it also installs that
  copy, imports it and draws a frame from it.

- `tools/package/served_setup.py` asks a built or packaged program through
  pipes on stdin and stdout, with made-up copies and a video driver that
  doesn't exist (a window would be a failure): each ask, wrong arguments, a
  cancel, a closed pipe, a kill in mid copy, `--folder`. `status_setup` and
  every package's smoke test run it.

**Running the setup by hand, or in a new test.** The setup's test hooks copy a
game into the player's folder, so a run that uses one must name a folder of its
own in `CHR_DATA_DIR`. That is how `status_setup` and the package smoke test
(tools/build_package.py) get theirs. Setting only `HOME` is not enough: on
macOS, SDL asks the system for the per-user folder, not `HOME`, so the run
would write to the real player's. A safe run:

    CHR_DATA_DIR=<an empty folder> CHR_HIDDEN=1 SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
        CHR_SETUP_PICK=<a copy> chr --setup

With `CHR_SETUP_PICK` or `REKIT_SETUP_GO` set and `CHR_DATA_DIR` not, the
program refuses and makes nothing (exit 2 from `--setup`, 1 from a start).

Both checks remove their work folders. They print names and counts only.
