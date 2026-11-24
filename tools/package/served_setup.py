"""The setup served to the launcher (docs/setup.md, "The launcher contract"), asked of a built or
packaged program as the launcher asks it: stdin and stdout are pipes. The copies of the game are
made up, and every run has its own player folder (CHR_DATA_DIR) under `scratch`.

tests/status_setup_test.py runs these on the build and tools/build_package.py on each package.
"""
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time

SECONDS = 120
MANY = 6000  # files in the copy that is stopped: enough that it can't end before the stop arrives


def asking_env(env, user):
    """`env` with the player's folder `user`. The video driver doesn't exist, so a run that tried to
    open a window would fail, and say so."""
    out = {k: v for k, v in env.items() if not k.startswith(("REKIT_SETUP_", "CHR_SETUP_")) and k != "CHR_GAME_DIR"}
    out.update(CHR_HIDDEN="1", CHR_DATA_DIR=str(user), SDL_VIDEO_DRIVER="none-of-them", SDL_AUDIO_DRIVER="dummy")
    return out


class Answer:
    """What one ask answered: its exit, its stdout's lines as JSON (None for a line that isn't), its stderr."""

    def __init__(self, code, lines, stderr):
        self.code, self.lines, self.stderr = code, lines, stderr
        self.messages = []
        for line in lines:
            try:
                value = json.loads(line)
            except ValueError:
                value = None
            self.messages.append(value if isinstance(value, dict) and "done" in value else None)
        self.last = self.messages[-1] if self.messages and self.messages[-1] else {}

    def clean(self):
        """Nothing but messages on stdout, `done` on the last and only there."""
        return bool(self.messages) and None not in self.messages and [m["done"] for m in self.messages] == \
            [False] * (len(self.messages) - 1) + [True]

    def __str__(self):
        return "exit %s, %d lines, last %s, stderr %s" % (self.code, len(self.lines), self.last, self.stderr[-300:])


def ask(program, args, env, cancel_at=None, gone_at=None, kill_at=None):
    """`program --setup args`, both ends pipes. With a stage's first word: the "cancel" line is sent
    (cancel_at), stdout closed (gone_at) or the program killed (kill_at) when that stage's first
    line comes; "" is at once."""
    p = subprocess.Popen([str(program), "--setup"] + [str(a) for a in args], env=env, stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    errors = []
    reader = threading.Thread(target=lambda: errors.append(p.stderr.read()), daemon=True)
    reader.start()
    got = queue.Queue()

    def read():
        try:
            for raw in p.stdout:
                got.put(raw.decode("utf-8", "replace").rstrip("\r\n"))
        except ValueError:
            pass  # closed here
        got.put(None)

    lines, acted = [], False
    at = cancel_at if cancel_at is not None else gone_at if gone_at is not None else kill_at

    def act():
        if cancel_at is not None:
            try:
                p.stdin.write(b"cancel\n")
                p.stdin.flush()
            except OSError:
                pass  # already ended: the answer's checks say what that means
        elif kill_at is not None:
            p.kill()

    if at == "" and gone_at is None:
        act()
        acted = True
    if gone_at is not None:
        # One line read by hand: a reader thread would hold the pipe open.
        while True:
            raw = p.stdout.readline()
            if not raw:
                break
            lines.append(raw.decode("utf-8", "replace").rstrip("\r\n"))
            if '"stage": "%s' % gone_at in lines[-1]:
                break
        p.stdout.close()
    else:
        threading.Thread(target=read, daemon=True).start()
        end = time.monotonic() + SECONDS
        while True:
            try:
                line = got.get(timeout=max(0.1, end - time.monotonic()))
            except queue.Empty:
                p.kill()
                raise RuntimeError("--setup %s did not end" % args)
            if line is None:
                break
            lines.append(line)
            if at and not acted and '"stage": "%s' % at in line:
                act()
                acted = True
    code = p.wait(timeout=SECONDS)
    reader.join(5)
    try:
        p.stdin.close()
    except OSError:
        pass
    return Answer(code, lines, (errors[0] if errors else b"").decode("utf-8", "replace"))


def made_up_copy(folder, files=3):
    """Another version's copy as far as the recipe goes: a BTECH.EXE that isn't DOS 1.03's, the first
    map, and `files` files more. Nothing of the game's."""
    folder = Path(folder)
    folder.mkdir(parents=True)
    (folder / "BTECH.EXE").write_bytes(b"made up")
    (folder / "MAP1.MTP").write_bytes(b"made up")
    for i in range(files):
        (folder / ("F%05d.DAT" % i)).write_bytes(b"made up %d\n" % i * 60)
    return folder


def tree(folder):
    out = []
    for d, _, files in os.walk(str(folder)):
        out += [os.path.relpath(os.path.join(d, f), str(folder)) for f in files]
    return sorted(out)


def checks(program, env, scratch, kill=True):
    """Yields (passed, what) for each check. `kill`: also kill a run in the middle of a copy."""
    scratch = Path(scratch)
    scratch.mkdir(parents=True, exist_ok=True)

    def run(user, args, **more):
        return ask(program, args, asking_env(env, scratch / user), **more)

    copy = made_up_copy(scratch / "copy")
    before = tree(copy)
    d = run("empty", ["--describe"])
    yield d.clean() and d.code == 0 and d.last.get("ok") and d.last.get("title") and d.last.get("pick_words"), \
        "describe: the words on stdout alone (%s)" % d
    yield d.last.get("into", "").endswith("install"), "describe: where it would go"
    for bad in (["--look"], ["--look", ""], ["--look", copy, "b"], ["--install"], ["--install", copy, "--folder"],
                ["--install", copy, "--folder", ""], ["--install", copy, "b"],
                ["--install", copy, "--folder", scratch / "x", "y"], ["--describe", "a"], ["--search", "a"]):
        a = run("empty", bad)
        yield a.clean() and len(a.lines) == 1 and a.code == 2 and a.last.get("ok") is False and a.last.get("summary"), \
            "break: %s: one line saying why, exit 2 (%s)" % (" ".join(str(w) for w in bad).replace(str(copy), "COPY"), a)
    a = run("empty", ["--nothing"])
    yield a.code == 2 and not a.lines, "break: another word after --setup is the usage, exit 2, as before (%s)" % a

    notgame = scratch / "notgame"
    notgame.mkdir()
    (notgame / "README.TXT").write_text("x")
    a = run("empty", ["--look", notgame])
    yield a.clean() and a.code == 2 and "No copy of" in a.last.get("summary", ""), "look: not the game (%s)" % a
    a = run("empty", ["--install", notgame])
    yield a.clean() and a.code == 2 and not a.last.get("ok"), "break: install: not the game"
    yield tree(scratch / "empty") == [] and not (scratch / "x").exists(), "break: nothing made in the player's folder by any of these"

    a = run("user", ["--look", copy / "BTECH.EXE"])
    yield a.clean() and a.code == 0 and a.last.get("ok") and a.last.get("words") and not a.last.get("in_place"), \
        "look: the pick of BTECH.EXE (%s)" % a
    a = run("user", ["--install", copy])
    install = scratch / "user" / "install"
    yield a.clean() and a.code == 0 and a.last.get("ok") and a.last.get("summary") and \
        a.last.get("where") and os.path.samefile(a.last["where"], install), "install: done (%s)" % a
    yield (install / "MAP1.MTP").is_file() and tree(copy) == before, "install: copied, the original left as it was"
    yield "game_folder=" in (scratch / "user" / "settings.ini").read_text(), "install: remembered in settings.ini"
    yield sorted(os.listdir(scratch / "user")) == ["install", "settings.ini"], "install: nothing else left"
    a = run("user", ["--look", install])
    yield a.clean() and a.last.get("in_place") and a.code == 0, "look: the install itself is in place (%s)" % a

    many = made_up_copy(scratch / "many", MANY)
    a = run("stopped", ["--install", many], cancel_at="Copying")
    yield a.clean() and a.code == 1 and a.last.get("cancelled") and not a.last.get("ok") and a.last.get("summary"), \
        "break: a cancel line on stdin stops the copy (%s)" % a
    yield tree(scratch / "stopped") == [] and not (scratch / "stopped" / "install.new").exists(), \
        "break: the stop left nothing behind"
    a = run("stopped", ["--install", many], cancel_at="")
    yield a.clean() and a.code == 1 and a.last.get("cancelled") and tree(scratch / "stopped") == [], \
        "break: a cancel sent at once (%s)" % a
    a = run("gone", ["--install", many], gone_at="Copying")
    yield (scratch / "gone" / "install" / "MAP1.MTP").is_file() and a.code == 0, \
        "break: the launcher gone in mid copy: the copy goes on (%s)" % a
    if kill:
        a = run("killed", ["--install", many], kill_at="Copying")
        yield not (scratch / "killed" / "install").exists(), "break: killed in mid copy: no install"
        a = run("killed", ["--install", copy])
        yield a.code == 0 and a.last.get("ok") and not (scratch / "killed" / "install.new").exists(), \
            "break: and the next install clears up and goes in (%s)" % a

    # --folder: the game's own folder; one of somebody else's, and an install that isn't ours, stay as they are.
    own = scratch / "drive" / "Inception"
    a = run("user2", ["--install", copy, "--folder", own])
    yield a.code == 0 and a.last.get("ok") and (own / "install" / "MAP1.MTP").is_file(), "folder: into one of its own (%s)" % a
    yield "game=chi" in (own / "rebuild-game-folder.txt").read_text(), "folder: marked as the game's"
    a = run("user2", ["--install", copy, "--folder", own])
    yield a.code == 0 and a.last.get("ok"), "folder: our own install there is replaced"
    (own / "install" / "inception-install.txt").unlink()
    (own / "install" / "LETTER.TXT").write_text("the player's")
    a = run("user3", ["--install", copy, "--folder", own])
    yield a.code == 2 and not a.last.get("ok") and (own / "install" / "LETTER.TXT").read_text() == "the player's", \
        "break: folder: an install without our marker is never replaced (%s)" % a
    theirs = scratch / "theirs"
    theirs.mkdir()
    (theirs / "LETTER.TXT").write_text("the player's")
    a = run("user3", ["--install", copy, "--folder", theirs])
    yield a.code == 2 and not a.last.get("ok") and tree(theirs) == ["LETTER.TXT"], \
        "break: folder: somebody else's folder is left alone (%s)" % a
    yield tree(scratch / "user3") == [], "break: nothing remembered from the refused ones"


if __name__ == "__main__":
    import tempfile
    failed = 0
    with tempfile.TemporaryDirectory(prefix="chi-served-") as scratch_dir:
        for passed, what in checks(Path(sys.argv[1]).resolve(), os.environ, scratch_dir):
            print(("ok   " if passed else "FAIL ") + what)
            failed += not passed
    sys.exit(1 if failed else 0)
