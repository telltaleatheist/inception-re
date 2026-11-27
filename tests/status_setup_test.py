"""The launcher contract on the built program (re-setup-kit DESIGN.md):
`chr --status` prints one JSON line, exits 0, opens no window and writes
nothing; `chr --setup` exits 0 ready, 1 cancelled, 2 failed (the reason as
{"error": ...} on stdout) and never opens a picker in a hidden run. With
--game (the player's copy) a copy is set up from a folder, the next --status
finds it ready and a start plays from it, and again through the kit's setup
screen, hidden and driven by its REKIT_SETUP_* hooks. Every run has its own
player folder (CHR_DATA_DIR) in the work folder, a made-up home, and SDL's
dummy drivers; nothing of the game is kept. Prints names and counts only.

    status_setup_test.py --program build/chr --work build/status_setup_work [--game DIR]
                          [--amiga COPY]
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "package"))
import served_setup  # noqa: E402

failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def tree(root):
    out = []
    for d, _, files in os.walk(root):
        out += [os.path.relpath(os.path.join(d, f), root) for f in files]
        out.append(os.path.relpath(d, root) + "/")
    return sorted(out)


def run(program, args, env, timeout=120):
    t = time.monotonic()
    p = subprocess.run([program] + args, env=env, capture_output=True, text=True, timeout=timeout)
    return p, time.monotonic() - t


def screen_pick(path, frames="200"):
    """The setup screen driven to a pick of `path`. FIND searches first, and what a search finds near
    the program is offered before any picker opens, so the find's plate (pick other files) is pressed
    too: the pick is then looked at whatever lies near the build."""
    return dict(REKIT_SETUP_FRAMES=frames, REKIT_SETUP_FIND="1", REKIT_SETUP_PICK_OTHER="1", REKIT_SETUP_GO="1",
                REKIT_SETUP_PICKER="path:" + path)


def after_pick(stdout):
    """The log's lines from the last picker on; none when no picker was asked for."""
    lines = stdout.splitlines()
    asked = [i for i, l in enumerate(lines) if l.startswith("setup: the file picker asked for")]
    return lines[asked[-1]:] if asked else []


def same(a, b):
    return os.path.normcase(os.path.normpath(a)) == os.path.normcase(os.path.normpath(b))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--program", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--game", default="")
    ap.add_argument("--amiga", default=os.environ.get("CHR_AMIGA_COPY", ""))
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)  # the test's own folder
    os.makedirs(work)
    home = os.path.join(work, "home")
    os.makedirs(home)
    base = dict(os.environ)
    for k in list(base):
        if k.startswith("REKIT_SETUP_") or k in ("CHR_DATA_DIR", "CHR_GAME_DIR", "CHR_SETUP_PICK"):
            base.pop(k)
    base.update(CHR_HIDDEN="1", SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy", SDL_RENDER_DRIVER="software",
                HOME=home, APPDATA=home, XDG_DATA_HOME=os.path.join(home, "share"))

    # --status with an empty home: one JSON line, exit 0, nothing written, fast.
    before = tree(work)
    p, took = run(a.program, ["--status"], base)
    lines = [l for l in p.stdout.splitlines() if l.strip()]
    check(p.returncode == 0, "--status exits 0")
    check(len(lines) == 1, "--status prints one line (%d)" % len(lines))
    st = json.loads(lines[0]) if len(lines) == 1 else {}
    check(st.get("kit") == 1 and st.get("game") == "inception", "--status: kit 1, game inception")
    check(st.get("files", {}).get("state") in ("missing", "ready", "incomplete"), "--status: a state")
    check(tree(work) == before, "break: --status wrote nothing")
    check(took < 5, "--status answered in %.2f s" % took)

    # --setup in a hidden run, nothing picked: failed, the reason as JSON, no picker.
    user = os.path.join(work, "user-a")
    env = dict(base, CHR_DATA_DIR=user)
    p, _ = run(a.program, ["--setup"], env)
    check(p.returncode == 2, "break: hidden --setup with nothing picked exits 2 (%d)" % p.returncode)
    check('{"error": ' in p.stdout, "hidden --setup says why as JSON")

    # A pick that is not the game: failed, nothing installed.
    notgame = os.path.join(work, "notgame")
    os.makedirs(notgame)
    open(os.path.join(notgame, "README.TXT"), "w").write("x")
    p, _ = run(a.program, ["--setup"], dict(env, CHR_SETUP_PICK=notgame))
    check(p.returncode == 2 and '{"error": ' in p.stdout, "break: a pick that is not the game exits 2")
    check(not os.path.exists(os.path.join(user, "install")), "break: nothing installed from it")

    # A test hook without the test's own player folder (HOME alone doesn't move the real one on a
    # Mac): refused before anything is made. The picks here hold no game, so a failure copies nothing.
    for hook, extra in (("CHR_SETUP_PICK", {"CHR_SETUP_PICK": notgame}),
                        ("REKIT_SETUP_GO", {"REKIT_SETUP_GO": "1", "REKIT_SETUP_FRAMES": "5"})):
        before = tree(work)
        unset = {k: v for k, v in dict(env, **extra).items() if k != "CHR_DATA_DIR"}
        p, _ = run(a.program, ["--setup"], unset)
        check(p.returncode == 2 and "needs CHR_DATA_DIR" in p.stdout and tree(work) == before,
              "break: %s without CHR_DATA_DIR is refused (%d)" % (hook, p.returncode))
        p, _ = run(a.program, [], unset)
        check(p.returncode == 1 and "needs CHR_DATA_DIR" in p.stderr, "break: %s refused at a start too" % hook)
        p, _ = run(a.program, ["--setup", "--look", notgame], unset)
        check(p.returncode == 2 and [json.loads(l)["done"] for l in p.stdout.splitlines()] == [True] and
              "needs CHR_DATA_DIR" in p.stdout and tree(work) == before, "break: %s refused when served" % hook)

    # The setup screen, hidden: the rebuild's header; no picker (unattended), a check's answer stands in.
    p, _ = run(a.program, ["--setup"], dict(env, **screen_pick(os.path.join(notgame, "README.TXT"))))
    out = after_pick(p.stdout)
    check(p.returncode == 1, "break: the screen with a pick that is not the game ends cancelled (%d)" % p.returncode)
    check("setup: header INCEPTION REBUILD" in p.stdout.splitlines(), "the screen's log: the rebuild's header")
    check(any(l.startswith("setup: No copy of") for l in out) and not any(l.startswith("setup: screen confirm") for l in out),
          "break: not the game, not offered")
    check(not os.path.exists(os.path.join(user, "install")), "break: nothing installed from the screen either")

    # Another version's copy, made up here: a C64 disk image holding one made-up file that the
    # checks' extra table (CHR_EXTRA_KNOWN_DISKS) names as disk 1 of 2. Picked by the scripted pick
    # and on the screen, it is named, not "No copy".
    d64 = bytearray(174848)
    def at(t, sec):
        return (sum(21 if i <= 17 else 19 if i <= 24 else 18 if i <= 30 else 17 for i in range(1, t)) + sec) * 256
    d64[at(18, 0)], d64[at(18, 0) + 1] = 18, 1
    d64[at(18, 1) + 1] = 0xFF
    body = b"a made-up file, not the game's"
    e = at(18, 1)
    d64[e + 2], d64[e + 3], d64[e + 4] = 0x82, 1, 0
    d64[e + 5:e + 21] = b"FIXA".ljust(16, b"\xa0")
    d64[at(1, 0)], d64[at(1, 0) + 1] = 0, len(body) + 1
    d64[at(1, 0) + 2:at(1, 0) + 2 + len(body)] = body
    other = os.path.join(work, "other")
    os.makedirs(other)
    disk = os.path.join(other, "made-up.d64")
    open(disk, "wb").write(bytes(d64))
    extra = os.path.join(work, "extra-known.txt")
    open(extra, "w").write("c64 1 2 FIXA %d %s\n" % (len(body), hashlib.sha256(body).hexdigest()))
    playable = "a copy of DOS 1.03 or Amiga 2.3"
    named = ("Disk 1 of 2 of the Commodore 64 version was found, but it can't give the same game: its towns "
             "are its own. Use " + playable + ".")
    known = dict(env, CHR_EXTRA_KNOWN_DISKS=extra)
    p, _ = run(a.program, ["--setup"], dict(known, CHR_SETUP_PICK=disk))
    check(p.returncode == 2 and json.dumps(named)[1:-1] in p.stdout, "another version's disk, picked: named in the error")
    p, _ = run(a.program, ["--status"], dict(known, CHR_GAME_DIR=disk))
    st = json.loads(p.stdout.strip().splitlines()[-1]) if p.stdout.strip() else {}
    check(st.get("files", {}).get("state") == "incomplete" and st.get("files", {}).get("missing") == [playable] and
          st.get("note") == named, "another version's disk, --status: the same note, and what is needed")
    p, _ = run(a.program, ["--setup"], dict(known, **screen_pick(disk)))
    out = after_pick(p.stdout)
    check(p.returncode == 1 and "setup: " + named in out, "another version's disk, picked on the screen: our sentence")
    check(not any("No copy of" in l for l in out), "break: not the kit's no-copy sentence")
    p, _ = run(a.program, ["--setup"], dict(env, **screen_pick(disk)))
    check(any(l.startswith("setup: No copy of") for l in after_pick(p.stdout)),
          "break: unknown to the table, the kit's sentence as before")
    check(not os.path.exists(os.path.join(user, "install")), "break: nothing installed from another version")

    # The setup served to the launcher, with made-up copies; and the words a refused pick gets are the screen's.
    check(st.get("setup") == 1, "--status: the setup is served")
    for passed, what in served_setup.checks(a.program, base, os.path.join(work, "served")):
        check(passed, "served: " + what)
    served_user = os.path.join(work, "user-served")
    ask_env = dict(served_setup.asking_env(base, served_user), CHR_EXTRA_KNOWN_DISKS=extra)
    for what in ("--look", "--install"):
        s = served_setup.ask(a.program, [what, disk], ask_env)
        check(s.clean() and s.code == 2 and s.last.get("summary") == named,
              "served: %s of another version's disk: the screen's sentence (%s)" % (what, s))
    check(served_setup.tree(served_user) == [], "break: served: nothing made for it")

    # --copies: a line for each copy on this computer, then done; nothing written; no arguments.
    copies_user = os.path.join(work, "user-copies")
    s = served_setup.ask(a.program, ["--copies"], served_setup.asking_env(base, copies_user))
    rows = [m for m in s.messages[:-1] if m]
    check(s.clean() and s.code == 0 and s.last.get("ok") is True, "served: --copies answers, done and ok (%s)" % s)
    # The kit leaves out "data" when it is "" and "in_use" when it is false.
    check(all("copy" in r and r.get("in_use", True) is True and isinstance(r.get("missing"), list) for r in rows),
          "served: --copies' lines each name a copy and what it lacks (%s)" % s)
    check(len([r for r in rows if r.get("in_use")]) <= 1, "served: --copies marks one copy in use at most")
    check(served_setup.tree(copies_user) == [], "break: served: --copies wrote nothing")
    s = served_setup.ask(a.program, ["--copies", "extra"], served_setup.asking_env(base, copies_user))
    check(s.clean() and s.code == 2 and "takes nothing after it" in s.last.get("summary", ""),
          "break: served: --copies with an argument is refused (%s)" % s)

    if a.game and os.path.isdir(a.game):
        pick = os.path.join(work, "pick", "BTECH")
        os.makedirs(pick)
        for f in os.listdir(a.game):
            if os.path.isfile(os.path.join(a.game, f)):
                shutil.copyfile(os.path.join(a.game, f), os.path.join(pick, f))
        btech = next(f for f in os.listdir(pick) if f.lower() == "btech.exe")
        user = os.path.join(work, "user-b")
        env = dict(base, CHR_DATA_DIR=user)
        p, _ = run(a.program, ["--setup"], dict(env, CHR_SETUP_PICK=os.path.join(pick, btech)))
        check(p.returncode == 0, "--setup from a pick of BTECH.EXE exits 0 (%d)" % p.returncode)
        install = os.path.join(user, "install")
        check(os.path.isfile(os.path.join(install, btech)), "the copy is in the player's folder's install")
        check(os.path.isfile(os.path.join(pick, btech)), "the player's original is left as it was")
        ini = os.path.join(user, "settings.ini")
        text = open(ini).read() if os.path.exists(ini) else ""
        check("game_folder=" in text, "game_folder saved to settings.ini")
        before = tree(work)
        p, took = run(a.program, ["--status"], env)
        st = json.loads(p.stdout.strip().splitlines()[-1]) if p.stdout.strip() else {}
        files = st.get("files", {})
        check(files.get("state") == "ready" and same(files.get("where", ""), install),
              "--status after the setup: ready, where = the install")
        check(files.get("data") == "DOS 1.03", "--status: the version")
        check(tree(work) == before, "break: --status wrote nothing after a setup either")
        check(took < 2.0, "--status found the game in %.2f s" % took)
        s = served_setup.ask(a.program, ["--copies"], served_setup.asking_env(base, user))
        mine = [m for m in s.messages[:-1] if m and same(m.get("copy", ""), install)]
        check(s.clean() and s.code == 0 and len(mine) == 1 and mine[0].get("in_use") is True and
              mine[0].get("data") == "DOS 1.03" and mine[0].get("missing") == [],
              "served: --copies lists the install, in use, DOS 1.03, nothing missing (%s)" % s)
        # The pick is in the work folder beside the program, where the search looks (drop-in): listed, not in use.
        check([same(m.get("copy", ""), install) for m in s.messages[:-1] if m and m.get("in_use")] == [True],
              "served: --copies marks the install alone in use (%s)" % [m.get("copy") for m in s.messages if m])
        check(tree(work) == before, "break: --copies wrote nothing after a setup either")

        # A start with no game folder named plays from the remembered one.
        shot = os.path.join(work, "start.bmp")
        p, _ = run(a.program, ["--map", "1", "--script", "up", "--screenshot", shot], env)
        check(p.returncode == 0 and any(l.startswith("setup: game files ") and same(l[len("setup: game files "):], install)
                                        for l in p.stdout.splitlines()),
              "a start plays from the remembered install (%d)" % p.returncode)

        # Another version's BTECH.EXE: --status incomplete, the note names DOS 1.03.
        other = os.path.join(work, "user-other")
        shutil.copytree(install, os.path.join(other, "install"))
        with open(os.path.join(other, "install", btech), "r+b") as f:
            f.truncate(os.path.getsize(os.path.join(other, "install", btech)) // 2)
        with open(os.path.join(other, "settings.ini"), "w") as f:
            f.write("game_folder=%s\n" % os.path.join(other, "install"))
        p, _ = run(a.program, ["--status"], dict(base, CHR_DATA_DIR=other))
        st = json.loads(p.stdout.strip().splitlines()[-1]) if p.stdout.strip() else {}
        check(st.get("files", {}).get("state") == "incomplete" and "DOS 1.03" in st.get("note", ""),
              "another version: incomplete, the note names DOS 1.03")
        s = served_setup.ask(a.program, ["--copies"], served_setup.asking_env(base, other))
        theirs = [m for m in s.messages[:-1] if m and same(m.get("copy", ""), os.path.join(other, "install"))]
        check(s.clean() and len(theirs) == 1 and theirs[0].get("in_use") is True and "data" not in theirs[0] and
              any("DOS 1.03" in w for w in theirs[0].get("missing", [])),
              "served: --copies lists another version's copy, in use, what it lacks naming DOS 1.03 (%s)" % s)

        # The same copy through the setup screen (hidden): found, offered, installed; and into a picked folder.
        user = os.path.join(work, "user-c")
        env = dict(base, CHR_DATA_DIR=user)
        screen = dict(env, **screen_pick(os.path.join(pick, btech)))
        p, _ = run(a.program, ["--setup"], screen)
        out = after_pick(p.stdout)
        check(p.returncode == 0, "the screen: --setup exits 0 (%d)" % p.returncode)
        check(any(l.startswith("setup: screen confirm: BTECH, INSTALL") for l in out) and
              any(l.startswith("setup: work: INSTALLING") for l in out), "the screen's log: offered, then installing")
        check(os.path.isfile(os.path.join(user, "install", btech)), "the screen's copy in the player's folder")
        games = os.path.join(work, "games")
        os.makedirs(os.path.join(games, "something else"))
        user = os.path.join(work, "user-d")
        p, _ = run(a.program, ["--setup"], dict(screen, CHR_DATA_DIR=user, REKIT_SETUP_FILES_TO=games))
        folder = os.path.join(games, "Inception Rebuild")
        mark = os.path.join(folder, "rebuild-game-folder.txt")
        check(p.returncode == 0 and os.path.isfile(os.path.join(folder, "install", btech)),
              "CHANGE FOLDER: a folder of the game's own made in a pick holding other things (%d)" % p.returncode)
        check(os.path.isfile(mark) and "\ngame=chi\n" in open(mark).read(), "its mark: game=chi")
        check(os.listdir(os.path.join(games, "something else")) == [], "what was there is left as it was")
    else:
        print("skip the game parts: no --game folder")

    # An Amiga copy (--amiga, else CHR_AMIGA_COPY: an .adf, or a folder or archive holding one).
    if a.amiga and os.path.exists(a.amiga):
        user = os.path.join(work, "user-amiga")
        env = dict(base, CHR_DATA_DIR=user)
        before = os.path.getsize(a.amiga) if os.path.isfile(a.amiga) else tree(a.amiga)
        p, _ = run(a.program, ["--setup"], dict(env, CHR_SETUP_PICK=a.amiga))
        check(p.returncode == 0, "Amiga: --setup from the pick exits 0 (%d)" % p.returncode)
        check("setup: copied 18 files" in p.stdout, "Amiga: the game's 18 files copied")
        install = os.path.join(user, "install")
        check(sorted(f for f in os.listdir(install) if f.lower().startswith("game")) == [], "break: no saved game copied")
        after = os.path.getsize(a.amiga) if os.path.isfile(a.amiga) else tree(a.amiga)
        check(before == after, "Amiga: the player's original is left as it was")
        p, _ = run(a.program, ["--status"], env)
        st = json.loads(p.stdout.strip().splitlines()[-1]) if p.stdout.strip() else {}
        check(st.get("files", {}).get("state") == "ready" and st.get("files", {}).get("data") == "Amiga 2.3",
              "Amiga: --status after the setup: ready, Amiga 2.3")
        p, _ = run(a.program, ["--map", "1", "--script", "up", "--screenshot", os.path.join(work, "amiga.bmp")], env)
        check(p.returncode == 0 and os.path.isfile(os.path.join(user, "import.json")),
              "Amiga: a start imports and plays from the install (%d)" % p.returncode)
        # The same pick through the setup screen (hidden): offered and installed.
        user2 = os.path.join(work, "user-amiga-screen")
        p, _ = run(a.program, ["--setup"], dict(base, CHR_DATA_DIR=user2, **screen_pick(a.amiga, "6000")))
        out = after_pick(p.stdout)
        check(p.returncode == 0 and any(l.startswith("setup: screen confirm: %s, INSTALL" % os.path.basename(a.amiga)) for l in out),
              "Amiga: the screen offers the pick (%d)" % p.returncode)
        check(os.path.isfile(os.path.join(user2, "install", "battletech")), "Amiga: the screen's copy in the player's folder")
        # And served to the launcher: the screen's words, installed as by the screen, stopped.
        user3 = os.path.join(work, "user-amiga-served")
        ask_env = served_setup.asking_env(base, user3)
        plate = [l for l in out if l.startswith("setup: found plate: ")]
        s = served_setup.ask(a.program, ["--look", a.amiga], ask_env)
        check(s.clean() and s.code == 0 and plate and
              plate[-1] == "setup: found plate: %s (%s)" % (os.path.basename(a.amiga), s.last.get("words")),
              "Amiga: served: the screen's words (%s; %s)" % (plate[-1:], s))
        s = served_setup.ask(a.program, ["--install", a.amiga], ask_env, cancel_at="")
        check(s.clean() and s.code == 1 and s.last.get("cancelled") and served_setup.tree(user3) == [],
              "break: Amiga: served: stopped, nothing left (%s)" % s)
        s = served_setup.ask(a.program, ["--install", a.amiga], ask_env)
        check(s.clean() and s.code == 0 and s.last.get("ok"), "Amiga: served: installed (%s)" % s)
        check(served_setup.tree(os.path.join(user3, "install")) == served_setup.tree(os.path.join(user2, "install")),
              "Amiga: served: the same files as the screen's install")
        p, _ = run(a.program, ["--status"], dict(base, CHR_DATA_DIR=user3))
        st = json.loads(p.stdout.strip().splitlines()[-1]) if p.stdout.strip() else {}
        check(st.get("files", {}).get("state") == "ready" and st.get("files", {}).get("data") == "Amiga 2.3",
              "Amiga: served: --status ready")
        os.remove(os.path.join(install, "snd"))
        p, _ = run(a.program, ["--status"], env)
        st = json.loads(p.stdout.strip().splitlines()[-1]) if p.stdout.strip() else {}
        check(st.get("files", {}).get("state") == "incomplete", "Amiga: a data file gone: incomplete")
    else:
        print("skip the Amiga parts: no Amiga copy")

    shutil.rmtree(work, ignore_errors=True)
    print("%s (%d failed)" % ("FAILED" if failures else "passed", failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
