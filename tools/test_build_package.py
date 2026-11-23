"""The packaging's own parts, without a build, SDL or the game's files:

    python3 -I -m unittest discover -s tools -p 'test_build_package.py' -v
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent / "package"))
import build_package  # noqa: E402
import sums  # noqa: E402

CHECK_PATHS = Path(__file__).resolve().parent / "package/check_paths.py"


def write(path, text="fixture"):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


class Names(unittest.TestCase):
    def test_a_release_tag_reports_its_number(self):
        self.assertEqual(build_package.program_version("chi-v1.2.3"), "1.2.3")
        self.assertEqual(build_package.program_version("chi-v1.2.3-test"), "chi-v1.2.3-test")
        self.assertEqual(build_package.program_version("nightly"), "nightly")

    def test_a_bad_label_is_refused_before_anything_is_built(self):
        with self.assertRaises(ValueError):
            build_package.package(Path("."), Path("b"), Path("s"), Path("d"), "../x")


class Staging(unittest.TestCase):
    def fixture(self, root):
        for name in ["LICENSE", "data/text/interface.json", "sdl/LICENSE.txt", "extern/re-setup-kit/LICENSE",
                     "extern/re-setup-kit/third_party/stb/LICENSE", "extern/re-setup-kit/third_party/miniz/LICENSE"]:
            write(root / name)
        prefix = root / "deps/prefix"
        for name in build_package.ARCHIVE_LICENCES:
            write(prefix / "share/licenses" / name, "fixture licence")
        write(root / "build/CMakeCache.txt", f"REKIT_LIBARCHIVE_LIB:FILEPATH={prefix / 'lib/libarchive.a'}\n")

    def test_the_stage_holds_our_data_the_read_me_and_every_licence(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            self.fixture(root)
            out = root / "stage"
            out.mkdir()
            build_package.stage_common(root, root / "build", root / "sdl", out)
            self.assertTrue((out / "data/text/interface.json").is_file())
            self.assertTrue((out / "LICENSE").is_file() and (out / "README.txt").is_file())
            for name in ["SDL-LICENSE.txt", "re-setup-kit-LICENSE", "stb-LICENSE", "miniz-LICENSE",
                         *build_package.ARCHIVE_LICENCES.values()]:
                self.assertTrue((out / "licenses" / name).is_file(), name)
            readme = (out / "README.txt").read_text()
            for word in ("Claude", "Anthropic", " AI "):
                self.assertNotIn(word, readme)

    def test_a_missing_kit_licence_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            self.fixture(root)
            (root / "extern/re-setup-kit/third_party/stb/LICENSE").unlink()
            (root / "stage").mkdir()
            with self.assertRaisesRegex(RuntimeError, "Missing licence"):
                build_package.stage_common(root, root / "build", root / "sdl", root / "stage")

    def test_a_shared_libarchive_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            self.fixture(root)
            write(root / "build/CMakeCache.txt", "REKIT_LIBARCHIVE_LIB:FILEPATH=/opt/x/lib/libarchive.dylib\n")
            (root / "stage").mkdir()
            with self.assertRaisesRegex(RuntimeError, "static libarchive"):
                build_package.stage_common(root, root / "build", root / "sdl", root / "stage")


class Archives(unittest.TestCase):
    def test_the_windows_zip_has_the_program_at_its_top(self):
        with tempfile.TemporaryDirectory() as d:
            stage = Path(d) / "CHI-Rebuild-x-win64"
            for name in ["chi.exe", "SDL3.dll", "data/text/interface.json", "licenses/SDL-LICENSE.txt"]:
                write(stage / name)
            archive = Path(d) / "x.zip"
            build_package.zip_flat(stage, archive)
            names = zipfile.ZipFile(archive).namelist()
            self.assertIn("chi.exe", names)
            self.assertIn("SDL3.dll", names)
            self.assertIn("data/text/interface.json", names)
            self.assertFalse(any(n.startswith("CHI-Rebuild") for n in names))

    def test_sha256sums_lists_every_file_but_the_notes(self):
        with tempfile.TemporaryDirectory() as d:
            folder = Path(d)
            for name in ["b.dmg", "a.zip", "a.zip.sha256", "SHA256SUMS"]:
                write(folder / name, name)
            lines = sums.sums(folder)
            self.assertEqual([l.split("  ")[1].strip() for l in lines], ["a.zip", "b.dmg"])
            self.assertTrue(all(len(l.split("  ")[0]) == 64 for l in lines))


class CheckPaths(unittest.TestCase):
    def check(self, folder):
        return subprocess.run([sys.executable, "-I", str(CHECK_PATHS), str(folder)], capture_output=True, text=True)

    def test_a_clean_folder_passes(self):
        with tempfile.TemporaryDirectory() as d:
            write(Path(d) / "README.txt", "nothing local here")
            self.assertEqual(self.check(d).returncode, 0)

    def test_a_local_path_fails(self):
        with tempfile.TemporaryDirectory() as d:
            write(Path(d) / "lib.so", "built in /Users/someone/src")
            self.assertEqual(self.check(d).returncode, 1)

    def test_a_game_file_fails_whatever_its_case(self):
        for name in ["BTECH.EXE", "map1.mtp", "data/CITADEL.BLD", "DEMOFILE"]:
            with tempfile.TemporaryDirectory() as d:
                write(Path(d) / name, "x")
                result = self.check(d)
                self.assertEqual(result.returncode, 1, name)
                self.assertIn("original game", result.stdout)


@unittest.skipIf(os.name == "nt", "a shell script stands in for the program")
class Smoke(unittest.TestCase):
    def test_the_smoke_test_asks_for_the_usage_and_the_status(self):
        with tempfile.TemporaryDirectory() as d:
            program = Path(d) / "chi"
            program.write_text('#!/bin/sh\n'
                               '[ "$1" = --help ] && { echo "usage: chi --status | --setup"; exit 0; }\n'
                               '[ "$1" = --status ] && { echo \'{"kit": 1, "game": "inception"}\'; exit 0; }\n'
                               'exit 2\n')
            program.chmod(0o755)
            build_package.smoke(program)
            program.write_text('#!/bin/sh\n[ "$1" = --help ] && { echo "usage: chr"; exit 0; }\nexit 0\n')
            with self.assertRaisesRegex(RuntimeError, "usage line"):
                build_package.smoke(program)

    def test_the_smoke_runs_are_hidden_on_a_made_up_home(self):
        with tempfile.TemporaryDirectory() as d:
            env = build_package.hidden_env(Path(d), CHR_DATA_DIR="x")
            self.assertEqual(env["CHR_HIDDEN"], "1")
            self.assertEqual(env["SDL_VIDEO_DRIVER"], "dummy")
            self.assertTrue(env["HOME"].startswith(d))
            self.assertEqual(env["CHR_DATA_DIR"], "x")
            self.assertNotIn("CHR_GAME_DIR", env)
            json.dumps(env)


if __name__ == "__main__":
    unittest.main()
