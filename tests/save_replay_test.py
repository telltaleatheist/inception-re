"""The window's loop and the player's saves: chr itself, headless (SDL's dummy
drivers), with its saves in a folder of its own. A game loaded, walked and
saved over must be on disk as it was saved, however often the loop plays the
town again (docs/resuming.md): the overhead map asks it to, many times a
second."""

import argparse
import os
import shutil
import subprocess
import sys

SAVE = "space down*5 enter enter"
LOAD = "space down*4 enter enter"
OVER = "left*3 space down enter enter"
MAP = "space down*8 enter"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--program", required=True)
    ap.add_argument("--game", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--work", required=True)
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)  # the test's own folder
    os.makedirs(work)
    env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy", SDL_RENDER_DRIVER="software",
               CHR_HIDDEN="1", CHR_DATA_DIR=a.data)
    failures = 0

    def check(ok, what):
        nonlocal failures
        print(("ok   " if ok else "FAIL ") + what)
        failures += not ok

    def chr_run(saves, script, *more):
        out = subprocess.run([a.program, "--game-dir", a.game, "--data-dir", a.data, "--save-dir", saves, "--map", "1",
                              "--script", script, *more], env=env, stdin=subprocess.DEVNULL, capture_output=True,
                             text=True, timeout=120)
        check(out.returncode == 0, "chr ran: " + script + " ".join(("",) + more))
        return out.stdout

    def x(saves, name="Game1"):
        b = open(os.path.join(saves, name), "rb").read()
        return b[-4] | b[-3] << 8

    saves = os.path.join(work, "saves")
    chr_run(saves, SAVE)
    check(x(saves) == 0x0C45, "a new game's save")
    first = open(os.path.join(saves, "Game1"), "rb").read()

    for ms in ("1", "400", "1500"):
        with open(os.path.join(saves, "Game1"), "wb") as f:
            f.write(first)
        out = chr_run(saves, " ".join((LOAD, OVER, MAP)), "--loop-ms", ms)
        check("(polling for a key)" in out, "the overhead map is up")
        check(x(saves) == 0x0C42, "loaded, walked, saved over, the loop for %s ms: %04x" % (ms, x(saves)))

    # The next sitting goes on from it.
    chr_run(saves, " ".join((LOAD, OVER, MAP)), "--loop-ms", "400")
    check(x(saves) == 0x0C3F, "and three steps more the next sitting: %04x" % x(saves))

    # Another slot: the first is left alone.
    chr_run(saves, " ".join((LOAD, "left*3 space down enter down enter", MAP)), "--loop-ms", "400")
    check(x(saves) == 0x0C3F and x(saves, "Game2") == 0x0C3C, "saved in another slot")

    # A write that can't be: the folder is where the file should be.
    os.makedirs(os.path.join(saves, "Game3"))
    chr_run(saves, " ".join((LOAD, "left*3 space down enter down*2 enter enter", MAP)), "--loop-ms", "400")
    check(x(saves) == 0x0C3F and sorted(os.listdir(saves)) == ["Game1", "Game2", "Game3"], "a failed write leaves the rest")

    print("%d failures" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
