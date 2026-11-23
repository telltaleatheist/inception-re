"""Fails when any file under a package folder carries a local path or the building user's name
(docs/RELEASING.md): every byte of every file, as ASCII and as UTF-16, so a library's built-in
strings count too. The public GitHub address (github.com/<user>, api.github.com/repos/<user>) is the
one allowed use of the name; a compiler may store that address in overlapping pieces, so the name may
follow the address and a first part of itself. It also fails on any file of the original game, by
name (the game's data files and programs), so no package can carry one.

    python3 -I tools/package/check_paths.py <folder> [extra name ...]
"""
import getpass
import os
import re
import sys

# The original's files by name (docs/original.md): never in a package, whatever folder they are in.
GAME_FILE = re.compile(r"(?i)^(btech\.exe|\$verify\.exe|demofile|wwoodbt\.sif|.*\.(bld|mtp|anm|icn|cmp))$")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    root = sys.argv[1]
    names = {n for n in [getpass.getuser(), os.environ.get("SUDO_USER", ""),
                         os.path.basename(os.path.expanduser("~"))] + sys.argv[2:] if len(n) >= 4}
    words = [b"/Volumes/", b"/Users/", b"/home/", b"C:\\Users\\", b"C:/Users/"] + [n.encode() for n in sorted(names)]
    pat = re.compile(b"|".join(re.escape(w) for w in words))
    pat16 = re.compile(b"|".join(re.escape(w.decode().encode("utf-16-le")) for w in words))
    allowed = re.compile(rb"github\.com/(repos/)?([A-Za-z0-9-]{0,16})$", re.I)

    bad = []
    for d, _, files in os.walk(root):
        for f in files:
            p = os.path.join(d, f)
            if GAME_FILE.match(f):
                bad.append(f"{os.path.relpath(p, root)}: a file of the original game")
                continue
            if os.path.islink(p):
                continue
            with open(p, "rb") as fh:
                data = fh.read()
            for m in pat.finditer(data):
                a = allowed.search(data[max(0, m.start() - 40):m.start()])
                if a and m.group().lower().startswith(a.group(2).lower()):
                    continue
                ctx = data[max(0, m.start() - 30):m.end() + 50]
                bad.append(f"{os.path.relpath(p, root)}: {ctx.decode('latin-1')!r}")
                break
            else:
                if pat16.search(data):
                    bad.append(f"{os.path.relpath(p, root)}: (UTF-16)")
    for b in bad:
        print("not allowed in the package:", b)
    if bad:
        sys.exit(1)
    print(f"check_paths: no local path, user name or game file in {root}")


if __name__ == "__main__":
    main()
