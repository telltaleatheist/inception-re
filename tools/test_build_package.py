"""The packaging's own parts, without a build, SDL or the game's files:

    python3 -I -m unittest discover -s tools -p 'test_build_package.py' -v
"""
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent / "package"))
import build_package  # noqa: E402
import sums  # noqa: E402

CHECK_PATHS = Path(__file__).resolve().parent / "package/check_paths.py"
ROOT = Path(__file__).resolve().parent.parent


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
    @staticmethod
    def fixture(_, root):
        for name in ["LICENSE", *build_package.OUR_DATA, "data/" + build_package.KIT_SETTINGS, "sdl/LICENSE.txt",
                     "extern/re-setup-kit/LICENSE", "extern/re-setup-kit/third_party/stb/LICENSE",
                     "extern/re-setup-kit/third_party/miniz/LICENSE", "extern/re-setup-kit/third_party/monocypher/LICENCE.md",
                     "extern/re-setup-kit/assets/OFL.txt"]:
            write(root / name)
        # The kit's own tool writes the files record.
        (root / build_package.FILES_MANIFEST).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / build_package.FILES_MANIFEST, root / build_package.FILES_MANIFEST)
        prefix = root / "deps/prefix"
        for name in build_package.ARCHIVE_LICENCES:
            write(prefix / "share/licenses" / name, "fixture licence")
        write(root / "build/CMakeCache.txt", f"REKIT_LIBARCHIVE_LIB:FILEPATH={prefix / 'lib/libarchive.a'}\n"
                                              f"REKIT_NOTICE_FILES:INTERNAL={root / 'extern/re-setup-kit/assets/OFL.txt'}\n")

    def test_the_stage_holds_our_data_the_read_me_and_every_licence(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            Staging.fixture(None, root)
            out = root / "stage"
            out.mkdir()
            build_package.stage_common(root, root / "build", root / "sdl", out)
            for name in build_package.OUR_DATA:
                self.assertTrue((out / name).is_file())
            self.assertTrue((out / "LICENSE").is_file() and (out / "README.txt").is_file())
            self.assertTrue((out / build_package.KIT_SETTINGS).is_file())  # the kit's settings beside the program
            self.assertIn("Shift+Alt+F1", (out / "README.txt").read_text())
            for name in ["SDL-LICENSE.txt", "re-setup-kit-LICENSE", "stb-LICENSE", "miniz-LICENSE", "monocypher-LICENCE.md",
                         "re-setup-kit-OFL.txt", *build_package.ARCHIVE_LICENCES.values()]:
                self.assertTrue((out / "licenses" / name).is_file(), name)

    def test_a_missing_kit_licence_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            Staging.fixture(None, root)
            (root / "extern/re-setup-kit/third_party/stb/LICENSE").unlink()
            (root / "stage").mkdir()
            with self.assertRaisesRegex(RuntimeError, "Missing licence"):
                build_package.stage_common(root, root / "build", root / "sdl", root / "stage")

    def test_a_shared_libarchive_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            Staging.fixture(None, root)
            write(root / "build/CMakeCache.txt", "REKIT_LIBARCHIVE_LIB:FILEPATH=/opt/x/lib/libarchive.dylib\n"
                                                  f"REKIT_NOTICE_FILES:INTERNAL={root / 'extern/re-setup-kit/assets/OFL.txt'}\n")
            (root / "stage").mkdir()
            with self.assertRaisesRegex(RuntimeError, "static libarchive"):
                build_package.stage_common(root, root / "build", root / "sdl", root / "stage")


@unittest.skipIf(os.name == "nt", "a shell script stands in for the program")
class LinuxLayout(unittest.TestCase):
    def build(self, root, installer=False):
        Staging.fixture(None, root)
        program = root / "build/chr"
        write(program, '#!/bin/sh\n'
                       '[ "$1" = --help ] && { echo "usage: chi --status | --setup"; exit 0; }\n'
                       '[ "$1" = --status ] && { echo \'{"kit": 1, "game": "inception", "setup": 1}\'; exit 0; }\n'
                       'exit 2\n')
        with open(program, "ab") as f:  # past the exit: the launcher's marker, as the real program holds it
            f.write(b"MW3-PROGRAM-VERSION:1-test\0")
        program.chmod(0o755)
        write(root / "build/sdl3/libSDL3.so.0.4.12", "fixture library")
        (root / "build/sdl3/libSDL3.so.0").symlink_to("libSDL3.so.0.4.12")
        with patch.object(build_package, "configure_and_build", return_value=program), \
                patch.object(build_package, "needed_sdl", return_value="libSDL3.so.0"), \
                patch.object(build_package, "resolved_beside", return_value=True), \
                patch.object(build_package, "check_paths"), \
                patch.object(build_package.served_setup, "checks", return_value=[(True, "served")]), \
                patch.object(build_package, "platform_name", return_value="x86_64"):
            return build_package.package(root, root / "build", root / "sdl", root / "dist", "1-test",
                                         installer=installer)

    def test_the_tarball_has_chi_and_sdl_side_by_side(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            archive = self.build(root)
            self.assertEqual(archive.name, "CHI-Rebuild-1-test-linux-x86_64.tar.gz")
            names = tarfile.open(archive).getnames()
            top = "CHI-Rebuild-1-test-linux-x86_64/"
            for name in ["chi", "libSDL3.so.0", *build_package.OUR_DATA, "README.txt", "licenses/SDL-LICENSE.txt"]:
                self.assertIn(top + name, names)
            self.assertNotIn(top + "chr", names)
            # The record of the package's own files, beside the program, naming the kit's settings too.
            record = tarfile.open(archive).extractfile(top + build_package.FILES_RECORD).read().decode()
            self.assertIn("  chi\n", record)
            self.assertIn("  " + build_package.KIT_SETTINGS + "\n", record)
            self.assertNotIn(build_package.FILES_RECORD, record)
            unpacked = root / "dist/smoke/CHI-Rebuild-1-test-linux-x86_64/libSDL3.so.0"
            self.assertFalse(unpacked.is_symlink())
            self.assertEqual(unpacked.read_text(), "fixture library")
            self.assertTrue((root / "dist/CHI-Rebuild-1-test-linux-x86_64.tar.gz.sha256").is_file())

    def test_the_appimage_stops_without_an_icon(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(RuntimeError, "needs an icon"):
                self.build(Path(d), installer=True)


class AppImageTools(unittest.TestCase):
    def test_the_two_tools_and_the_runtime_are_pinned(self):
        pins = build_package.appimage_tools(ROOT)
        self.assertEqual(set(pins), {"linuxdeploy-x86_64.AppImage", "appimagetool-x86_64.AppImage", "runtime-x86_64"})
        self.assertTrue(all(len(digest) == 64 for digest in pins.values()))

    def test_a_missing_or_different_tool_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            tools = Path(d)
            with self.assertRaisesRegex(RuntimeError, "--appimage-tools"):
                build_package.appimage_ready(ROOT, None)
            with self.assertRaisesRegex(RuntimeError, "Missing AppImage tool"):
                build_package.appimage_ready(ROOT, tools)
            for name in build_package.appimage_tools(ROOT):
                write(tools / name, "not the pinned release")
            with self.assertRaisesRegex(RuntimeError, "hashes .* pins"):
                build_package.appimage_ready(ROOT, tools)

    def test_the_appimage_needs_the_runtimes_notices(self):
        notices = sorted(f.name for f in (ROOT / build_package.RUNTIME_NOTICE).iterdir())
        self.assertIn("NOTICE.txt", notices)
        self.assertIn("libfuse-LGPL2.txt", notices)
        with tempfile.TemporaryDirectory() as d:
            licenses = Path(d) / "licenses"
            with self.assertRaisesRegex(RuntimeError, "runtime's notice"):
                build_package.check_runtime_notice(ROOT, licenses)
            shutil.copytree(ROOT / build_package.RUNTIME_NOTICE, licenses / "appimage-runtime")
            build_package.check_runtime_notice(ROOT, licenses)
            write(licenses / "appimage-runtime/NOTICE.txt", "changed")
            with self.assertRaisesRegex(RuntimeError, "NOTICE.txt"):
                build_package.check_runtime_notice(ROOT, licenses)


@unittest.skipUnless(sys.platform == "darwin", "a Mac app's x86_64 half")
class X86Copy(unittest.TestCase):
    def test_the_copy_holds_the_x86_64_half_alone_and_the_app_is_untouched(self):
        with tempfile.TemporaryDirectory() as d:
            app = Path(d) / "app/Inception Rebuild.app"
            program = app / "Contents/MacOS" / build_package.PROGRAM
            program.parent.mkdir(parents=True)
            shutil.copyfile("/usr/bin/true", program)  # a universal program of the system's (not its flags)
            program.chmod(0o755)
            with open(app / "Contents/Info.plist", "wb") as f:  # what makes the folder a bundle to codesign
                plistlib.dump({"CFBundleExecutable": build_package.PROGRAM, "CFBundleIdentifier": "test.chi",
                               "CFBundlePackageType": "APPL"}, f)
            before = program.read_bytes()
            (Path(d) / "copy").mkdir()
            copy = build_package.x86_64_copy(app, Path(d) / "copy")
            archs = subprocess.run(["lipo", "-archs", str(copy)], capture_output=True, text=True, check=True).stdout
            self.assertEqual(archs.split(), ["x86_64"])
            self.assertEqual(program.read_bytes(), before)
            subprocess.run(["codesign", "--verify", str(copy.parent.parent.parent)], check=True)


class FilesRecord(unittest.TestCase):
    def test_the_files_record_is_checked(self):
        with tempfile.TemporaryDirectory() as d:
            root, folder = Path(d), Path(d) / "pkg"
            Staging.fixture(None, root)
            write(folder / "chi", "program")
            write(folder / "data/text/interface.json", "{}")
            build_package.write_files_record(root, folder)
            build_package.check_files_record(folder)
            write(folder / "data/text/interface.json", "changed")
            with self.assertRaisesRegex(RuntimeError, "doesn't match"):
                build_package.check_files_record(folder)
            (folder / build_package.FILES_RECORD).unlink()
            with self.assertRaisesRegex(RuntimeError, "No "):
                build_package.check_files_record(folder)


class Marker(unittest.TestCase):
    def test_the_marker_holds_the_full_label_and_its_nul(self):
        with tempfile.TemporaryDirectory() as d:
            program = Path(d) / "chi"
            program.write_bytes(b"\x7fELF...MW3-PROGRAM-VERSION:chi-v0.1.0\0...")
            build_package.check_marker(program, "chi-v0.1.0")
            for other in ("chi-v0.1", "0.1.0", "chi-v0.1.1"):
                with self.assertRaisesRegex(RuntimeError, "marker"):
                    build_package.check_marker(program, other)

    def test_the_build_is_given_the_full_label(self):
        with patch.object(build_package, "run") as run, patch.object(build_package, "save_tests") as save_tests:
            build_package.configure_and_build(Path("r"), Path("b"), Path("s"), "chi-v0.1.0")
        save_tests.assert_called_once()
        configure = run.call_args_list[0].args
        self.assertIn("-DCHR_VERSION_TAG=chi-v0.1.0", configure)
        self.assertIn("-DCHR_VERSION=0.1.0", configure)


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

    def test_the_name_passes_only_in_a_public_github_address(self):
        name = "someonemadeup"
        for text, code in [("https://github.com/someonemadeup/x/releases", 0),
                           ("https://api.github.com/repos/someonemadeup/x", 0),
                           ("https://raw.githubusercontent.com/someonemadeup/x/main/catalog.json", 0),
                           ("https://raw.githubusercontent.com/someosomeonemadeup/x", 0),
                           ("https://example.com/someonemadeup/x", 1),
                           ("built by someonemadeup", 1)]:
            with tempfile.TemporaryDirectory() as d:
                write(Path(d) / "chi.exe", text)
                result = subprocess.run([sys.executable, "-I", str(CHECK_PATHS), d, name], capture_output=True, text=True)
                self.assertEqual(result.returncode, code, text)

    def test_a_game_file_fails_whatever_its_case(self):
        for name in ["BTECH.EXE", "map1.mtp", "data/CITADEL.BLD", "DEMOFILE"]:
            with tempfile.TemporaryDirectory() as d:
                write(Path(d) / name, "x")
                result = self.check(d)
                self.assertEqual(result.returncode, 1, name)
                self.assertIn("original game", result.stdout)


class SaveTests(unittest.TestCase):
    GOOD = "saves_files\n11 tests, 79 checks, 1 skipped, 0 failures\n"

    def answer(self, code, out):
        result = subprocess.CompletedProcess([], code, stdout=out, stderr="")
        with patch.object(build_package.subprocess, "run", return_value=result):
            build_package.save_tests(Path("savetests"))

    def test_a_run_that_passes_goes_on(self):
        self.answer(0, self.GOOD)

    def test_any_failure_and_nothing_tested_stop_the_packaging(self):
        for code, out in [(1, self.GOOD), (0, ""), (0, "saves_files\n"), (-11, "saves_files\n"),
                          (0, "11 tests, 79 checks, 0 skipped, 1 failures\n"),
                          (0, "0 tests, 0 checks, 0 skipped, 0 failures\n"),
                          (0, "11 tests, 0 checks, 11 skipped, 0 failures\n")]:
            with self.assertRaisesRegex(RuntimeError, "save tests failed"):
                self.answer(code, out)

    def test_they_are_built_with_the_program(self):
        with patch.object(build_package, "run") as run, patch.object(build_package, "save_tests"):
            build_package.configure_and_build(Path("r"), Path("b"), Path("s"), "1-test")
        target = run.call_args_list[-1].args
        self.assertIn("chr", target)
        self.assertIn("savetests", target)


@unittest.skipIf(os.name == "nt", "a shell script stands in for the program")
class Smoke(unittest.TestCase):
    def test_the_smoke_test_asks_for_the_usage_and_the_status(self):
        with tempfile.TemporaryDirectory() as d:
            program = Path(d) / "chi"
            program.write_text('#!/bin/sh\n'
                               '[ "$1" = --help ] && { echo "usage: chi --status | --setup"; exit 0; }\n'
                               '[ "$1" = --status ] && { echo \'{"kit": 1, "game": "inception", "setup": 1}\'; exit 0; }\n'
                               'exit 2\n')
            program.chmod(0o755)
            with patch.object(build_package.served_setup, "checks", return_value=[(True, "served")]):
                build_package.smoke(program)
            with patch.object(build_package.served_setup, "checks", return_value=[(True, "a"), (False, "cancel")]):
                with self.assertRaisesRegex(RuntimeError, "served setup: cancel"):
                    build_package.smoke(program)
            program.write_text(program.read_text().replace(', "setup": 1', ""))
            with self.assertRaisesRegex(RuntimeError, "setup is served"):
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



class Rpaths(unittest.TestCase):
    def test_a_build_folder_with_a_space_is_kept_whole(self):
        lines = ["Load command 20", "          cmd LC_RPATH", "      cmdsize 64",
                 "         path /Volumes/Disk/Projects/a game/build-package/sdl3 (offset 12)",
                 "Load command 21", "          cmd LC_RPATH", "      cmdsize 32", "         path @loader_path (offset 12)",
                 "Load command 40", "          cmd LC_RPATH", "      cmdsize 64",
                 "         path /Volumes/Disk/Projects/a game/build-package/sdl3 (offset 12)"]
        self.assertEqual(build_package.build_rpaths(lines),
                         ["/Volumes/Disk/Projects/a game/build-package/sdl3", "@loader_path"])

class MacSigning(unittest.TestCase):
    """The commands only: run is a mock, so nothing is signed, and the identity is a made-up word."""

    def calls(self, work, env):
        root, app, dmg = Path(work), Path(work) / "Inception Rebuild.app", Path(work) / "x.dmg"
        frameworks = app / "Contents/Frameworks"
        write(frameworks / "libSDL3.0.dylib")
        with patch.dict(os.environ, env, clear=False), patch.object(build_package, "run") as run:
            for name in ("RE_SIGN_IDENTITY", "MW3_SIGN_IDENTITY"):
                if name not in env:
                    os.environ.pop(name, None)
            build_package.sign_app(root, app, frameworks, app / "Contents/MacOS/chi", "chi.entitlements")
            build_package.notarize_dmg(root, dmg, app)
        return root, app, dmg, [c.args for c in run.call_args_list]

    def test_without_an_identity_it_is_signed_ad_hoc_and_the_dmg_is_left(self):
        with tempfile.TemporaryDirectory() as d:
            root, app, dmg, calls = self.calls(d, {})
            self.assertTrue(all(c[0] == "codesign" for c in calls))
            self.assertEqual(sum(1 for c in calls if c[:4] == ("codesign", "--force", "--sign", "-")), 3)
            self.assertEqual(calls[-1], ("codesign", "--verify", "--deep", "--strict", str(app)))
            self.assertFalse(any(build_package.SIGNER in c[0] for c in calls))

    def test_with_an_identity_the_kit_alone_signs_and_notarizes(self):
        with tempfile.TemporaryDirectory() as d:
            root, app, dmg, calls = self.calls(d, {"RE_SIGN_IDENTITY": "made-up"})
            signer = str(root / build_package.SIGNER)
            self.assertEqual(calls, [(signer, "app", str(app), "--entitlements", "chi.entitlements"),
                                     ("codesign", "--verify", "--deep", "--strict", str(app)),
                                     (signer, "dmg", str(dmg), str(app))])
            self.assertFalse(any("made-up" in a for c in calls for a in c))

    def test_the_kits_older_name_counts_as_an_identity(self):
        with patch.dict(os.environ, {"MW3_SIGN_IDENTITY": "made-up"}, clear=False):
            os.environ.pop("RE_SIGN_IDENTITY", None)
            self.assertTrue(build_package.developer_id())

    def test_the_signer_is_the_kits(self):
        self.assertTrue((Path(__file__).resolve().parent.parent / build_package.SIGNER).is_file())


if __name__ == "__main__":
    unittest.main()
