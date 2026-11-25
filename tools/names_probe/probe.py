"""Names with letters outside ASCII on Windows: what the program does with them, with the system's code page, with
UTF-8 as the program's (a manifest) and with Japanese, side by side.

    probe.py PROGRAMS_DIR SCRATCH_DIR

A verdict of chi_cases, base_cases and path_cases is a look at the disk by Python, whose names are wide; what a
program says is only noted beside it. The search and the remembered folder leave nothing on the disk: theirs are what
the program answered (search_cases, settings_cases, left_cases). The copies of the game are made up
(tools/package/served_setup.py). Nothing is changed outside SCRATCH_DIR, but for a key of the player's registry, taken
away again, and the player folder SDL makes when asked for it; so on Windows it runs on a runner alone
(GITHUB_ACTIONS). It fails only when the plain name's cases do: then nothing here means anything.
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
# More, for the files a search passes: an emoji, outside 16 bits, and half of one, which is no text at all.
SEARCH_NAMES = dict(NAMES, emoji="Emoji \U0001f600", lone="Half \ud83d x")
SECONDS = 60
FRAMES = 3  # of the hidden setup screen, then it ends itself
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
        self.pages, self.old = {}, "cp1252"

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
        self.report(what, name, page, verdict, code, note, words)

    def told(self, what, name, page, run):
        """`run(place)` -> (verdict, exit, note, words): the program's own answer."""
        place = self.place()
        try:
            row = run(place)
        except Exception as e:
            row = "CASE BROKE", "", shown(repr(e)), ""
        shutil.rmtree(place / "above", ignore_errors=True)  # some megabytes, hundreds of times
        self.report(what, name, page, *row)

    def report(self, what, name, page, verdict, code, note, words):
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


# --- the cases: the search's ---

class NotMade(Exception):
    pass


def enclosed(place, **more):
    """No search outside `place`: the system drive, the home and so on are a folder in it that isn't there."""
    nowhere = str(place / "nowhere")
    env = hidden(CHR_DATA_DIR=str(place / "user"), REKIT_SETUP_FRAMES=str(FRAMES), **more)
    env = {k: v for k, v in env.items() if k.upper() not in ("APPIMAGE", "WINEPREFIX", "SYSTEMDRIVE", "PROGRAMFILES",
                                                             "PROGRAMFILES(X86)", "PROGRAMW6432")}
    env.update(dict.fromkeys(("SystemDrive", "ProgramFiles", "ProgramFiles(x86)", "ProgramW6432") if WINDOWS else ("HOME",), nowhere))
    return env


def text_of(value, probe):
    """What the program wrote: as UTF-8 if it is, else as the old code page's, and which."""
    try:
        return value.decode("utf-8"), ""
    except UnicodeDecodeError:
        return value.decode(probe.old, "backslashreplace"), "not UTF-8"


def same(a, b):
    return bool(a) and os.path.exists(a) and os.path.samefile(a, b)


def ended(code):
    return "RUNNING" if code == "no end" else "ENDED"


def plain_start(probe, program, place, copy, remembered=False):
    """Reached the setup: the log's line for the screen's last frame, and the exit of a cancelled setup. A program that
    had to be stopped is RUNNING, one that ended without that line is ENDED; neither counts as reaching it."""
    log = place / "user" / "chi.log"
    env = enclosed(place)
    if remembered:  # as a player starts it: the folder from SDL (pref_folder.cpp's), nothing in the environment
        del env["CHR_DATA_DIR"]
        env["PROBE_PREF"] = (str(place / "user") + os.sep).encode("utf-8").hex()
    code, out, err = started(program, [], env)
    written = log.is_file()
    said, _ = text_of(log.read_bytes() if written else b"", probe)
    lines = said.splitlines()
    reached = "setup: frames %d: ended" % FRAMES in lines and "setup: cancelled" in lines and code == "0"
    # The screen's own "game files into" is where they would go.
    used = any(line.startswith("setup: game files ") and not line.startswith("setup: game files into ") for line in lines)
    plates = [line for line in lines if line.startswith("setup: found plate: ")]
    words = "log %s; %s" % ("%d lines" % len(lines) if written else "not written",
                            shown(" | ".join(([lines[-1][:200]] if lines else []) + [err.decode("utf-8", "replace")[:160]])))
    if remembered:  # the made-up copy can't be played: it is used, and the start ends with the reason (1)
        return "ok" if used and code == "0x1" else "ASKED" if reached else ended(code), code, "", words
    if used:
        raise RuntimeError("a copy nobody named was used: " + words)
    if not reached:
        return ended(code), code, "", words
    if not copy:
        if plates:
            raise RuntimeError("a copy from outside the case: " + plates[0])
        return "ok", code, "", words
    if not plates:
        return "HIDDEN", code, "the setup, but the copy isn't offered", words
    if copy.name not in plates[0]:
        raise RuntimeError("a copy from outside the case: " + plates[0])
    return "ok", code, "", words


def asked(probe, program, place, args, copy):
    """--status's and --setup --search's answer: one line of JSON, the copy's folder in it (files.where, path) or none."""
    code, out, err = started(program, args, enclosed(place))
    log = place / "user" / "chi.log"
    words = shown(" | ".join(b.decode("utf-8", "replace").strip()[:200] for b in (out, err) if b.strip()))
    if log.exists():
        words = "log written; " + words
    text, odd = text_of(out, probe)
    try:
        answer = json.loads(text)
    except ValueError:
        answer = None
    if code != "0" or not isinstance(answer, dict):
        return ended(code) if code != "0" else "WRONG", code, "no answer" if code == "0" else "", words
    where = (answer["files"].get("where") if args == ["--status"] else answer.get("path")) or ""
    if where and not Path(where).resolve().is_relative_to(place):
        raise RuntimeError("a copy from outside the case: " + where)
    if not copy:
        return ("WRONG" if where or answer.get("ok") is False else "ok"), code, odd, words
    if not where:
        return "HIDDEN", code, "answered, but not the copy", words
    right = same(where, copy)
    return "ok" if right and not odd else "WRONG", code, odd or ("" if right else "another folder"), words


def search_cases(probe):
    """The search of a first start, with a player's own file or folder of each name in its way. It reads %SystemDrive%'s
    BattleTech and so on (a folder that isn't there here), chi's folder and the one above with their folders, each two
    levels down; no registry key, no Program Files, and Downloads only if chi is in it."""
    def file_in(*where):
        def make(above, name):
            folder = above.joinpath(*where)
            folder.mkdir(exist_ok=True)
            made(folder, name, lambda p: p.write_bytes(b"a file of the player's"))
        return make

    def folder_in(above, name):
        made(above, name, lambda p: p.mkdir())

    def made(folder, name, make):
        try:
            make(folder / name)
        except (OSError, UnicodeError) as e:
            raise NotMade(repr(e))
        if name not in os.listdir(folder):
            raise NotMade("listed under another name")

    # "a folder" is before the copy in the search's order, "z folder" after it.
    places = [("a file in chi's folder", file_in("chi folder")), ("a file in the folder above", file_in()),
              ("a file in a folder beside chi's", file_in("a folder")), ("a file in one past the copy", file_in("z folder")),
              ("a folder in the folder above", folder_in)]
    runs = [("plain start", lambda probe, p, place, copy: plain_start(probe, p, place, copy)),
            ("--status", lambda probe, p, place, copy: asked(probe, p, place, ["--status"], copy)),
            ("--setup --search", lambda probe, p, place, copy: asked(probe, p, place, ["--setup", "--search"], copy))]
    for where, make in places:
        for with_copy in (False, True):
            for what, run in runs:
                for name in SEARCH_NAMES:
                    for page in ("legacy", "utf8"):
                        def case(place):
                            above = place / "above"
                            (place / "user").mkdir()
                            stage(probe.programs, above / "chi folder", probe.program("chr", page))
                            copy = served_setup.made_up_copy(above / "copy") if with_copy else None
                            try:
                                make(above, SEARCH_NAMES[name])
                            except NotMade as e:
                                return "NOT MADE", "", shown(str(e)), ""
                            return run(probe, above / "chi folder" / ("chi" + EXE), place, copy)
                        probe.told("%s, %s%s" % (where, what, ", a copy there" if with_copy else ""), name, page, case)


# --- the cases: the remembered folder's ---

def settings_cases(probe):
    """A start with settings.ini naming a copy that is there: in the old code page's bytes (what 1.0.0 wrote for a folder
    from the command line), in UTF-8 (what it wrote for the dialog's), and in UTF-8 with the copy where 1.0.0 then put
    it. The player folder's own name is plain."""
    def old(name):
        return name.encode(probe.old)

    def utf8(name):
        return name.encode("utf-8")

    def misread(name):
        return name.encode("utf-8").decode(probe.old)

    def itself(name):
        return name

    rows = [("settings.ini in the old page", old, itself, ("plain", "latin")), ("settings.ini in UTF-8", utf8, itself, tuple(NAMES)),
            ("settings.ini in UTF-8, the misread folder", utf8, misread, ("plain", "latin"))]
    for what, written, folder, names in rows:
        for run in ("plain start", "--status"):
            for name in names:
                for page in ("legacy", "utf8"):
                    def case(place):
                        (place / "user").mkdir()
                        (place / "games").mkdir()
                        copy = served_setup.made_up_copy(place / "games" / folder(NAMES[name]))
                        start = (str(place / "games") + os.sep).encode("ascii")
                        (place / "user" / "settings.ini").write_bytes(b"game_folder=" + start + written(NAMES[name]) + b"\n")
                        chi = place / "above" / "chi folder"
                        stage(probe.programs, chi, probe.program("pref" if run == "plain start" else "chr", page))
                        if run == "plain start":
                            return plain_start(probe, chi / ("chi" + EXE), place, copy, True)
                        return asked(probe, chi / ("chi" + EXE), place, ["--status"], copy)
                    probe.told("%s, %s" % (what, run), name, page, case)


def left_cases(probe):
    """A start whose player folder 1.0.0 misread, with a saved game under the misread name: it is that folder, or the log
    says where it is; and it stays where it was."""
    for name in ("plain", "latin"):
        for page in ("legacy", "utf8"):
            def case(place):
                (place / "user").mkdir()
                right = place / "user" / NAMES[name] / "BattleTech" / "Inception Rebuild"
                left = place / "user" / NAMES[name].encode("utf-8").decode(probe.old) / "BattleTech" / "Inception Rebuild"
                left.mkdir(parents=True)
                right.mkdir(parents=True, exist_ok=True)
                (left / "Game1").write_bytes(b"a saved game")
                chi = place / "above" / "chi folder"
                stage(probe.programs, chi, probe.program("pref", page))
                env = enclosed(place)
                del env["CHR_DATA_DIR"]
                env["PROBE_PREF"] = (str(right) + os.sep).encode("utf-8").hex()
                code, out, err = started(chi / ("chi" + EXE), [], env)
                logs = [f / "chi.log" for f in (left, right) if (f / "chi.log").is_file()]
                said = logs[0].read_bytes().decode("utf-8", "replace") if logs else ""
                lines = [line for line in said.splitlines() if "Saved games from version 1.0.0" in line]
                words = shown(" | ".join(lines or said.splitlines()[-1:]))
                if not logs or code != "0":
                    return ended(code), code, "", words
                if (left / "Game1").read_bytes() != b"a saved game" or (left != right and (right / "Game1").exists()):
                    return "WRONG", code, "moved", words
                if logs[0].parent == left:  # its own folder: nothing to say
                    return ("WRONG" if lines else "ok"), code, "", words
                return ("ok" if lines and str(left) in lines[0] else "HIDDEN"), code, "", words
            probe.told("saves 1.0.0 left, the player folder", name, page, case)


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
        cells[what, name, page] = verdict if verdict in ("ok", "NOT MADE", "CASE BROKE") else "%s (%s)" % (verdict, code)
        if what not in whats:
            whats.append(what)
    lines = ["| case | name | " + " | ".join(PAGES) + " |", "|---|---|" + "---|" * len(PAGES)]
    for what in whats:
        for name in [n for n in SEARCH_NAMES if any((what, n, p) in cells for p in PAGES)]:
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
    print("names: " + ", ".join("%s = %s" % (k, shown(v)) for k, v in SEARCH_NAMES.items()))
    for page in PAGES:
        code, out, err = started(probe.program("path", page), ["info"], hidden())
        said = json.loads(out.decode("ascii")) if out.strip() else {}
        wanted = {"legacy": said.get("system_acp"), "utf8": 65001, "ja": 932}[page]
        probe.pages[page] = bool(said) and (not WINDOWS or said.get("acp") == wanted)
        print("%-7s exit %s, code page %s (system %s, OEM %s, file APIs ANSI %s)%s" % (
            page, code, said.get("acp"), said.get("system_acp"), said.get("oemcp"), said.get("file_apis_ansi"),
            "" if probe.pages[page] else ": NOT the one asked for, its cases are not run"))
        if page == "legacy":
            if WINDOWS and said:
                probe.old = "cp%d" % said["system_acp"]
            print("        SDL_GetPrefPath: " + bytes.fromhex(said.get("pref", {}).get("bytes", "")).decode("ascii", "backslashreplace"))
    print(flush=True)
    chi_cases(probe)
    base_cases(probe)
    path_cases(probe)
    search_cases(probe)
    settings_cases(probe)
    left_cases(probe)
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
    print("\nThe control passed: %d plain rows, all ok. %d rows in all, %d not ok." % (
        len(control), len(probe.rows), sum(r[3] not in ("ok", "NOT MADE") for r in probe.rows)))


if __name__ == "__main__":
    main()
