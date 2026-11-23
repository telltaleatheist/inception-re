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
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

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


def same(a, b):
    return os.path.normcase(os.path.normpath(a)) == os.path.normcase(os.path.normpath(b))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--program", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--game", default="")
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

    # The setup screen, hidden: the rebuild's header; no picker (unattended), a check's answer stands in.
    screen = dict(env, REKIT_SETUP_FRAMES="5", REKIT_SETUP_FIND="1", REKIT_SETUP_GO="1",
                  REKIT_SETUP_PICKER="path:" + os.path.join(notgame, "README.TXT"))
    p, _ = run(a.program, ["--setup"], screen)
    out = p.stdout.splitlines()
    check(p.returncode == 1, "break: the screen with a pick that is not the game ends cancelled (%d)" % p.returncode)
    check("setup: header INCEPTION REBUILD" in out, "the screen's log: the rebuild's header")
    check(not any(l.startswith("setup: screen confirm") for l in out), "break: not the game, not offered")
    check(not os.path.exists(os.path.join(user, "install")), "break: nothing installed from the screen either")

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

        # The same copy through the setup screen (hidden): found, offered, installed; and into a picked folder.
        user = os.path.join(work, "user-c")
        env = dict(base, CHR_DATA_DIR=user)
        screen = dict(env, REKIT_SETUP_FRAMES="200", REKIT_SETUP_FIND="1", REKIT_SETUP_GO="1",
                      REKIT_SETUP_PICKER="path:" + os.path.join(pick, btech))
        p, _ = run(a.program, ["--setup"], screen)
        out = p.stdout.splitlines()
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

    shutil.rmtree(work, ignore_errors=True)
    print("%s (%d failed)" % ("FAILED" if failures else "passed", failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
