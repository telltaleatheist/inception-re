**Inception Rebuild** is a fan rebuild of BattleTech: The Crescent Hawk's
Inception (Infocom, 1988, written by Westwood Associates) for today's Windows,
macOS and Linux. It is new code written to play as the DOS original did, not an
emulator and not a patched copy. It is unofficial and free, and not affiliated
with or endorsed by any holder of rights in the game.

## New since 0.1.0

- **The Amiga version 2.3 plays.** Pick your Amiga disk image (`.adf`), or the
  zip it came in, even a zip full of zips. You get the Amiga's own pictures,
  colours and recorded sounds. The game itself is the same one: the rules,
  texts, maps and timing are the DOS version's whichever copy you play from.
- **Several Amiga disks in one download** are fine. If they differ, the setup
  takes the unchanged one and says so.
- **Commodore 64 and Apple II copies are named**, with the reason they can't
  be played here, instead of "no copy found".
- **The launcher can set up the game on its own screens**, if your launcher
  has that. The rebuild's own setup screen works as before.
- **The launcher can tell which version a downloaded program file is.**
- The title screen waits exactly as long as the original's does before its
  demo. A copy without its tune's file used to start the demo at once.
- The setup's search passes over files whose names Windows can't turn into
  text, instead of stopping.
- A setup that was stopped in the middle of copying a folder no longer stands
  in the way of the next one.
- Two more stretches of play were compared with the DOS original, and matched:
  talking to people outdoors for their rumours, and a fight whose messages wait
  for a key.

Your saved games and settings from 0.1.0 carry on as they are.

## Bring your own copy of the game

None of the game's files are in these downloads. You need your own copy of
the DOS version 1.03 or the Amiga version 2.3. The first start looks for it on
your computer, or asks you to pick its folder, its disk image or the archive it
came in, then copies it into the rebuild's own folder. Your copy is never
changed.

## Downloads

- **Windows (64-bit):** `CHI-Rebuild-chi-v1.0.0-Setup.exe` installs it with a
  Start-menu shortcut. `CHI-Rebuild-chi-v1.0.0-win64.zip` is the same program
  to unpack anywhere and run `chi.exe`. Neither is signed, so Windows may warn
  first: choose "More info", then "Run anyway".
- **macOS 12 or later (Apple silicon and Intel):** `CHI-Rebuild-chi-v1.0.0-mac.dmg`.
  Open it and drag Inception Rebuild to Applications.
- **Linux (x86-64):** `CHI-Rebuild-chi-v1.0.0-x86_64.AppImage`: make it
  executable and run it. If it doesn't start, your system may lack FUSE 2
  (the `libfuse2` package); `CHI-Rebuild-chi-v1.0.0-linux-x86_64.tar.gz` is
  the same program to unpack and run as `./chi`.
- **`SHA256SUMS`** lists every file's SHA-256.

## The first start on a Mac

The app isn't notarized, so macOS stops it the first time. Right-click (or
Control-click) Inception Rebuild in Applications and choose Open, then Open
again. On macOS 15 or later, if that offers no Open button, open System
Settings, Privacy & Security, and choose "Open Anyway" for Inception
Rebuild. After that it starts normally.

## Not yet played or heard by a person

Automated tests check these, but nobody has gone through them yet:

- The whole story, from start to end. Parts of the DOS version have been
  played. An Amiga copy has been started and looked at, not played through.
- An Amiga copy's sounds. In a recorded test fight they come at the moments
  the DOS version's do, and they are louder. Please report anything that looks
  or sounds wrong.
- Setting the game up on the launcher's screens.
- The Windows and Linux packages, which are built and smoke-tested
  automatically.

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

## Other known limits

- A game saved inside the cache comes back without its 'Mechs. This is the
  original's bug, kept. Save before you go in.
- Commodore 64 and Apple II copies can't be played: their towns are their own.
- Three weapon sounds of the DOS version, and two moments at the start of its
  demo, haven't been compared with the original.
