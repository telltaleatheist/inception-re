"""Names with letters outside ASCII on Windows: what the program does with them, with the system's code page, with
UTF-8 as the program's (a manifest) and with Japanese, side by side.

    probe.py PROGRAMS_DIR SCRATCH_DIR

Every verdict is a look at the disk by Python, whose names are wide; what a program says is only noted beside it. The
copies of the game are made up (tools/package/served_setup.py). Nothing is changed outside SCRATCH_DIR, but for a key
of the player's registry, taken away again, and the player folder SDL makes when asked for it; so on Windows it runs
on a runner alone (GITHUB_ACTIONS). It fails only when the plain name's cases do: then nothing here means anything.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "package"))
import served_setup  # noqa: E402

PAGES = ("legacy", "utf8", "ja")
# Plain ASCII, the control; in Windows-1252; in no one code page, and in Japanese's some of its UTF-8 is no text.
NAMES = {"plain": "Plain name", "latin": "Zoë é", "other": "Ł łódź 日本 x"}
SECONDS = 60
WINDOWS = os.name == "nt"
EXE = ".exe" if WINDOWS else ""
REG_KEY = "Software\\chr-names-probe"


def shown(text):
    return ascii(text)[1:-1]


def hidden(**more):
    env = {k: v for k, v in os.environ.items() if not k.startswith(("CHR_", "REKIT_", "PROBE_"))}
    env.update(CHR_HIDDEN="1", SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy", SDL_RENDER_DRIVER="software", **more)
    return env


def started(program, args, env):
    """The exit code in hex ("no end" when it had to be stopped), stdout and stderr as bytes."""
    try:
        r = subprocess.run([str(program)] + [str(a) for a in args], env=env, stdin=subprocess.DEVNULL,
                           capture_output=True, timeout=SECONDS)
    except subprocess.TimeoutExpired as e:
        return "no end", e.stdout or b"", e.stderr or b""
    except OSError as e:
        return "no start", b"", str(e).encode("utf-8", "replace")
    return ("%#x" % (r.returncode & 0xffffffff)) if r.returncode else "0", r.stdout, r.stderr


def everything(folder):
    out = set()
    for d, dirs, files in os.walk(str(folder)):
        out |= {os.path.relpath(os.path.join(d, n), str(folder)) for n in dirs + files}
    return out


class Probe:
    def __init__(self, programs, scratch):
        self.programs, self.scratch, self.number, self.rows = programs, scratch, 0, []
        self.pages = {}

    def program(self, kind, page):
        return self.programs / ("probe_%s_%s%s" % (kind, page, EXE))

    def place(self):
        """A folder of the case's own, with a plain name."""
        self.number += 1
        folder = self.scratch / ("%03d" % self.number)
        folder.mkdir(parents=True)
        return folder

    def case(self, what, name, page, run):
        """`run(place, name)` -> (wanted paths below place, allowed others, exit, words)."""
        place = self.place()
        try:
            wanted, allowed, code, words = run(place, NAMES[name], page)
            now = everything(place)
            strays = sorted(p for p in now if p not in wanted and not any(
                p == a or p.startswith(a + os.sep) or a.startswith(p + os.sep) for a in list(allowed) + list(wanted)))
            missing = sorted(w for w in wanted if w not in now)
            verdict = "ok" if not missing and not strays else "WRONG"
            note = "; ".join((["missing " + shown(m) for m in missing] + ["stray " + shown(s) for s in strays])[:4])
        except Exception as e:  # the case itself went wrong: never taken for a verdict
            verdict, code, note, words = "CASE BROKE", "", shown(repr(e)), ""
        self.rows.append((what, name, page, verdict, code, note, words))
        print("%-40s %-6s %-7s %-10s exit %-10s %s%s" % (what, name, page, verdict, code, note,
                                                         "\n      says: " + words if words else ""), flush=True)


# --- the cases: chi's ---

def last_words(out, err):
    text = (out.decode("utf-8", "replace").strip().splitlines() or [""])[-1]
    errors = (err.decode("utf-8", "replace").strip().splitlines() or [""])[-1]
    return shown(" | ".join(t for t in (text[:230], errors[:160]) if t))


def installed(user):
    return {user + os.sep + "settings.ini", user + os.sep + os.path.join("install", "MAP1.MTP")}


def chi_cases(probe):
    def data_dir(place, name, page):
        (place / name).mkdir()
        copy = served_setup.made_up_copy(place / "copy")
        code, out, err = started(probe.program("chr", page), ["--data-dir", place / name, "--setup", "--install", copy], hidden())
        return installed(name), {"copy", name}, code, last_words(out, err)

    def env_dir(place, name, page):
        (place / name).mkdir()
        copy = served_setup.made_up_copy(place / "copy")
        code, out, err = started(probe.program("chr", page), ["--setup", "--install", copy], hidden(CHR_DATA_DIR=str(place / name)))
        return installed(name), {"copy", name}, code, last_words(out, err)

    def copy_in(place, name, page):
        copy = served_setup.made_up_copy(place / name)
        code, out, err = started(probe.program("chr", page), ["--setup", "--install", copy], hidden(CHR_DATA_DIR=str(place / "user")))
        return installed("user"), {name, "user"}, code, last_words(out, err)

    def into_folder(place, name, page):
        copy = served_setup.made_up_copy(place / "copy")
        code, out, err = started(probe.program("chr", page), ["--setup", "--install", copy, "--folder", place / name],
                                 hidden(CHR_DATA_DIR=str(place / "user")))
        return {name + os.sep + os.path.join("install", "MAP1.MTP"), "user" + os.sep + "settings.ini"}, \
            {"copy", "user", name}, code, last_words(out, err)

    def odd_file(place, name, page):
        copy = served_setup.made_up_copy(place / "copy")
        (copy / (name + ".txt")).write_text("a file of the player's")
        code, out, err = started(probe.program("chr", page), ["--setup", "--install", copy], hidden(CHR_DATA_DIR=str(place / "user")))
        return installed("user"), {"copy", "user"}, code, last_words(out, err)

    def unpacked(place, name, page):
        stage(probe.programs, place / name, probe.program("chr", page))
        copy = served_setup.made_up_copy(place / "copy")
        code, out, err = started(place / name / ("chi" + EXE), ["--setup", "--install", copy], hidden(CHR_DATA_DIR=str(place / "user")))
        return installed("user"), {"copy", "user", name}, code, last_words(out, err)

    def player_folder(place, name, page):
        """A start with nothing on the command line, SDL's player folder being the named one (made, as SDL makes it)."""
        folder = place / name / "BattleTech" / "Inception Rebuild"
        folder.mkdir(parents=True)
        given = (str(folder) + os.sep).encode("utf-8").hex()
        code, out, err = started(probe.program("pref", page), [], hidden(PROBE_PREF=given))
        return {os.path.join(name, "BattleTech", "Inception Rebuild", "chi.log")}, {name}, code, last_words(out, err)

    for what, run in [("--data-dir, then --install", data_dir), ("CHR_DATA_DIR, then --install", env_dir),
                      ("the copy's folder", copy_in), ("--folder", into_folder), ("a file beside the game", odd_file),
                      ("the program's own folder", unpacked), ("the player folder, main.cpp:407", player_folder)]:
        for name in NAMES:
            for page in PAGES:
                if probe.pages.get(page):
                    probe.case(what, name, page, run)


# --- the cases: each way in, each way to the disk ---

def path_cases(probe):
    def way(source, look):
        def run(place, name, page):
            folder = place / name
            folder.mkdir()
            program, env, args = probe.program("path", page), hidden(), [source, folder]
            if source == "env":
                env, args = hidden(PROBE_FOLDER=str(folder)), ["env", "PROBE_FOLDER"]
            elif source == "utf8":
                args = ["utf8", str(folder).encode("utf-8").hex()]
            elif source == "reg":
                import winreg
                with winreg.CreateKey(winreg.HKEY_CURRENT_USER, REG_KEY) as key:
                    winreg.SetValueEx(key, "folder", 0, winreg.REG_SZ, str(folder))
                args = ["reg", REG_KEY]
            try:
                code, out, err = started(program, args, env)
            finally:
                if source == "reg":
                    winreg.DeleteKey(winreg.HKEY_CURRENT_USER, REG_KEY)
            said = json.loads(out.decode("ascii")) if out.strip() else {}
            threw = {k: v["threw"] for k, v in said.items() if isinstance(v, dict)}
            # Only `look` is this row's; the others' files may be anywhere below.
            others = {f for f in everything(place) if os.path.basename(f) in {"Game1", "fopen.txt", "sdl.txt"}
                      and os.path.basename(f) != look}
            return {os.path.join(name, look)}, others | {name}, code, shown(json.dumps(threw)) if threw else ""
        return run

    sources = ["argv", "env", "utf8"] + (["reg"] if WINDOWS else [])
    named = {"argv": "argv", "env": "std::getenv", "utf8": "UTF-8 bytes (as SDL's)", "reg": "RegGetValueA"}
    for source in sources:
        for look, how in (("Game1", "SaveFiles"), ("fopen.txt", "fopen(string())"), ("sdl.txt", "SDL(string())")):
            for name in NAMES:
                for page in PAGES:
                    if probe.pages.get(page):
                        probe.case("%s -> %s" % (named[source], how), name, page, way(source, look))


def base_cases(probe):
    """The program's folder: its own data/text/interface.json is there; does SDL_GetBasePath's text lead to it?"""
    for name in NAMES:
        for page in PAGES:
            if not probe.pages.get(page):
                continue
            place = probe.place()
            stage(probe.programs, place / NAMES[name], probe.program("path", page))
            code, out, err = started(place / NAMES[name] / ("chi" + EXE), ["info"], hidden())
            there = (place / NAMES[name] / "data/text/interface.json").is_file()
            base = json.loads(out.decode("ascii")).get("base", {}) if out.strip() else {}
            verdict = "CASE BROKE" if not there or not base else "ok" if base.get("interface") else "WRONG"
            note = base.get("threw", "")
            probe.rows.append(("SDL_GetBasePath -> interface.json", name, page, verdict, code, note, ""))
            print("%-40s %-6s %-7s %-10s exit %-10s %s" % ("SDL_GetBasePath -> interface.json", name, page, verdict, code, note), flush=True)


def stage(programs, folder, program):
    """A package's layout with `program` as chi."""
    folder.mkdir(parents=True)
    shutil.copy2(program, folder / ("chi" + EXE))
    shutil.copytree(programs / "data", folder / "data")
    for library in programs.glob("SDL3.dll"):
        shutil.copy2(library, folder / library.name)


# --- the table ---

def table(probe):
    cells, whats = {}, []
    for what, name, page, verdict, code, _, _ in probe.rows:
        cells[what, name, page] = verdict if verdict != "WRONG" else "WRONG (%s)" % code
        if what not in whats:
            whats.append(what)
    lines = ["| case | name | " + " | ".join(PAGES) + " |", "|---|---|" + "---|" * len(PAGES)]
    for what in whats:
        for name in NAMES:
            lines.append("| %s | %s | %s |" % (what, name, " | ".join(cells.get((what, name, p), "not run") for p in PAGES)))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("programs", type=Path, help="the folder of the probe's programs, data/ and (on Windows) SDL3.dll beside them")
    parser.add_argument("scratch", type=Path)
    options = parser.parse_args()
    if WINDOWS and os.environ.get("GITHUB_ACTIONS") != "true":
        sys.exit("On Windows this runs on a runner only.")
    scratch = options.scratch.resolve()
    if scratch.exists():
        sys.exit("%s is there already" % scratch)
    if WINDOWS:  # a program that ends badly shows no box (its children take this over)
        import ctypes
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    probe = Probe(options.programs.resolve(), scratch)
    print("names: " + ", ".join("%s = %s" % (k, shown(v)) for k, v in NAMES.items()))
    for page in PAGES:
        code, out, err = started(probe.program("path", page), ["info"], hidden())
        said = json.loads(out.decode("ascii")) if out.strip() else {}
        wanted = {"legacy": said.get("system_acp"), "utf8": 65001, "ja": 932}[page]
        probe.pages[page] = bool(said) and (not WINDOWS or said.get("acp") == wanted)
        print("%-7s exit %s, code page %s (system %s, OEM %s, file APIs ANSI %s)%s" % (
            page, code, said.get("acp"), said.get("system_acp"), said.get("oemcp"), said.get("file_apis_ansi"),
            "" if probe.pages[page] else ": NOT the one asked for, its cases are not run"))
        if page == "legacy":
            print("        SDL_GetPrefPath: " + bytes.fromhex(said.get("pref", {}).get("bytes", "")).decode("ascii", "backslashreplace"))
    print(flush=True)
    chi_cases(probe)
    base_cases(probe)
    path_cases(probe)
    text = table(probe)
    print("\n" + text)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write(text + "\n")
    control = [r for r in probe.rows if r[1] == "plain"]
    bad = [r for r in control if r[3] != "ok"] + [r for r in probe.rows if r[3] == "CASE BROKE"]
    if bad or not control or not probe.pages["legacy"] or not probe.pages["utf8"]:
        print("\nTHE CONTROL FAILED: nothing above means anything (%d bad of %d plain rows, or a code page missing)"
              % (len(bad), len(control)))
        sys.exit(1)
    print("\nThe control passed: %d plain rows, all ok. %d rows in all, %d wrong." % (
        len(control), len(probe.rows), sum(r[3] == "WRONG" for r in probe.rows)))


if __name__ == "__main__":
    main()
