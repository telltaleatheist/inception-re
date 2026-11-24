The first release of **Inception Rebuild**, a fan rebuild of BattleTech: The
Crescent Hawk's Inception (Infocom, 1988, written by Westwood Associates) for
today's Windows, macOS and Linux. It is new code written to play as the DOS
original did, not an emulator and not a patched copy. It is unofficial and
free, and not affiliated with or endorsed by any holder of rights in the game.

This is an early release. The rebuild has been checked against the original
game, but more playtesting is still to come.

## Bring your own copy of the game

None of the game's files are in these downloads. You need your own copy of
the DOS version, 1.03. The first start looks for it on your computer, or asks
you to pick its folder or the archive it came in, then copies it into the
rebuild's own folder. Your copy is never changed.

## Downloads

- **Windows (64-bit):** `CHI-Rebuild-chi-v0.1.0-Setup.exe` installs it with a
  Start-menu shortcut. `CHI-Rebuild-chi-v0.1.0-win64.zip` is the same program
  to unpack anywhere and run `chi.exe`. Neither is signed, so Windows may warn
  first: choose "More info", then "Run anyway".
- **macOS 12 or later (Apple silicon and Intel):** `CHI-Rebuild-chi-v0.1.0-mac.dmg`.
  Open it and drag Inception Rebuild to Applications.
- **Linux (x86-64):** `CHI-Rebuild-chi-v0.1.0-x86_64.AppImage`: make it
  executable and run it. If it doesn't start, your system may lack FUSE 2
  (the `libfuse2` package); `CHI-Rebuild-chi-v0.1.0-linux-x86_64.tar.gz` is
  the same program to unpack and run as `./chi`.
- **`SHA256SUMS`** lists every file's SHA-256.

## The first start on a Mac

The app isn't notarized, so macOS stops it the first time. Right-click (or
Control-click) Inception Rebuild in Applications and choose Open, then Open
again. On macOS 15 or later, if that offers no Open button, open System
Settings, Privacy & Security, and choose "Open Anyway" for Inception
Rebuild. After that it starts normally.

## Known limits

- A few remaining comparison checks against the original game are still to
  be run; they come in a later update.
- The Windows and Linux packages are built and smoke-tested automatically,
  but haven't yet been played through by hand on those systems.
