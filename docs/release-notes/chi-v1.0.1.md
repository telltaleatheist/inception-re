**Inception Rebuild** is a fan rebuild of BattleTech: The Crescent Hawk's
Inception (Infocom, 1988, written by Westwood Associates) for today's Windows,
macOS and Linux. It is new code written to play as the DOS original did, not an
emulator and not a patched copy. It is unofficial and free, and not affiliated
with or endorsed by any holder of rights in the game.

**Please update if you have 0.1.0 or 1.0.0.** Both can change a saved game
you loaded and later saved to the same slot.

## Fixed since 1.0.0

- **Saved games:** a game loaded and later saved to the same slot is no longer
  changed by the next menu or question.
- **Saved games:** if a save fails half way (a full disk, for instance), the
  one before stays. The original loses both; this is a difference from it, on
  purpose. An empty save file is now
  refused like any other file that isn't a saved game.
- **Menus and questions answer at once**, however long you have been playing.
  They took longer the longer you had played. A very long fight or a long stay
  in one place can still build up some delay; it ends with the visit.
- **Windows:** the first start no longer closes because of a file or folder
  nearby with an emoji or foreign letters in its name.
- **Windows 10 (1903) and later:** accented and non-English letters work in your
  user name, in the game's folder and in your copy's folder. A folder that 1.0.0
  remembered in its own way is still found.
- **Windows:** if 1.0.0 put saved games under a garbled folder name, the first
  start says where they are. Nothing is moved: copy `Game1` to `Game6` yourself.
- **A start that can't go on because of a folder's name** now says why in a
  window instead of closing silently.
- **The picture fills the window at any size**, as far as the original's shape
  allows, at your display's own resolution. Nothing is smoothed. The window keeps
  its shape while you resize it, where the system allows it. At sizes that aren't a
  whole multiple, some screen pixels are one display pixel wider than others.
- **A 'Mech's stats screen** keeps changing its colours while it waits for a key,
  as the original's does.
- **If the graphics driver restarts**, the picture should come back. A frame that
  can't be drawn is skipped and the game goes on.
- The waits for sounds and for the title are counted in milliseconds. They were
  rounded up to the next pass of the loop. The beginning of `chi.log` is no
  longer overwritten at the end.

Your saved games and settings from 0.1.0 and 1.0.0 carry on as they are.

## Bring your own copy of the game

None of the game's files are in these downloads. You need your own copy of
the DOS version 1.03 or the Amiga version 2.3. The first start looks for it on
your computer, or asks you to pick its folder, its disk image or the archive it
came in, then copies it into the rebuild's own folder. Your copy is never
changed.

## Downloads

- **Windows (64-bit):** `CHI-Rebuild-chi-v1.0.1-Setup.exe` installs it with a
  Start-menu shortcut. `CHI-Rebuild-chi-v1.0.1-win64.zip` is the same program
  to unpack anywhere and run `chi.exe`. Neither is signed, so Windows may warn
  first: choose "More info", then "Run anyway".
- **macOS 12 or later (Apple silicon and Intel):** `CHI-Rebuild-chi-v1.0.1-mac.dmg`.
  Open it and drag Inception Rebuild to Applications.
- **Linux (x86-64):** `CHI-Rebuild-chi-v1.0.1-x86_64.AppImage`: make it
  executable and run it. If it doesn't start, your system may lack FUSE 2
  (the `libfuse2` package); `CHI-Rebuild-chi-v1.0.1-linux-x86_64.tar.gz` is
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
- A long sitting with the new saving and the faster menus: scripted runs only.
- An Amiga copy's sounds. In a recorded test fight they come at the moments
  the DOS version's do, and they are louder. Please report anything that looks
  or sounds wrong.
- The window at odd sizes, in full screen, tiled, on a high-density display,
  and on Wayland and X11.
- The graphics driver restarting under the game, and the new message windows.
- Setting the game up on the launcher's screens.
- The Windows and Linux packages, which are built and smoke-tested
  automatically, and the folder-name fixes on a computer where 1.0.0 really
  ran. A Windows whose own language is Japanese, Chinese or Korean hasn't been
  tried.

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

- If the graphics device can't be brought back, the picture stops and the game
  must be started again.
- A game saved inside the cache comes back without its 'Mechs. This is the
  original's bug, kept. Save before you go in.
- Commodore 64 and Apple II copies can't be played: their towns are their own.
- Three weapon sounds of the DOS version, and two moments at the start of its
  demo, haven't been compared with the original.
