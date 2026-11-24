#!/usr/bin/env python3
"""Run the ORIGINAL game headless with a key script and collect screenshots.

    run.py --game-dir DIR --out OUTDIR "3 @snap:title enter ... @snap:town"

It builds a scratch C: drive from the player's own game folder (copied, never
changed, never into the repo), adds SNAP.COM (tools/capture/snap.asm, built
here with nasm) and KEYS.BIN, and runs DOSBox-X with -silent, no sound, no
PC speaker and SDL's dummy video driver: no window opens and nothing plays.
On macOS it runs from a background-only copy (background_app) to keep it from
taking the focus (docs/reference.md says what is proven).
The scratch drive goes in CHR_SCRATCH (else the system's temp folder) and is
deleted afterwards. SNAP feeds each key only
when the game is waiting for one (docs/reference.md), so runs do not race
the game's loading. Each "@snap:NAME" writes OUTDIR/NAME.png (and NAME.bin,
the raw capture) at the moment the game next waits for a key.

Script tokens, separated by spaces or commas, "*N" repeats:
  up down left right home end pgup pgdn enter space esc, a single letter
  or digit, and @snap:NAME.

Exit status: 0 when the whole script ran, 1 on a timeout or error.
"""
import argparse
import hashlib
import os
import plistlib
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(TOOLS, "capture"))
import snap2png  # noqa: E402

DOSBOX = "/Applications/dosbox-x.app/Contents/MacOS/dosbox-x"
SNAP_MARK = 0xFFFF
IDLE_MARK = 0xFFFE
ARM_MARK = 0xFFFC  # "@arm:N": the poll count set at once (tools/capture/snap.asm)
POKE_MARK = 0xFFFD

# A test poke, "@poke:SEG:OFFSET=BYTES" (hex, e.g. @poke:F:d30c=02), names
# its segment as the docs do (docs/original.md, "Data segments"); SNAP finds
# it through the DGROUP word the game itself keeps it in. Data only (SNAP
# refuses code segments), and never what decides the copy-protection path:
# flag 14 (F:d320) is refused here.
POKE_SEGMENTS = {"F": 0x5528, "P": 0x562A, "DG": 0xFFFF}
POKE_FORBIDDEN = [("F", 0xD320, 0xD321, "flag 14, which decides whether the quiz runs")]


def poke_words(token):
    """The KEYS.BIN words of one @poke token."""
    body = token[len("@poke:"):]
    try:
        where, data = body.split("=")
        seg, off = where.split(":")
        offset = int(off, 16)
        values = bytes.fromhex(data)
    except ValueError:
        raise ValueError(f"bad poke {token!r}: use @poke:SEG:OFFSET=BYTES")
    if seg not in POKE_SEGMENTS:
        raise ValueError(f"bad poke {token!r}: the segment is one of {', '.join(POKE_SEGMENTS)}")
    if not values or offset + len(values) > 0x10000:
        raise ValueError(f"bad poke {token!r}: no bytes, or past the segment's end")
    for fseg, lo, hi, why in POKE_FORBIDDEN:
        if seg == fseg and offset < hi and offset + len(values) > lo:
            raise ValueError(f"refused poke {token!r}: it would write {why}")
    return [POKE_MARK, POKE_SEGMENTS[seg], offset, len(values)] + list(values)


# DOSBox-X's configuration for a run. No window at all (SDL's dummy video
# driver: the captures come from SNAP inside the game, not from the window).
# And never the host's mouse or focus: no mouse lock on a click, no
# middle-button unlock or clipboard button, no menu bar, no waiting on an
# error, normal priority; no quit dialog; no DOS mouse driver (BTECH.EXE
# makes no INT 33h call). The drive is mounted relative to the working
# folder, so captures don't show a temp path.
RUN_CONF = ("[sdl]\noutput=surface\nvideodriver=dummy\n"
            "autolock=false\nmiddle_unlock=none\nclip_mouse_button=none\n"
            "mouse_emulation=locked\nshowmenu=false\nwaitonerror=false\npriority=normal,normal\n"
            "[dosbox]\nquit warning=false\n"
            "[dos]\nint33=false\n"
            "[mixer]\nnosound=true\n"
            "[speaker]\npcspeaker=false\ntandy=off\n"
            "[autoexec]\n"
            "mount c c\n"
            "c:\n"
            "SNAP 0 K\n"
            "BTECH\n")


# BIOS key words: scan code << 8 | character.
NAMED = {
    "up": 0x4800, "down": 0x5000, "left": 0x4B00, "right": 0x4D00,
    "home": 0x4700, "pgup": 0x4900, "end": 0x4F00, "pgdn": 0x5100,
    "enter": 0x1C0D, "space": 0x3920, "esc": 0x011B,
    "backspace": 0x0E08,
}
ROWS = {  # scan codes of the letter and digit keys
    "1234567890": 0x02, "qwertyuiop": 0x10, "asdfghjkl": 0x1E, "zxcvbnm": 0x2C,
}


def key_word(name):
    if name in NAMED:
        return NAMED[name]
    if len(name) == 1:
        for row, first in ROWS.items():
            i = row.find(name.lower())
            if i >= 0:
                return (first + i) << 8 | ord(name)
    raise ValueError(f"unknown key {name!r}")


def compile_script(script):
    """The KEYS.BIN words and the capture names, in order."""
    words, names = [], []
    for token in script.replace(",", " ").split():
        if token.startswith("@snap:"):
            words.append(SNAP_MARK)
            names.append(token[len("@snap:"):])
            continue
        if token.startswith("@poke:"):
            words.extend(poke_words(token))
            continue
        if token.startswith("@arm:"):
            words.extend([ARM_MARK, int(token[len("@arm:"):])])
            continue
        if token.startswith("idle:"):
            # N more passes of a polling wait before the next item: in the
            # town's main loop, N more idle passes (docs/reference.md).
            words.extend([IDLE_MARK, int(token[len("idle:"):])])
            continue
        name, _, count = token.partition("*")
        words.extend([key_word(name)] * (int(count) if count else 1))
    return words, names


def text_screen(data):
    """The characters of a text-mode capture, 80x25."""
    t = data[24 + 768:24 + 768 + 4000]
    rows = []
    for y in range(25):
        rows.append("".join(chr(c) if 32 <= c < 127 else " "
                            for c in t[2 * y * 80:2 * (y + 1) * 80:2]).rstrip())
    return "\n".join(rows).rstrip("\n") + "\n"


_background_lock = threading.Lock()


def background_app(binary):
    """The DOSBox-X to launch: on macOS, a rig-only app bundle around a copy
    of `binary` whose Info.plist sets LSBackgroundOnly, which asks the system
    not to make it the frontmost app or give it a Dock icon (its own Cocoa
    code would, even with the dummy video driver). Elsewhere, `binary` itself.
    The copy is signed ad hoc, as its Info.plist is part of the signature;
    the cache key hashes `binary`, not the copy. Kept under CHR_SCRATCH (else
    the temp folder) by the binary's hash, made once for every run."""
    if sys.platform != "darwin":
        return binary
    with _background_lock:
        return _background_app(binary)


def _background_app(binary):
    h = hashlib.sha256()
    with open(binary, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    root = os.path.join(os.environ.get("CHR_SCRATCH") or tempfile.gettempdir(), "chr-background-apps")
    name = os.path.basename(binary)
    app = os.path.join(root, h.hexdigest()[:16], name + ".app")
    exe = os.path.join(app, "Contents", "MacOS", name)
    if os.path.exists(exe):
        return exe
    os.makedirs(os.path.dirname(app), exist_ok=True)
    temp = tempfile.mkdtemp(prefix="making-", suffix=".app", dir=os.path.dirname(app))
    contents = os.path.join(temp, "Contents")
    macos = os.path.join(contents, "MacOS")
    os.makedirs(macos)
    shutil.copy(binary, os.path.join(macos, name))
    here = os.path.dirname(os.path.abspath(binary))
    source = os.path.dirname(here)
    info = {}
    if os.path.basename(source) == "Contents" and os.path.exists(os.path.join(source, "Info.plist")):
        with open(os.path.join(source, "Info.plist"), "rb") as f:
            info = plistlib.load(f)
        if os.path.isdir(os.path.join(source, "Resources")):
            os.symlink(os.path.join(source, "Resources"), os.path.join(contents, "Resources"))
        # The libraries the binary loads from @executable_path/<arch>.
        for arch in ("x86_64", "arm64"):
            if os.path.isdir(os.path.join(here, arch)):
                os.symlink(os.path.join(here, arch), os.path.join(macos, arch))
    info.update({"CFBundleExecutable": name, "CFBundlePackageType": "APPL",
                 "CFBundleIdentifier": "local.inception-rig." + name, "LSBackgroundOnly": True})
    with open(os.path.join(contents, "Info.plist"), "wb") as f:
        plistlib.dump(info, f)
    signed = subprocess.run(["codesign", "--force", "--sign", "-", temp], capture_output=True, text=True)
    if signed.returncode:
        shutil.rmtree(temp, ignore_errors=True)
        raise RuntimeError("run.py: signing the background DOSBox-X failed: " + signed.stderr.strip())
    try:
        os.rename(temp, app)
    except OSError:  # another run made it first
        shutil.rmtree(temp, ignore_errors=True)
    return exe


def build_snap(dest):
    src = os.path.join(TOOLS, "capture", "snap.asm")
    subprocess.run(["nasm", "-f", "bin", "-o", dest, src], check=True)


def max_keys():
    """SNAP's key buffer in words (snap.asm's MAX_KEYS). A longer KEYS.BIN
    is refused, SNAP stays out and nothing is captured."""
    with open(os.path.join(TOOLS, "capture", "snap.asm")) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 3 and parts[0] == "MAX_KEYS" and parts[1] == "equ":
                return int(parts[2])
    raise ValueError("snap.asm has no MAX_KEYS")


# The DOSBox-X process groups running now, so a stopped run can kill them.
# DOSBox-X answers anything but SIGKILL with a "Quit DOSBox-X" dialog on
# screen, so they are only ever stopped with SIGKILL.
_running = set()
_work_dirs = set()
_running_lock = threading.Lock()


def kill_all():
    with _running_lock:
        groups, works = list(_running), list(_work_dirs)
    for pgid in groups:
        try:
            os.killpg(pgid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    for work in works:
        shutil.rmtree(work, ignore_errors=True)


def stop_on_signals():
    """On SIGINT or SIGTERM, SIGKILL every DOSBox-X this process started and
    exit (call from the main thread)."""
    def stop(signum, _frame):
        kill_all()
        os._exit(128 + signum)
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)


def save_run_log(work, c, to):
    """DOSBox-X's log and the scratch drive's file list (name and size),
    as `to`-dosbox.log and `to`-files.txt, for a run that went wrong."""
    shutil.copyfile(os.path.join(work, "dosbox.log"), to + "-dosbox.log")
    with open(to + "-files.txt", "w") as f:
        for root, _dirs, files in os.walk(c):
            for name in sorted(files):
                path = os.path.join(root, name)
                f.write(f"{os.path.relpath(path, c)} {os.path.getsize(path)}\n")


def run(game_dir, script, out_dir, timeout=120, keep=False, log_to=None, seed=(), stall=None, why=None, env=None):
    """`timeout`: seconds for the whole script. `stall`: seconds with no new
    capture after which the run counts as stuck (None: no such limit). Either
    way DOSBox-X is stopped. `why`, a list, gets "timeout", "stalled" or
    "exited" appended when the script does not finish. `env`: more
    environment for DOSBox-X (the speaker log's file, docs/reference.md)."""
    words, names = compile_script(script)
    if len(words) > max_keys():
        raise ValueError(f"run.py: the key script is {len(words)} words; SNAP holds {max_keys()} (snap.asm MAX_KEYS)")
    os.makedirs(out_dir, exist_ok=True)
    scratch = os.environ.get("CHR_SCRATCH")
    if scratch:
        os.makedirs(scratch, exist_ok=True)
    work = tempfile.mkdtemp(prefix="chr-ref-", dir=scratch)
    if not keep:
        with _running_lock:
            _work_dirs.add(work)
    try:
        c = os.path.join(work, "c")
        # Fresh timestamps (shutil.copy, not copy2): macOS's temp cleaner
        # (dirhelper, daily at 03:35) deletes temp files older than 3 days,
        # and copies dated like the originals vanished mid-run
        # (docs/reference.md, "the disk prompt").
        shutil.copytree(game_dir, c, copy_function=shutil.copy)
        # Files the game finds on its drive from the start (a saved game
        # an earlier run wrote): (source, name) pairs.
        for source, name in seed:
            shutil.copyfile(source, os.path.join(c, name))
        build_snap(os.path.join(c, "SNAP.COM"))
        with open(os.path.join(c, "KEYS.BIN"), "wb") as f:
            f.write(struct.pack(f"<H{len(words)}H", len(words), *words))
        conf = os.path.join(work, "run.conf")
        with open(conf, "w") as f:
            f.write(RUN_CONF)
        child_env = dict(os.environ, **(env or {}), SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy")
        log = open(os.path.join(work, "dosbox.log"), "w")
        proc = subprocess.Popen([background_app(DOSBOX), "-silent", "-conf", conf], cwd=work, env=child_env,
                                stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        with _running_lock:
            _running.add(proc.pid)
        done = os.path.join(c, "SNAPEND.TXT")
        deadline = time.time() + timeout
        finished = False
        reason = "timeout"
        captures, last_capture = 0, time.time()
        try:
            while time.time() < deadline:
                if os.path.exists(done):
                    finished = True
                    time.sleep(0.5)  # the last capture's file is closed first; let it settle
                    break
                if proc.poll() is not None:
                    reason = "exited"
                    break
                if stall is not None:
                    n = sum(1 for f in os.listdir(c) if f.startswith("SNAP") and f.endswith(".BIN"))
                    if n != captures:
                        captures, last_capture = n, time.time()
                    elif time.time() - last_capture > stall:
                        reason = "stalled"
                        break
                time.sleep(0.2)
        finally:
            # DOSBox-X ignores a plain kill while the game runs.
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            proc.wait()
            with _running_lock:
                _running.discard(proc.pid)
            log.close()
        if os.path.exists(os.path.join(c, "POKEFAIL.TXT")):
            print("run.py: SNAP refused a poke (not a data segment, or no game DS yet)", file=sys.stderr)
            finished = False
        shots = sorted(f for f in os.listdir(c) if f.startswith("SNAP") and f.endswith(".BIN"))
        written = []
        for name, shot in zip(names, shots):
            data = open(os.path.join(c, shot), "rb").read()
            shutil.copyfile(os.path.join(c, shot), os.path.join(out_dir, name + ".bin"))
            if len(data) > 5 and data[5] <= 3:  # a text mode: write the characters
                with open(os.path.join(out_dir, name + ".txt"), "w") as f:
                    f.write(text_screen(data))
                written.append(name)
                continue
            try:
                w, h, rows = snap2png.decode(data)
            except ValueError as e:
                print(f"{name}: {e}", file=sys.stderr)
                continue
            snap2png.png(os.path.join(out_dir, name + ".png"), w, h, rows)
            written.append(name)
        # The saved games the script made (docs/formats/save.md), as GameN.
        for f in os.listdir(c):
            if len(f) == 5 and f.lower().startswith("game") and f[4] in "123456":
                shutil.copyfile(os.path.join(c, f), os.path.join(out_dir, "Game" + f[4]))
        if not finished and why is not None:
            why.append(reason)
        if not finished:
            print(f"run.py: the script did not finish ({len(shots)} of {len(names)} captures); "
                  f"DOSBox-X log in {work}" if keep else
                  f"run.py: the script did not finish ({len(shots)} of {len(names)} captures)",
                  file=sys.stderr)
        return finished, written
    finally:
        if log_to:
            save_run_log(work, os.path.join(work, "c"), log_to)
        if not keep:
            shutil.rmtree(work, ignore_errors=True)
            with _running_lock:
                _work_dirs.discard(work)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("script")
    ap.add_argument("--game-dir", default=os.environ.get("CHR_GAME_DIR"), required=not os.environ.get("CHR_GAME_DIR"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--timeout", type=float, default=120)
    ap.add_argument("--keep", action="store_true", help="keep the scratch C: drive and log")
    a = ap.parse_args()
    stop_on_signals()
    if not os.path.exists(DOSBOX):
        print(f"run.py: DOSBox-X not found at {DOSBOX}", file=sys.stderr)
        return 1
    ok, written = run(a.game_dir, a.script, a.out, a.timeout, a.keep)
    for name in written:
        print(os.path.join(a.out, name))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
