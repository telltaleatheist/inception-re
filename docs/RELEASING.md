# Building a package

A release is made only when the project owner clears it, and it is made by
hand: nothing in this repository tags or publishes on its own.
`tools/build_package.py` builds and checks a package on the machine it runs
on, and the package check workflow runs it on all three platforms. Releases
go to this repository's GitHub Releases page only, with `SHA256SUMS`
(DEPLOY.md has the family's repos). "Making a release", below, has the
steps.

## What a package holds

- The program, `chi`: `chi.exe` in the Windows zip, `Contents/MacOS/chi` in
  the Mac app, `usr/bin/chi` in the AppImage. The build's target and file
  stay `chr`, which the tools, the tests and the reference rig run; the
  package gives it its shipped name.
- SDL3 as a shared library, built from source: 3.4.12, pinned by commit
  `f87239e71e42da91ca317a12eefb82cfbf3393eb`. That is the version the
  builds and checks here run on, and the renderer needs 3.4
  (`SDL_SetDefaultTextureScaleMode`). libarchive 3.7.9, zlib 1.3.1
  and liblzma (xz 5.8.1) are linked in statically, built from their release
  sources, each pinned by its sha256.
- `data/text/interface.json` (our interface texts, docs/text.md) and
  `data/font/font.txt` (our own font, docs/formats/font.md), beside the
  program or in the Mac app's `Contents/Resources`.
- `LICENSE`, a `README.txt`, and `licenses/`: SDL's, re-setup-kit's,
  stb's, miniz's, libarchive's, zlib's and xz's.

No file of the original game is ever packaged: no program, data file, save or
imported text. `tools/package/check_paths.py` refuses a package holding any
file of the game by name, and any local path or user name in any file. The
first start finds or asks for the player's copy (docs/setup.md), copies it
into the player's folder, and imports the text and tables from its BTECH.EXE
there.

The packages carry the project's own icon, a hawk's head in a crescent on a
steel and crimson patch: `tools/package/chi.png` (the AppImage's desktop
entry), `chi.ico` (chi.exe's resource and Setup.exe) and `chi.icns` (the
Mac app's `CFBundleIconFile`). It holds nothing from the game; where it came
from is in `tools/package/ICONS.md`.

## Names

A release is tagged `chi-vX.Y.Z`. That tag is the label, and `--status`
reports its number as the program's version (`chi-v1.0.0` reports `1.0.0`).
A test build takes any other label with `-test` in it, so it is never taken
for a release.

The program file also carries the full label for the family launcher, whose
ADD FROM FILE reads it without running the program: the NUL-ended string
`MW3-PROGRAM-VERSION:<label>` (`chi_version_tag` in src/main.cpp, from the
`CHR_VERSION_TAG` CMake setting, "dev" by default). Every package checks its
program for it, the Mac app in each architecture's slice.

| Platform | Made on | Files |
|---|---|---|
| macOS | a Mac | `CHI-Rebuild-<label>-mac.dmg`: `Inception Rebuild.app`, signed ad hoc, universal (arm64 and x86_64) from macOS 12 |
| Windows x64 | Windows, MSVC | `CHI-Rebuild-<label>-win64.zip` (`chi.exe`, `SDL3.dll` and the MSVC runtime at the top level), and with `--installer` `CHI-Rebuild-<label>-Setup.exe` |
| Linux x86-64 | Linux or WSL | `CHI-Rebuild-<label>-linux-x86_64.tar.gz` (the checks' form), and with `--installer` `CHI-Rebuild-<label>-x86_64.AppImage` |

Each file gets a `.sha256` beside it. For a release, put every platform's
files in one folder and run `python3 -I tools/package/sums.py <folder>`.
That writes one `SHA256SUMS` over them, which is what the launcher's updater
reads.

## Building one

Once per machine, the dependencies' sources:

    tools/deps/build_archive.sh mac        # or linux; Windows: tools\windows\build_deps_archive.cmd
    git clone https://github.com/libsdl-org/SDL.git build-deps/SDL
    git -C build-deps/SDL checkout f87239e71e42da91ca317a12eefb82cfbf3393eb

Then:

    python3 -I tools/build_package.py --sdl build-deps/SDL --label LABEL [--game GAME] [--this-arch] [--installer] [--appimage-tools DIR]

The package's build lives in `build-package/` and its files in `dist/`, and
both are ignored by git. It is configured with `CHR_GAME_DIR` empty, so it
has no built-in game folder. It also maps the source and build folders out
of what the compiler writes (`CHR_TRIM_PATHS`).

- **macOS**: the release form is universal, arm64 and x86_64, from macOS 12:
  SDL and the program are built for both (`CMAKE_OSX_ARCHITECTURES`), the
  static archive libraries are too (`build_archive.sh mac`), and `lipo`
  checks every binary in the app for both. The x86_64 half is started under
  Rosetta when the Mac has it. `--this-arch` builds this Mac's architecture
  alone, for a quicker local test. SDL goes to `Contents/Frameworks`, named `@rpath`. The library,
  the program and the app are signed ad hoc with
  `tools/package/chi.entitlements` and checked with
  `codesign --verify --deep --strict`. The dmg holds the app, a link to
  Applications and the read-me. The app is not signed with a developer
  identity, so the first start needs right-click, Open. Its bundle id is
  `io.github.inceptionre.chi`.
- **Windows**: the program is linked as a GUI program (`CHR_WINDOWS_GUI`),
  so no console window opens. `--installer` runs Inno Setup's `iscc` on
  `tools/package/installer.iss`. It installs to
  `Program Files\Inception Rebuild` (or "only for me"), with Start-menu and
  desktop shortcuts. Its uninstaller leaves the player's folder alone. The
  AppId is this project's own and stays the same in every version, so a new
  Setup upgrades in place.
- **Linux**: every run makes `CHI-Rebuild-<label>-linux-x86_64.tar.gz`,
  the checks' form. It holds `chi` with `libSDL3.so.0` beside it (found
  through `$ORIGIN`, which `ldd` confirms on the unpacked copy) and the rest
  of the zip's layout, and is smoke-tested from the unpacked archive. The
  release form is the AppImage, made with `--installer`.
  `--appimage-tools DIR` names the folder holding
  `linuxdeploy-x86_64.AppImage` and `appimagetool-x86_64.AppImage`. The
  AppImage holds `usr/bin/chi` with `data/` beside it, and the notices in
  `usr/share/doc/chi`. Its desktop entry (`tools/package/chi.desktop`)
  names the icon, `tools/package/chi.png`.

A started program with nothing on its command line (a shortcut, the
launcher, a double-click) has no console. Its log goes to `chi.log` in the
player's folder.

## The smoke test

Packaging ends by starting the packaged program, hidden: SDL's dummy drivers (a
video driver that doesn't exist for the served setup), `CHR_HIDDEN` (a background app on macOS) and a made-up home. On Windows, the
program is started from the extracted zip.

- `--help` must print its usage, and `--status` its JSON line for
  `inception`.
- The setup served to the launcher answers through pipes, with made-up copies
  and its own player folder (`tools/package/served_setup.py`,
  docs/setup.md).
- With a game folder (`--game DIR`, or `CHR_GAME_DIR`), `--setup` must copy
  the game from it into an empty player folder (`CHR_SETUP_PICK`). A start
  must then import the game's text and draw a frame. The game folder is only
  read and is never packaged.

To run the same checks on a package already unpacked (its folder, the app's
folder, or an AppImage):

    python3 -I tools/build_package.py --smoke DIR --game GAME

The packaging's unit tests (no build, no game files):

    python3 -I -m unittest discover -s tools -p 'test_build_package.py' -v

## The package check workflow

`.github/workflows/package-check.yml` ("package check") runs the packaging on
GitHub's runners, one job per platform: Linux (Ubuntu 24.04, the tarball
and the AppImage, with linuxdeploy and appimagetool at pinned releases
checked by SHA-256, the AppImage started without FUSE),
Windows (Windows Server 2022, MSVC: the zip and Setup.exe) and macOS (the
universal dmg). It runs by hand (workflow_dispatch), and on pushes to main
that touch `src/`, `tools/`, `CMakeLists.txt`, `extern/` or the workflow.
Each job runs the unit tests, builds the static archive libraries from their
pinned sources, then packages and smoke-tests with SDL at its pinned commit.
The jobs have no game folder, so their smoke test is `--help` and
`--status`. They upload their files as workflow artifacts
(`package-check-linux`, `-windows`, `-mac`, kept 7 days) and nothing else:
no tag, no release, no secret beyond the default read-only token. Their
label is the run's number with `-test`. A run started by hand may be given
one (`label`), which must be a release tag, `chi-vX.Y.Z`, or end in `-test`,
or every job stops at once. A labelled run has its own concurrency group, so
a push to main doesn't cancel it.

## Making a release

1. Choose the version, `chi-vX.Y.Z`, and the commit on main to release.
   Write its notes in `docs/release-notes/chi-vX.Y.Z.md`.
2. Run the package check by hand on main with the tag as its label:

       gh workflow run "package check" --ref main -f label=chi-vX.Y.Z

   and wait until all three jobs pass. Note the commit it ran on.
3. Download its three artifacts into one empty folder
   (`gh run download <run id>`), and check each file against its `.sha256`.
4. Write `SHA256SUMS` over the packages:
   `python3 -I tools/package/sums.py <folder>`.
5. Tag the commit the run built `chi-vX.Y.Z`, and push the tag.
6. Publish the release:

       gh release create chi-vX.Y.Z --verify-tag --title "Inception Rebuild X.Y.Z" \
         --notes-file docs/release-notes/chi-vX.Y.Z.md <every package> SHA256SUMS

   Not a draft and not a prerelease: the launcher takes only a published,
   full release, and installs nothing without the package's line in
   `SHA256SUMS` (DEPLOY.md, "How updates work").
7. Download every asset again from the release and check it against
   `SHA256SUMS`.

## Before calling a package validated

The smoke test only shows that the package loads, sets the game up, imports
and draws. Before a package goes to anyone, check by hand on each platform:
the window, the setup screen, a new game through the town, a fight and its
return, a save and a load, input and sound.
