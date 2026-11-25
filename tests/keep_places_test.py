"""The window's loop plays the Town again from the last place it kept, the main
loop, not from the start (docs/resuming.md). Here the rig's own key scripts
(tools/reference/check.py; nothing of the original is run) are played once
straight through, and once as the window plays them, with the place kept
wherever it can be (--keep-places). Everything must be the same: the log, the
state, the events and the sounds played, the last screen and the saves."""

import argparse
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "reference"))
import check as rig  # noqa: E402

NONE = ("demo",)  # it never reaches the main loop
A_SAVE = ("save_startup",)  # it loads one: a new game's here, made by chr


def play(a, work, sequence, kept):
    name, short, more, options = sequence
    cases = short + more
    folder = os.path.join(work, name, "kept" if kept else "straight")
    os.makedirs(os.path.join(folder, "saves"))
    env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy", SDL_RENDER_DRIVER="software",
               CHR_HIDDEN="1", CHR_DATA_DIR=a.data)
    run = [a.program, "--game-dir", a.game, "--data-dir", a.data, "--save-dir", os.path.join(folder, "saves"),
           "--script", rig.chr_script(cases, len(cases) - 1), "--state", "--fight-state", "--events", "--sound-log",
           "--screenshot", os.path.join(folder, "screen.bmp"), *options]
    if name in A_SAVE:
        subprocess.run(run[:7] + ["--map", "1", "--script", "space down*5 enter enter"], env=env, stdin=subprocess.DEVNULL,
                       capture_output=True, timeout=120, check=True)
    r = subprocess.run(run + (["--keep-places"] if kept else []), env=env, stdin=subprocess.DEVNULL, capture_output=True,
                       timeout=900)
    files = {}
    for d, _, names in os.walk(folder):
        for n in names:
            with open(os.path.join(d, n), "rb") as f:
                files[os.path.relpath(os.path.join(d, n), folder)] = f.read()
    return r.returncode, r.stdout, r.stderr, files


def window(a, work):
    """The window's own loop, keys pressed in it as time goes by (so not the same game twice): what the Town showed is
    played once, and each sound started once, however often the place was kept. The script's are in the totals: it ends
    before the second of two visits, with all of the first's shown and played, and the loop goes in again."""
    name, short, more, options = next(s for s in rig.SEQUENCES if s[0] == "spot")
    cases = short + more
    items = rig.chr_script(cases, len(cases) - 1).split()
    at = items.index("idle:2900") + 1
    saves = os.path.join(work, "window-saves")
    os.makedirs(saves)
    env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy", SDL_RENDER_DRIVER="software",
               CHR_HIDDEN="1", CHR_DATA_DIR=a.data)
    out = subprocess.run([a.program, "--game-dir", a.game, "--data-dir", a.data, "--save-dir", saves, "--script",
                          " ".join(items[:at]), "--events", "--loop-keys", " ".join(i for i in items[at:] if ":" not in i),
                          "--loop-ms", "60000", "--loop-resets", *options], env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                         timeout=200)
    said = re.search(r"loop (\d+) places, (\d+) of (\d+) events played, (\d+) of (\d+) sounds started, (\d+) keys left", out.stdout)
    again = re.search(r"loop (\d+) resets, (\d+) textures now", out.stdout)
    if not said or not again or out.returncode != 0:
        return False, "exit %s: %s" % (out.returncode, (out.stdout + out.stderr)[-200:])
    places, played, shown, started, sounds, left = map(int, said.groups())
    first, first_sounds = out.stdout.count("\nevent "), out.stdout.count("\nevent sound")
    # SDL's event for a device reset comes between the keys, and the textures are made again after the last.
    return (int(again[1]) > 3 and int(again[2]) > 0 and places > 3 and played == shown > first > 0 and started == sounds > first_sounds > 0 and left == 0,
            "%s; the script's %d and %d; %s" % (said[0], first, first_sounds, again[0]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--program", required=True)
    ap.add_argument("--game", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--all", action="store_true", help="every sequence (long)")
    ap.add_argument("sequences", nargs="*")
    a = ap.parse_args()
    work = os.path.abspath(a.work)
    shutil.rmtree(work, ignore_errors=True)  # the test's own folder
    os.makedirs(work)
    chosen = [s for s in rig.SEQUENCES if a.all or s[0] in a.sequences]
    if not chosen or (a.sequences and len(chosen) != len(set(a.sequences))):
        sys.exit("no such sequence")
    failures = 0

    def both(sequence):
        return sequence[0], play(a, work, sequence, False), play(a, work, sequence, True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(2, (os.cpu_count() or 4) // 2)) as pool:
        for name, straight, kept in pool.map(both, chosen):
            wrong = []
            if straight[0] != 0 or kept[0] != 0:
                wrong.append("exit %s and %s: %s" % (straight[0], kept[0], (straight[2] + kept[2]).decode(errors="replace")[-200:]))
            if straight[1] != kept[1]:
                one, other = straight[1].splitlines(), kept[1].splitlines()
                at = next((i for i, (x, y) in enumerate(zip(one, other)) if x != y), min(len(one), len(other)))
                wrong.append("output from line %d of %d, %d" % (at + 1, len(one), len(other)))
            said = re.match(rb"chr: (\d+) places kept\r?\n", kept[2])
            places = int(said[1]) if said else 0
            if not said or straight[2] != kept[2][said.end():]:
                wrong.append("stderr")
            if places == 0 and name not in NONE:
                wrong.append("no place was kept")
            if straight[3] != kept[3]:
                wrong.append("files " + " ".join(sorted(n for n in {*straight[3], *kept[3]} if straight[3].get(n) != kept[3].get(n))))
            # A test that compares nothing says so.
            if b"state random" not in straight[1] or "screen.bmp" not in straight[3]:
                wrong.append("nothing to compare")
            print("%s %s: %d places, %d lines, %d files%s" % ("FAIL" if wrong else "ok  ", name, places, len(straight[1].splitlines()),
                                                   len(straight[3]), "".join("; " + w for w in wrong)), flush=True)
            failures += bool(wrong)
    ok, words = window(a, work)
    print(("ok   " if ok else "FAIL ") + "the window's loop: " + words)
    failures += not ok
    print("%d sequences and the window's loop, %d failures" % (len(chosen), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
