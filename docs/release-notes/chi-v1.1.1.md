**Inception Rebuild** is a fan rebuild of BattleTech: The Crescent Hawk's
Inception (Infocom, 1988, written by Westwood Associates) for today's Windows,
macOS and Linux. It is new code written to play as the DOS original did, not an
emulator and not a patched copy. It is unofficial and free, and not affiliated
with or endorsed by any holder of rights in the game.

This version changes only the Linux AppImage (below). Everything else is as in
1.1.0, which added a menu over the game and changed how the game is set up. The
game itself plays as in 1.0.1.

## New in 1.1.1

- **The Linux AppImage now carries its runtime's licence notices, and a pinned
  runtime.** An AppImage starts with a small separate program, the AppImage
  runtime, which has libfuse and other libraries built in. The AppImage now
  includes their licences and says where their source is (in
  `usr/share/doc/chi/licenses/appimage-runtime`), and the runtime is a fixed,
  checked release instead of whichever was newest when the package was built.
  Nothing else changed: the game, the Windows and Mac packages and the
  tarball are as in 1.1.0.

## New in 1.1.0 (since 1.0.1)

- **A menu over the game.** Press Shift+Alt+F1 (Cmd+Shift+F1 on a Mac) at any
  moment. The game waits while it is open, and Esc takes you back to it. For
  the first few starts a small note in the corner says so. In the menu:
  - **Game files**: the copy of the game in use. CHANGE sets the game up again
    with another copy and starts the game again with it; if you cancel, the
    game starts again with the copy it had.
  - **Updates**: whether a newer version is out, checked against the
    BattleTech launcher's signed list. UPDATE NOW downloads it, checks it
    against the release's signed list of files, copies your saved games and
    settings to a Backups folder first, puts the new version in place and
    starts it. A start checks once a day by itself, unless you turn that off;
    the launcher does its own checking, so a game it starts never asks.
  - **Check my files**: the program's files and the game's, each checked
    against the record made when they were put in place, naming any that are
    missing or changed.
  - **Report a problem**: the page to report it on, and the log to attach
    (open it, or copy its path).
  - **Open my data folder**: the folder that holds your saves, settings and
    log.
  On Windows, if your keyboard-layout hotkey Alt+Shift is turned on, the
  menu's keys may also switch the layout.
- **The first start sets itself up.** A copy of the game found on your
  computer is checked and copied in at once, in a small window, with no
  question; a copy whose version it can't tell is shown to you first. With no
  copy found, the download of the original DOS files starts at once in the same
  small window: from the BattleTech launcher's signed list, only the exact file
  the list names, and going on from where it stopped if it breaks off. CANCEL
  and USE MY OWN COPY INSTEAD are there all along. A failure or a cancel opens
  the setup window, where you pick a copy yourself or start the download again.
- **The BattleTech launcher can list every copy of the game on your computer**
  and show which one the game plays.
- **The Mac app is signed and notarized.** It opens like any other app: no
  right-click, Open, and no trip to Privacy & Security.

Your saved games and settings from 0.1.0, 1.0.0 and 1.0.1 carry on as they are.

## Bring your own copy of the game

None of the game's files are in these downloads. The game needs the DOS version
1.03 or the Amiga version 2.3. The first start looks for a copy on your computer
and copies it into the rebuild's own folder. If none is found, it downloads the
DOS files (above). You can instead pick your own copy: its folder, its disk
image or the archive it came in. Your copy is never changed.

## Downloads

- **Windows (64-bit):** `CHI-Rebuild-chi-v1.1.1-Setup.exe` installs it with a
  Start-menu shortcut. `CHI-Rebuild-chi-v1.1.1-win64.zip` is the same program
  to unpack anywhere and run `chi.exe`. Neither is signed, so Windows may warn
  first: choose "More info", then "Run anyway".
- **macOS 12 or later (Apple silicon and Intel):** `CHI-Rebuild-chi-v1.1.1-mac.dmg`.
  Open it and drag Inception Rebuild to Applications.
- **Linux (x86-64):** `CHI-Rebuild-chi-v1.1.1-x86_64.AppImage`: make it
  executable and run it. If it doesn't start, your system may lack FUSE 2
  (the `libfuse2` package); `CHI-Rebuild-chi-v1.1.1-linux-x86_64.tar.gz` is
  the same program to unpack and run as `./chi`.
- **`SHA256SUMS`** lists every file's SHA-256.

## Not yet played or heard by a person

Automated tests check these, but nobody has gone through them yet:

- The whole story, from start to end. Parts of the DOS version have been
  played. An Amiga copy has been started and looked at, not played through.
- The first start's download of the original files. It has been tested with
  the launcher's own file and with a test list, not yet by a player.
- Setting the game up on the launcher's screens, and its list of copies.
- The menu over the game on a real screen, and its keys on real keyboards. It
  was tested on hidden windows, with its keys sent by the tests.
- Updating from the menu. The setup kit's own tests run its download, backup
  and swap with test releases; no real release has been updated to yet, by a
  player or a test.
- A long sitting with the saving and the faster menus of 1.0.1: scripted runs
  only.
- An Amiga copy's sounds. In a recorded test fight they come at the moments
  the DOS version's do, and they are louder. Please report anything that looks
  or sounds wrong.
- The window at odd sizes, in full screen, tiled, on a high-density display,
  and on Wayland and X11.
- The graphics driver restarting under the game, and the message windows.
- The Windows and Linux packages, which are built and smoke-tested
  automatically. A Windows whose own language is Japanese, Chinese or Korean
  hasn't been tried.

## Known differences with an Amiga copy

- The letters are the rebuild's own: no font was found in the Amiga game's files.
- Inside the cache the Amiga colours the whole screen differently. Here only
  the cache's map tiles have those colours; the frame, the text and the
  figures keep the usual ones.
- The menus' highlight bar and the boxes drawn over the map may have other
  colours than on an Amiga.
- Two sounds that the Amiga plays higher or lower at times are always played
  the same, and the sounds during the pictures of places aren't played.
- The title is silent: no title music was found in the Amiga version's files.
- Waits after sounds are the DOS version's.
- Saved games of the Amiga original aren't copied. They have another form, so
  don't put them where the rebuild's saves are.
- The download the first start makes is the DOS version.

## Other known limits

- If the graphics device can't be brought back, the picture stops and the game
  must be started again.
- A game saved inside the cache comes back without its 'Mechs. This is the
  original's bug, kept. Save before you go in.
- Commodore 64 and Apple II copies can't be played: their towns are their own.
- Three weapon sounds of the DOS version, and two moments at the start of its
  demo, haven't been compared with the original.
