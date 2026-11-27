"""Build and smoke-test the rebuild's packages, with no file of the original game in them.

    build_package.py --sdl SDL_SOURCE --label LABEL [--game GAME_DIR] [--this-arch] [--installer] [--appimage-tools DIR]
    build_package.py --smoke PACKAGE_DIR [--game GAME_DIR]

One platform per run, this machine's (docs/RELEASING.md):
  macOS    CHI-Rebuild-<label>-mac.dmg            Inception Rebuild.app, Contents/MacOS/chi: universal
                                                  (arm64, x86_64), or this Mac's alone with --this-arch
  Windows  CHI-Rebuild-<label>-win64.zip          chi.exe and SDL3.dll at the top level
           CHI-Rebuild-<label>-Setup.exe          with --installer (Inno Setup)
  Linux    CHI-Rebuild-<label>-linux-x86_64.tar.gz   chi and libSDL3.so.0 (the checks' form)
           CHI-Rebuild-<label>-x86_64.AppImage    with --installer: usr/bin/chi (--appimage-tools)
The build's program stays chr, which the tools and the reference rig run; the
package names it chi. Every file made gets a .sha256 beside it; sums.py writes
a release's SHA256SUMS over all of them.

The saves' tests that need no game folder (savetests) are built and run with the
program, and must pass; they are in no package.

The smoke test starts the packaged program: --help must print its usage and
--status its JSON line, and the setup served to the launcher must answer
through pipes (tools/package/served_setup.py). With a game folder (--game, or CHR_GAME_DIR) it also
sets the game up from it (--setup with CHR_SETUP_PICK) into an empty player
folder, and a start must import the game's text and draw a frame. Every run is
hidden: SDL's dummy drivers (the served setup's: none at all), a background app
on macOS, a made-up home. The
game folder is only read and never packaged.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent / "package"))
import served_setup  # noqa: E402

STEM = "CHI-Rebuild"
APP_NAME = "Inception Rebuild"
PROGRAM = "chi"   # the shipped program; the build's target stays chr
TARGET = "chr"
SAVE_TESTS = "savetests"   # run for every package, in none
BUNDLE_ID = "io.github.inceptionre.chi"  # its own, so it never shares another game's settings
TAG = re.compile(r"chi-v(\d+\.\d+\.\d+)")
README = (
    "Inception Rebuild\n\n"
    "A rebuild of BattleTech: The Crescent Hawk's Inception (1988). Your own copy of the game\n"
    "is required and is not included: no game files are in this package. It plays the DOS\n"
    "version 1.03 and the Amiga version 2.3.\n\n"
    "The first time it starts without your game, it asks for your copy: the DOS game's\n"
    "BTECH.EXE, any file in its folder, the Amiga game's disk image (.adf), or the archive it\n"
    "came in (a .zip, for example), or the launcher asks for it on its own screens. It copies\n"
    "the game into your own folder, or the one you choose, and plays from there; your copy is\n"
    "never changed. Your settings (settings.ini), saves, the text and tables read once from\n"
    "your copy, the copied game and the log (chi.log) live in your own folder:\n"
    "  Windows: %APPDATA%\\BattleTech\\Inception Rebuild\n"
    "  macOS:   ~/Library/Application Support/BattleTech/Inception Rebuild\n"
    "  Linux:   ~/.local/share/BattleTech/Inception Rebuild\n\n"
    "From a command line: chi --game-dir <game folder> starts on that folder; chi --setup\n"
    "sets the game up again; chi --help lists the rest.\n\n"
    "BattleTech: The Crescent Hawk's Inception was written by Westwood Associates and\n"
    "published by Infocom. This rebuild is a fan project, not affiliated with any of them;\n"
    "its source code is under the MIT licence (LICENSE), which covers our code only, never\n"
    "the game.\n")
SMOKE_SECONDS = 120
# The Mac release form: both architectures, from macOS 12 (as mech3-re's).
MAC_ARCHS = ("arm64", "x86_64")


# re-setup-kit's Mac signing (its README, Mac signing): a Developer ID, the hardened runtime and
# notarization. Who signs comes from the environment alone (RE_SIGN_IDENTITY, RE_NOTARY_PROFILE),
# never from here; without an identity the package is signed ad hoc, as a check build is.
SIGNER = "extern/re-setup-kit/tools/macos/sign_notarize.sh"


def developer_id():
    """Whether a Developer ID signs this package: RE_SIGN_IDENTITY (or MW3_SIGN_IDENTITY, which the
    kit reads when it is unset) names one."""
    return bool(os.environ.get("RE_SIGN_IDENTITY") or os.environ.get("MW3_SIGN_IDENTITY"))


def sign_app(root, app, frameworks, exe, entitlements):
    """The app's signature. With a Developer ID, the kit's `app` alone: every library, the program,
    then the app, with the hardened runtime and a timestamp (it replaces any signature they had).
    Without one, ad hoc (no name in the signature). Then checked either way."""
    if developer_id():
        run(str(root / SIGNER), "app", str(app), "--entitlements", entitlements)
    else:
        for lib in sorted(frameworks.iterdir()):
            run("codesign", "--force", "--sign", "-", str(lib))
        run("codesign", "--force", "--sign", "-", "--entitlements", entitlements, str(exe))
        run("codesign", "--force", "--sign", "-", "--entitlements", entitlements, str(app))
    run("codesign", "--verify", "--deep", "--strict", str(app))


def notarize_dmg(root, dmg, app):
    """With a Developer ID, the kit's `dmg`: the dmg signed, notarized (refused unless Apple accepts
    it), its ticket stapled to it and to the app, and Gatekeeper's verdict checked. Without one,
    nothing: a check build's dmg isn't signed."""
    if developer_id():
        run(str(root / SIGNER), "dmg", str(dmg), str(app))


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def platform_name():
    if os.name == "nt":
        return "win64"
    if sys.platform == "darwin":
        return "mac"
    return "x86_64"


def program_version(label):
    """The version --status reports: a release tag's X.Y.Z (chi-v1.0.0 -> 1.0.0), else the label."""
    found = TAG.fullmatch(label)
    return found.group(1) if found else label


def configure_and_build(root, build, sdl, label, universal=True):
    # CHR_GAME_DIR empty: a package has no built-in game folder (docs/setup.md).
    args = ["cmake", "-S", str(root), "-B", str(build), f"-DCHR_SDL_SOURCE_DIR={sdl}", "-DCHR_TRIM_PATHS=ON",
            f"-DCHR_VERSION={program_version(label)}", f"-DCHR_VERSION_TAG={label}", "-DCHR_GAME_DIR="]
    if os.name == "nt":
        args += ["-A", "x64", "-DCHR_WINDOWS_GUI=ON"]
    elif sys.platform == "darwin":
        args += ["-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0",
                 "-DCMAKE_OSX_ARCHITECTURES=" + (";".join(MAC_ARCHS) if universal else "")]
    else:
        args += ["-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_BUILD_RPATH=$ORIGIN", "-DCMAKE_BUILD_RPATH_USE_ORIGIN=ON"]
    run(*args)
    run("cmake", "--build", str(build), "--config", "Release", "--target", TARGET, SAVE_TESTS, "--parallel", "4")
    save_tests(built(build, SAVE_TESTS))
    return built(build, TARGET)


def built(build, target):
    return build / f"Release/{target}.exe" if os.name == "nt" else build / target


def save_tests(program):
    """The tests of the saves that need no game folder (tests/test_save_files.cpp), in the same build as the
    program. Any failure stops the packaging, and so does a run in which nothing was tested."""
    print("Save tests, without those that play the game (tests/test_saves.cpp: they need its folder):", flush=True)
    result = subprocess.run([str(program)], cwd=program.parent, capture_output=True, text=True, errors="replace",
                            timeout=SMOKE_SECONDS)
    print(result.stdout + result.stderr, end="", flush=True)
    found = re.findall(r"^(\d+) tests, (\d+) checks, (\d+) skipped, (\d+) failures$", result.stdout, re.MULTILINE)
    if result.returncode != 0 or len(found) != 1:
        raise RuntimeError(f"The save tests failed ({result.returncode})")
    tests, checks, skipped, failures = map(int, found[0])
    if failures or not tests or not checks:
        raise RuntimeError(f"The save tests failed: {tests} tests, {checks} checks, {failures} failures")
    print(f"Save tests: {tests} tests, {checks} checks, {skipped} skipped, all passed", flush=True)


# libarchive, zlib and liblzma, linked in statically (tools/deps/build_archive.sh and
# tools/windows/build_deps_archive.cmd put their licences in <prefix>/share/licenses).
ARCHIVE_LICENCES = {"libarchive-3.7.9/COPYING": "libarchive-COPYING", "zlib-1.3.1/LICENSE": "zlib-LICENSE",
                    "xz-5.8.1/COPYING": "xz-COPYING", "xz-5.8.1/COPYING.0BSD": "xz-COPYING.0BSD"}


def archive_licences(build, notices):
    """The archive libraries' licences into `notices`. A build without the pinned static libarchive (none,
    or a system one) is refused: its setup could not read a download, and the licences would be missing."""
    cache = build / "CMakeCache.txt"
    lib = ""
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("REKIT_LIBARCHIVE_LIB:"):
                lib = line.split("=", 1)[1].strip()
    if not lib or lib.endswith("-NOTFOUND") or not lib.endswith((".a", ".lib")):
        raise RuntimeError("Missing the static libarchive: build it first (tools/deps/build_archive.sh, "
                           "tools/windows/build_deps_archive.cmd) so the setup reads downloads")
    licences = Path(lib).parent.parent / "share/licenses"
    for source, name in ARCHIVE_LICENCES.items():
        if not (licences / source).is_file():
            raise RuntimeError(f"Missing licence: {licences / source} ({lib} is not the pinned static build)")
        shutil.copy2(licences / source, notices / name)


# The family launcher's ADD FROM FILE reads a program's release tag from this NUL-ended string in the
# file, without running it (mech3-re's mw3_version_tag); chi's is in src/main.cpp.
MARKER = b"MW3-PROGRAM-VERSION:"


def check_marker(binary, label):
    """`binary` holds the launcher's marker with this package's full label."""
    if MARKER + label.encode() + b"\0" not in Path(binary).read_bytes():
        raise RuntimeError(f"{Path(binary).name} lacks the launcher's marker {(MARKER + label.encode()).decode()}")


def check_marker_slices(binary, label, archs):
    """Each architecture of a universal Mac program holds the marker (lipo -thin, one by one)."""
    with tempfile.TemporaryDirectory(prefix="chi-slice-") as scratch:
        for arch in archs:
            thin = Path(scratch) / arch
            run("lipo", str(binary), "-thin", arch, "-output", str(thin))
            check_marker(thin, label)


# Our own data files: the interface texts and the font for a copy that has none.
OUR_DATA = ["data/text/interface.json", "data/font/font.txt"]


def stage_common(root, build, sdl, resources):
    """Our data, the licence, the read-me and the notices into `resources` (beside the program,
    or a Mac app's Contents/Resources, where SDL_GetBasePath points)."""
    for name in OUR_DATA:
        ours = root / name
        if not ours.is_file():
            raise RuntimeError(f"Missing our data: {ours}")
        (resources / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ours, resources / name)
    shutil.copy2(root / "LICENSE", resources / "LICENSE")
    notices = resources / "licenses"
    notices.mkdir(exist_ok=True)
    kit = root / "extern/re-setup-kit"
    for name, source in [("SDL-LICENSE.txt", sdl / "LICENSE.txt"), ("re-setup-kit-LICENSE", kit / "LICENSE"),
                         ("stb-LICENSE", kit / "third_party/stb/LICENSE"),
                         ("miniz-LICENSE", kit / "third_party/miniz/LICENSE")]:
        if not source.is_file():
            raise RuntimeError(f"Missing licence: {source}")
        shutil.copy2(source, notices / name)
    archive_licences(build, notices)
    (resources / "README.txt").write_text(README, encoding="utf-8", newline="\r\n" if os.name == "nt" else "\n")


def check_paths(root, folder, *extra):
    """No local path or user name in anything shipped (tools/package/check_paths.py), nor the folders
    the build ran in (`extra`: the tree and SDL's source, which __FILE__ would name)."""
    run(sys.executable, "-I", str(root / "tools/package/check_paths.py"), str(folder),
        *[str(Path(p).resolve()) for p in extra])


def write_digest(file):
    digest = hashlib.sha256(file.read_bytes()).hexdigest()
    (file.parent / (file.name + ".sha256")).write_text(f"{digest}  {file.name}\n", encoding="utf-8", newline="\n")


# --- the smoke test ---

def hidden_env(scratch, **more):
    """A run no player is at: SDL's dummy drivers, CHR_HIDDEN (a background app on macOS), a made-up home."""
    home = scratch / "home"
    home.mkdir(exist_ok=True)
    env = dict(os.environ)
    for k in [k for k in env if k.startswith(("REKIT_SETUP_", "CHR_"))]:
        env.pop(k)
    env.update(CHR_HIDDEN="1", SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy", SDL_RENDER_DRIVER="software",
               HOME=str(home), APPDATA=str(home), XDG_DATA_HOME=str(home / "share"), **more)
    return env


def started(executable, args, env, what):
    result = subprocess.run([str(executable)] + args, cwd=executable.parent, env=env, capture_output=True,
                            text=True, timeout=SMOKE_SECONDS)
    if result.returncode != 0:
        raise RuntimeError(f"The packaged program failed {what} ({result.returncode}):\n"
                           + "\n".join((result.stdout + result.stderr).splitlines()[-10:]))
    return result


def smoke(executable, game=None, **more):
    """--help, --status and the setup served to the launcher (tools/package/served_setup.py, made-up
    copies) from the packaged program; with `game`, the setup from that folder and a start.
    `more` goes into each run's environment."""
    with tempfile.TemporaryDirectory(prefix="chi-smoke-") as scratch:
        scratch = Path(scratch)
        env = hidden_env(scratch, **more)
        if f"usage: {PROGRAM}" not in started(executable, ["--help"], env, "--help").stdout:
            raise RuntimeError("The packaged program's --help has no usage line")
        status = json.loads(started(executable, ["--status"], env, "--status").stdout.strip().splitlines()[-1])
        if status.get("game") != "inception":
            raise RuntimeError(f"--status answered {status}")
        if status.get("setup") != 1:
            raise RuntimeError(f"--status doesn't say that the setup is served: {status}")
        print("Smoke: --help and --status answer")
        count = 0
        for passed, what in served_setup.checks(executable, hidden_env(scratch, **more), scratch / "served", kill=not more):
            if not passed:
                raise RuntimeError(f"The packaged program's served setup: {what}")
            count += 1
        print(f"Smoke: the setup served to the launcher, {count} checks through pipes")
        if not game:
            return
        user = scratch / "player"
        env = hidden_env(scratch, CHR_DATA_DIR=str(user), CHR_SETUP_PICK=str(Path(game).resolve()), **more)
        started(executable, ["--setup"], env, "--setup from the game folder")
        if not (user / "install").is_dir() or "game_folder=" not in (user / "settings.ini").read_text():
            raise RuntimeError("--setup copied no game into the player's folder")
        env.pop("CHR_SETUP_PICK")
        shot = scratch / "start.bmp"
        started(executable, ["--map", "1", "--script", "up", "--screenshot", str(shot)], env, "a start")
        if not (user / "import.json").is_file() or not shot.is_file():
            raise RuntimeError("A start from the set-up game imported nothing or drew no frame")
        print("Smoke: the game set up from the folder given, imported, and a frame drawn")


# --- Windows ---

def package_windows(root, build, sdl, output, label, game, installer):
    name = f"{STEM}-{label}-win64"
    stage = output / name
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    binary = configure_and_build(root, build, sdl, label)
    shutil.copy2(binary, stage / f"{PROGRAM}.exe")
    check_marker(stage / f"{PROGRAM}.exe", label)
    sdl_dll = [p for p in build.rglob("SDL3.dll") if p.is_file()]
    if not sdl_dll:
        raise RuntimeError("Missing runtime dependency: SDL3.dll")
    shutil.copy2(sdl_dll[0], stage / "SDL3.dll")
    copy_msvc_runtime(stage)
    stage_common(root, build, sdl, stage)
    check_paths(root, stage, root, sdl)
    archive = output / f"{name}.zip"
    zip_flat(stage, archive)
    extracted = output / "smoke"
    if extracted.exists():
        shutil.rmtree(extracted)
    with zipfile.ZipFile(archive) as source:
        source.extractall(extracted)
    smoke(extracted / f"{PROGRAM}.exe", game)
    write_digest(archive)
    print(f"Packaged and smoke-tested {archive}")
    if installer:
        windows_installer(root, stage, output, label)
    return archive


def zip_flat(stage, archive):
    """The staged folder's files at the zip's top level: chi.exe and SDL3.dll first, as a player unpacks them."""
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as target:
        for file in sorted(stage.rglob("*")):
            if file.is_file():
                target.write(file, file.relative_to(stage).as_posix())


def copy_msvc_runtime(stage):
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    vs = Path(subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
                                       "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property",
                                       "installationPath"], text=True).strip())
    crt = sorted((vs / "VC/Redist/MSVC").glob("*/x64/Microsoft.VC*.CRT"))
    if not crt:
        raise RuntimeError("Cannot locate the redistributable MSVC runtime")
    for dll in crt[-1].glob("*.dll"):
        shutil.copy2(dll, stage / dll.name)


def find_iscc():
    found = shutil.which("iscc")
    if found:
        return found
    for base in (os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles"),
                 os.path.expandvars(r"%USERPROFILE%\scoop\apps\inno-setup\current")):
        if base:
            for candidate in (Path(base) / "Inno Setup 6/ISCC.exe", Path(base) / "ISCC.exe"):
                if candidate.is_file():
                    return str(candidate)
    raise RuntimeError("Inno Setup's ISCC.exe not found (scoop install inno-setup, or jrsoftware.org)")


def windows_installer(root, stage, output, label):
    """CHI-Rebuild-<label>-Setup.exe from the staged folder (tools/package/installer.iss)."""
    stem = f"{STEM}-{label}"
    run(find_iscc(), "/Qp", f"/DAppVersion={program_version(label)}", f"/DDist={stage.resolve()}",
        f"/DStem={stem}", f"/O{output.resolve()}", str(root / "tools/package/installer.iss"))
    setup = output / f"{stem}-Setup.exe"
    if not setup.is_file():
        raise RuntimeError(f"Inno Setup made no {setup.name}")
    write_digest(setup)
    print(f"Installer {setup}")
    return setup


# --- Linux ---

def needed_sdl(executable):
    """The SDL library the Linux program loads, by its soname (readelf's NEEDED): libSDL3.so.0."""
    out = subprocess.run(["readelf", "-d", str(executable)], capture_output=True, text=True, check=True).stdout
    names = re.findall(r"\(NEEDED\).*\[(libSDL3\.so[^\]]*)\]", out)
    if not names:
        raise RuntimeError(f"{executable.name} loads no libSDL3")
    return names[0]


def resolved_beside(executable, library):
    """ldd's answer: `library` is loaded from the program's own folder, not the system's."""
    out = subprocess.run(["ldd", str(executable)], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        if line.strip().startswith(library + " => "):
            return Path(line.split("=>")[1].split("(")[0].strip()).resolve().parent == executable.parent.resolve()
    return False


def package_linux(root, build, sdl, output, label, game, installer, tools):
    """CHI-Rebuild-<label>-linux-x86_64.tar.gz: chi, SDL's library beside it (found through $ORIGIN) and the
    rest of the zip's layout, smoke-tested from the unpacked archive (the checks' form; the release form is
    the AppImage). With `installer`, the AppImage too."""
    name = f"{STEM}-{label}-linux-x86_64"
    stage = output / name
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    binary = configure_and_build(root, build, sdl, label)
    shutil.copy2(binary, stage / PROGRAM)
    check_marker(stage / PROGRAM, label)
    soname = needed_sdl(stage / PROGRAM)
    found = [p for p in build.rglob(soname) if p.exists()]
    if not found:
        raise RuntimeError(f"Missing runtime dependency: {soname}")
    shutil.copy2(found[0].resolve(), stage / soname)
    stage_common(root, build, sdl, stage)
    check_paths(root, stage, root, sdl)
    archive = output / f"{name}.tar.gz"
    with tarfile.open(archive, "w:gz") as target:
        target.add(stage, arcname=name)
    extracted = output / "smoke"
    if extracted.exists():
        shutil.rmtree(extracted)
    extracted.mkdir()
    with tarfile.open(archive) as source:
        source.extractall(extracted, filter="data")
    executable = extracted / name / PROGRAM
    if not resolved_beside(executable, soname):
        raise RuntimeError(f"The unpacked {PROGRAM} does not load {soname} from beside it")
    smoke(executable, game)
    write_digest(archive)
    print(f"Packaged and smoke-tested {archive}")
    if installer:
        appimage(root, stage, output, label, game, sdl, tools)
    return archive


def appimage(root, stage, output, label, game, sdl, tools):
    """CHI-Rebuild-<label>-x86_64.AppImage: usr/bin/chi with data/ beside it (SDL_GetBasePath), the
    notices in usr/share/doc/chi, the libraries by linuxdeploy. `tools` holds linuxdeploy-x86_64.AppImage
    and appimagetool-x86_64.AppImage. The desktop entry's icon is tools/package/chi.png (ICONS.md)."""
    icon = root / "tools/package/chi.png"
    if not icon.is_file():
        raise RuntimeError("The AppImage needs an icon: tools/package/chi.png is missing")
    if not tools:
        raise RuntimeError("--appimage-tools DIR (linuxdeploy and appimagetool) is needed for the AppImage")
    tools = Path(tools).resolve()
    appdir = output / "AppDir"
    if appdir.exists():
        shutil.rmtree(appdir)
    bin_dir, doc = appdir / "usr/bin", appdir / f"usr/share/doc/{PROGRAM}"
    bin_dir.mkdir(parents=True)
    doc.mkdir(parents=True)
    shutil.copy2(stage / PROGRAM, bin_dir / PROGRAM)
    shutil.copytree(stage / "data", bin_dir / "data")
    for name in ("LICENSE", "README.txt", "licenses"):
        (shutil.copytree if (stage / name).is_dir() else shutil.copy2)(stage / name, doc / name)
    env = dict(os.environ, APPIMAGE_EXTRACT_AND_RUN="1", ARCH="x86_64",
               LD_LIBRARY_PATH=os.pathsep.join([str(stage.resolve()), os.environ.get("LD_LIBRARY_PATH", "")]))
    run(str(tools / "linuxdeploy-x86_64.AppImage"), "--appdir", str(appdir), "-e", str(bin_dir / PROGRAM),
        "-d", str(root / "tools/package/chi.desktop"), "-i", str(icon), env=env)
    check_marker(bin_dir / PROGRAM, label)
    smoke(bin_dir / PROGRAM, game)
    check_paths(root, appdir, root, sdl)
    image = output / f"{STEM}-{label}-x86_64.AppImage"
    run(str(tools / "appimagetool-x86_64.AppImage"), str(appdir), str(image), env=env)
    # Started without FUSE (a CI runner has none): the image unpacks itself and runs.
    smoke(image.resolve(), game, APPIMAGE_EXTRACT_AND_RUN="1")
    write_digest(image)
    print(f"AppImage {image}")
    return image


# --- macOS ---

def check_archs(files, archs):
    """Every one of `files` holds each of `archs` (lipo): a universal app has no thin part."""
    for file in files:
        got = set(subprocess.run(["lipo", "-archs", str(file)], capture_output=True, text=True,
                                 check=True).stdout.split())
        if not set(archs) <= got:
            raise RuntimeError(f"{file.name} holds {' '.join(sorted(got))}, not {' '.join(archs)}")


def smoke_x86_64(exe):
    """The universal app's other half: its x86_64 program started under Rosetta (--help and --status), when
    this Mac has Rosetta; otherwise said and skipped."""
    if subprocess.run(["arch", "-x86_64", "/usr/bin/true"], capture_output=True).returncode != 0:
        print("Smoke: x86_64 not started (no Rosetta on this Mac)")
        return
    with tempfile.TemporaryDirectory(prefix="chi-smoke-") as scratch:
        env = hidden_env(Path(scratch))
        for args in (["--help"], ["--status"]):
            result = subprocess.run(["arch", "-x86_64", str(exe)] + args, env=env, capture_output=True, text=True,
                                    timeout=SMOKE_SECONDS)
            if result.returncode != 0:
                raise RuntimeError(f"The x86_64 program failed {args[0]} ({result.returncode})")
    print("Smoke: the x86_64 program answers --help and --status (Rosetta)")


def package_mac(root, build, sdl, output, label, game, universal=True):
    """Inception Rebuild.app and CHI-Rebuild-<label>-mac.dmg: the program in Contents/MacOS, data/ and the
    notices in Contents/Resources (SDL_GetBasePath), SDL in Contents/Frameworks named @rpath, every binary
    signed ad hoc, or with a Developer ID and the dmg notarized (sign_app, notarize_dmg). Universal (arm64 and x86_64, checked with lipo), or with `universal` off this Mac's
    architecture alone (a local test)."""
    if developer_id():  # refused before anything is built when it couldn't be notarized
        run(str(root / SIGNER), "check")
    binary = configure_and_build(root, build, sdl, label, universal)
    app = output / f"{APP_NAME}.app"
    if app.exists():
        shutil.rmtree(app)
    macos, frameworks, resources = (app / "Contents" / d for d in ("MacOS", "Frameworks", "Resources"))
    for d in (macos, frameworks, resources):
        d.mkdir(parents=True)
    exe = macos / PROGRAM
    shutil.copy2(binary, exe)

    def dependencies(file):
        lines = subprocess.run(["otool", "-L", str(file)], capture_output=True, text=True,
                               check=True).stdout.splitlines()
        return sorted({l.strip().split(" (")[0] for l in lines[1:] if l.startswith(("\t", " "))})

    def system(path):
        return path.startswith(("/System/", "/usr/lib/"))

    copied, todo = set(), [exe]
    while todo:
        file = todo.pop()
        for dep in dependencies(file):
            leaf = Path(dep).name
            if system(dep) or dep in copied or dep.startswith(("@executable_path", "@loader_path")) or leaf == file.name:
                continue  # a library lists its own install name too
            if not dep.startswith("@rpath/"):
                raise RuntimeError(f"{file.name} loads {dep} from outside the build: build its own copy")
            found = [p for p in build.rglob(leaf) if p.is_file() or p.is_symlink()]
            if not found:
                raise RuntimeError(f"{dep}: not in {build}")
            target = frameworks / leaf
            if not target.exists():
                shutil.copy2(found[0].resolve(), target)
                target.chmod(0o644)
                run("install_name_tool", "-id", f"@rpath/{leaf}", str(target), stderr=subprocess.DEVNULL)
                todo.append(target)
            copied.add(dep)
    # The build's rpaths, each once: a universal program lists them once a slice, and one delete takes all.
    lines = subprocess.run(["otool", "-l", str(exe)], capture_output=True, text=True, check=True).stdout.splitlines()
    rpaths = dict.fromkeys(lines[i + 2].strip().split(" ")[1] for i, line in enumerate(lines) if "cmd LC_RPATH" in line)
    for rpath in rpaths:
        run("install_name_tool", "-delete_rpath", rpath, str(exe))
    run("install_name_tool", "-add_rpath", "@executable_path/../Frameworks", str(exe))
    left = [d for f in [exe, *frameworks.iterdir()] for d in dependencies(f) if not (system(d) or d.startswith("@"))]
    if left:
        raise RuntimeError("still outside the app: " + ", ".join(sorted(set(left))))
    if not list(frameworks.glob("libSDL3*")):
        raise RuntimeError("Missing runtime dependency: libSDL3")
    if universal:
        check_archs([exe, *frameworks.iterdir()], MAC_ARCHS)
        check_marker_slices(exe, label, MAC_ARCHS)
    else:
        check_marker(exe, label)

    stage_common(root, build, sdl, resources)
    shutil.copy2(root / "tools/package/chi.icns", resources / "chi.icns")
    minos = re.search(r"minos (\S+)", subprocess.run(["otool", "-l", str(exe)], capture_output=True, text=True,
                                                      check=True).stdout)
    version = program_version(label)
    plist = {"CFBundleName": APP_NAME, "CFBundleDisplayName": APP_NAME, "CFBundleIdentifier": BUNDLE_ID,
             "CFBundleExecutable": PROGRAM, "CFBundlePackageType": "APPL", "CFBundleIconFile": "chi",
             "CFBundleShortVersionString": version, "CFBundleVersion": version, "CFBundleDevelopmentRegion": "en",
             "CFBundleInfoDictionaryVersion": "6.0", "LSMinimumSystemVersion": minos.group(1) if minos else "12.0",
             "LSApplicationCategoryType": "public.app-category.games", "NSHighResolutionCapable": True,
             "NSHumanReadableCopyright": "A fan rebuild; BattleTech is a trademark of its owners. "
                                         "Needs your own copy of The Crescent Hawk's Inception (DOS 1.03 or Amiga 2.3)."}
    with open(app / "Contents/Info.plist", "wb") as f:
        plistlib.dump(plist, f)
    # Signed before the smoke test, so a release's hardened runtime is what is started.
    sign_app(root, app, frameworks, exe, str(root / "tools/package/chi.entitlements"))
    smoke(exe, game)
    if universal:
        smoke_x86_64(exe)

    dmg = output / f"{STEM}-{label}-mac.dmg"
    dmg_stage = output / "dmg"
    for old in (dmg_stage, dmg):
        if old.is_dir():
            shutil.rmtree(old)
        elif old.exists():
            old.unlink()
    dmg_stage.mkdir()
    run("ditto", str(app), str(dmg_stage / app.name))
    (dmg_stage / "Applications").symlink_to("/Applications")
    shutil.copy2(resources / "README.txt", dmg_stage / "Read me.txt")
    check_paths(root, dmg_stage, root, sdl)
    run("hdiutil", "create", "-volname", APP_NAME, "-srcfolder", str(dmg_stage), "-ov", "-format", "UDZO", str(dmg),
        stdout=subprocess.DEVNULL)
    shutil.rmtree(dmg_stage)
    notarize_dmg(root, dmg, app)  # stapling changes the dmg: its digest comes after
    write_digest(dmg)
    print(f"App: {app}\nDMG: {dmg}")
    return dmg


def package(root, build, sdl, output, label, game=None, installer=False, tools=None, universal=True):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", label):
        raise ValueError("Invalid package label")
    output.mkdir(parents=True, exist_ok=True)
    which = platform_name()
    if which == "mac":
        return package_mac(root, build, sdl, output, label, game, universal)
    if which == "win64":
        return package_windows(root, build, sdl, output, label, game, installer)
    return package_linux(root, build, sdl, output, label, game, installer, tools)


def packaged_program(folder):
    for name in [f"{PROGRAM}.exe", PROGRAM, f"Contents/MacOS/{PROGRAM}", f"usr/bin/{PROGRAM}",
                 f"{APP_NAME}.app/Contents/MacOS/{PROGRAM}"]:
        if (folder / name).is_file():
            return (folder / name).resolve()
    return None


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sdl", type=Path, help="SDL3's source (3.4.12, pinned by commit: docs/RELEASING.md)")
    parser.add_argument("--label", help="the release tag (chi-vX.Y.Z) or a test name with -test in it")
    parser.add_argument("--smoke", type=Path, help="a package folder (or an AppImage): run the smoke test only")
    parser.add_argument("--this-arch", action="store_true",
                        help="macOS: this Mac's architecture only, for a local test (a release is universal)")
    parser.add_argument("--installer", action="store_true",
                        help="Windows: also Setup.exe (Inno Setup); Linux: also the AppImage")
    parser.add_argument("--appimage-tools", type=Path, help="Linux: the folder with linuxdeploy and appimagetool")
    parser.add_argument("--game", type=Path, default=os.environ.get("CHR_GAME_DIR") or None,
                        help="the player's game folder for the smoke test (default CHR_GAME_DIR; never packaged)")
    options = parser.parse_args()
    if options.smoke:
        executable = options.smoke.resolve() if options.smoke.is_file() else packaged_program(options.smoke)
        if not executable:
            parser.error(f"no {PROGRAM} program in {options.smoke}")
        smoke(executable, options.game)
    else:
        if not options.sdl or not options.label:
            parser.error("--sdl and --label are required to package")
        root = Path(__file__).resolve().parents[1]
        package(root, root / "build-package", options.sdl.resolve(), root / "dist", options.label, options.game,
                options.installer, options.appimage_tools, not options.this_arch)
