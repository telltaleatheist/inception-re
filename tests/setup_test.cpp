// The setup recipe (src/setup/setup.h) over re-setup-kit, on folders made in
// the work folder. The game-free parts always run: the player's folder's name
// and its move from the earlier names, settings.ini's game_folder, the
// verdicts on folders that are not the game or are another version, an
// install refusing a folder that is not ours, --status's answer writing
// nothing. With the player's copy (the second argument, CHR_GAME_DIR) the game
// parts run too: the copy judged, installed from a folder and from a zip
// (built here and never kept), an entry climbing out of the archive left out.
// Prints names and counts only, never the game's bytes; the work folder is
// removed at the end.
//
// CHR_AMIGA_COPY, when set, names an Amiga copy for its parts.
//
//     setup_test <work folder> [<game folder>]
#include "setup/setup.h"
#include "setup/versions.h"
#include "formats/sha256.h"

#include <rekit/archive.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#if REKIT_HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

namespace fs = std::filesystem;
namespace setup = chr::setup;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}
std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
std::size_t count_files(const fs::path& d) {
    std::error_code ec;
    std::size_t n = 0;
    for (auto it = fs::recursive_directory_iterator(d, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec))
        ++n;
    return n;
}

void set_env(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    if (value.empty()) unsetenv(name);
    else setenv(name, value.c_str(), 1);
#endif
}

#if REKIT_HAVE_LIBARCHIVE
// A zip of `files` (name in the archive, path on disk; an empty path writes a short text entry).
bool make_zip(const fs::path& out, const std::vector<std::pair<std::string, fs::path>>& files) {
    archive* a = archive_write_new();
    archive_write_set_format_zip(a);
    if (archive_write_open_filename(a, out.string().c_str()) != ARCHIVE_OK) return archive_write_free(a), false;
    for (const auto& [name, path] : files) {
        const std::string data = path.empty() ? std::string("not the game\n") : read(path);
        archive_entry* e = archive_entry_new();
        archive_entry_set_pathname(e, name.c_str());
        archive_entry_set_size(e, la_int64_t(data.size()));
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_write_header(a, e);
        archive_write_data(a, data.data(), data.size());
        archive_entry_free(e);
    }
    archive_write_close(a);
    archive_write_free(a);
    return true;
}
#endif

// The player's folder on each start (setup::settle_user_folder), each case on folders of its own.
void user_folder_move(const fs::path& base) {
    std::error_code ec;
    const fs::path now = base / "BattleTech" / setup::kApplication;
    const fs::path re = base / "inception-re", first = base / "crescent-hawks-rebuild";
    const std::string folder = (now / "").string();  // ending in a separator, as SDL names it
    const auto fresh = [&] {
        fs::remove_all(base, ec);
        fs::create_directories(base);
    };

    fresh();
    write(now / "Game1", "new");
    write(re / "Game1", "old");
    setup::Settled s = setup::settle_user_folder(folder);
    check(s.kind == setup::Settled::kInUse && s.folder == folder && read(now / "Game1") == "new" &&
              read(re / "Game1") == "old" && setup::unmoved_folder(folder).empty(),
          "move: a folder in use is used, never merged with an earlier one");

    fresh();
    write(re / "Game1", "re");
    write(re / "import.json", "{}");
    write(re / setup::kSettingsFile, "game_folder=/x\n");
    fs::create_directories(now);  // as SDL_GetPrefPath leaves it
    check(fs::path(setup::unmoved_folder(folder)) == fs::path((re / "").string()),
          "move: before it, inception-re is the folder in use");
    s = setup::settle_user_folder(folder);
    check(s.kind == setup::Settled::kMoved && s.folder == folder && !fs::exists(re), "move: inception-re renamed");
    check(read(now / "Game1") == "re" && count_files(now) == 3, "move: everything in it moved");
    check(setup::settle_user_folder(folder).kind == setup::Settled::kInUse, "move: the next start uses it as it is");

    fresh();
    write(first / "Game2", "first");
    s = setup::settle_user_folder(folder);
    check(s.kind == setup::Settled::kMoved && read(now / "Game2") == "first" && !fs::exists(first),
          "move: crescent-hawks-rebuild renamed when it is the only one");

    fresh();
    write(re / "Game1", "re");
    write(first / "Game1", "first");
    s = setup::settle_user_folder(folder);
    check(s.kind == setup::Settled::kMoved && read(now / "Game1") == "re" && read(first / "Game1") == "first",
          "move: the newer name wins, the older left as it was");

    fresh();
    write(re / "Game1", "re");
    write(base / "BattleTech", "not a folder");
    s = setup::settle_user_folder(folder);
    check(s.kind == setup::Settled::kMoveFailed && fs::path(s.folder) == fs::path((re / "").string()) &&
              !s.error.empty() && read(re / "Game1") == "re",
          "break: a failed move keeps the old folder whole and in use");
    fs::remove(base / "BattleTech", ec);
    s = setup::settle_user_folder(folder);
    check(s.kind == setup::Settled::kMoved && read(now / "Game1") == "re", "move: the next start tries again");

    fresh();
    s = setup::settle_user_folder(folder);
    check(s.kind == setup::Settled::kNew && !fs::exists(now), "move: nothing to settle: nothing made");
    fs::remove_all(base, ec);
}

void game_free(const fs::path& work) {
    const std::string pf = setup::pref_folder(setup::kOrganisation, setup::kApplication);
    check(pf.find("BattleTech") != std::string::npos && pf.find("Inception Rebuild") != std::string::npos,
          "the player's folder is BattleTech/Inception Rebuild");
    set_env("CHR_DATA_DIR", (work / "configured").string());
    check(fs::path(setup::user_folder()) == fs::path((work / "configured" / "").string()),
          "CHR_DATA_DIR names the player's folder");
    set_env("CHR_DATA_DIR", "");

    user_folder_move(work / "family");

    // settings.ini's game_folder: read, and written with the other lines kept.
    write(work / "ini" / setup::kSettingsFile, "; a comment\nspeed=2\n  game_folder = /some/where  \n");
    check(setup::remembered_folder((work / "ini").string()) == "/some/where", "game_folder read from settings.ini");
    check(setup::remember_folder((work / "ini").string(), "/else/where") &&
              setup::remembered_folder((work / "ini").string()) == "/else/where" &&
              read(work / "ini" / setup::kSettingsFile).find("speed=2\n") != std::string::npos,
          "game_folder written, the other lines kept");
    check(setup::remembered_folder((work / "absent").string()).empty(), "no settings.ini: no game_folder");

    // Folders that are not the game, and another version's.
    write(work / "notgame" / "README.TXT", "x");
    write(work / "other" / "BTECH.EXE", std::string(1000, 'x'));
    write(work / "older" / "BTECH.EXE", std::string(1000, 'x'));
    write(work / "older" / "MAP1.MTP", std::string(100, 'm'));
    check(setup::check_install((work / "notgame").string()).state == rekit::Status::kMissing,
          "a folder without BTECH.EXE: missing");
    check(setup::check_install((work / "other").string()).state == rekit::Status::kMissing,
          "break: another game's BTECH.EXE: missing");
    const setup::Check older = setup::check_install((work / "older").string());
    check(older.state == rekit::Status::kIncomplete && older.version.empty() && older.missing.size() == 2,
          "another version's copy: incomplete, BTECH.EXE and the data files named");
    check(rekit::find_source((work / "other").string(), setup::rules()).kind == rekit::Found::kNothing,
          "break: the walk passes over another game's BTECH.EXE");
    const rekit::Found f = rekit::find_source((work / "older").string(), setup::rules());
    check(f.kind == rekit::Found::kInstall && f.game == "inception" && f.version.empty(),
          "the walk finds another version's copy by its map");
    check(setup::btech_version(std::vector<std::uint8_t>(152429, 0)).empty(), "break: zeros of BTECH.EXE's size");

    // --status's answer with nothing set up: missing, nothing written. The
    // search looks in a made-up home and beside a program alone in its folder.
    const fs::path user = work / "status-user";
    const char* real_home = std::getenv("HOME");
    const std::string home = real_home ? real_home : "";
    set_env("HOME", (work / "home").string());
    fs::create_directories(work / "home");
    const rekit::Status st = setup::status({}, user.string(), (work / "alone" / "bin").string(), "test");
    set_env("HOME", home);
    check(st.game == "inception" && st.state == rekit::Status::kMissing, "status: inception, missing");
    check(!fs::exists(user), "break: status writes nothing (the player's folder not made)");
    const rekit::Status st2 = setup::status({(work / "older").string()}, user.string(), "", "test");
    check(st2.state == rekit::Status::kIncomplete && st2.note.find(setup::kVersion) != std::string::npos,
          "status: another version is incomplete, the note names the version needed");

    // Break: a .new folder that is not ours where the install stages is left alone.
    write(work / "blocked.new" / "X", "the player's");
    const setup::Installed r = setup::install(f, (work / "blocked").string());
    check(!r.ok && fs::exists(work / "blocked.new" / "X"), "break: a .new folder that is not ours is left alone");
}

// What can't be read, or has a name that is no text in the program's code page, in the way of a copy.
void in_the_way(const fs::path& work) {
    const fs::path base = work / "way";
    const std::string copy = (base / "above" / "copy").string(), bin = (base / "above" / "bin").string();
    const auto made_up_copy = [](const fs::path& dir) {
        write(dir / "BTECH.EXE", "made up");
        write(dir / "MAP1.MTP", "made up");
    };
    made_up_copy(copy);
    fs::create_directories(base / "home");
    fs::create_directories(base / "user");

    std::vector<std::string> looked;
    const std::string third = setup::first_found({"first", "second", "third", "fourth"}, [&](const std::string& place) {
        looked.push_back(place);
        if (place == "second") throw std::runtime_error("made up");
        return place == "third" ? "found there" : "";
    });
    check(third == "found there" && looked.size() == 3, "a place whose look throws is passed over, the next ones looked at");
    check(setup::first_found({"only"}, [](const std::string&) -> std::string { throw std::runtime_error("made up"); }).empty(),
          "every look throwing: nothing found");

    const char* real_home = std::getenv("HOME");
    const std::string home = real_home ? real_home : "";
    set_env("HOME", (base / "home").string());

    // Names for the files in the way: on Windows none but the first is text in code page 1252, and the last is none in
    // any (half a character; only there). Elsewhere they are text, and the copy is found all the same.
    std::vector<fs::path> names{"plain", fs::path(u8"\u0141 \u65e5\u672c"), fs::path(u8"emoji \U0001f600")};
#if defined(_WIN32)
    names.push_back(fs::path(std::wstring{L'h', wchar_t(0xd83d), L'x'}));
#endif
    for (const fs::path& name : names)
        for (const char* where : {"bin", "", "a folder", "copy"}) {
            const fs::path folder = base / "above" / where;
            fs::create_directories(folder);
            std::ofstream(folder / name) << "in the way";
        }
    for (const fs::path& name : names) fs::create_directories(base / "above" / "folders" / name);
    bool searched = false;
    check(setup::game_in((base / "above").string()) == copy, "game_in past files and folders of every name");
    check(setup::find_install({}, (base / "user").string(), bin, &searched) == copy && searched,
          "the search past files and folders of every name");
    check(setup::status({}, (base / "user").string(), bin, "test").where == copy, "status past files and folders of every name");

#if !defined(_WIN32)
    // A remembered folder of one disk image that can't be opened: its size alone says what it is.
    const fs::path locked = base / "locked", image = locked / "game.adf";
    write(image, std::string(901120, 'x'));
    fs::permissions(image, fs::perms::none);
    if (std::ifstream(image).good()) {
        std::printf("skip  an image that can't be opened (this user opens everything)\n");
    } else {
        bool threw = false, there = true;
        std::string found;
        rekit::Status st;
        try {
            there = setup::check_install(locked.string()).state != rekit::Status::kMissing;
            found = setup::find_install({locked.string()}, (base / "user").string(), bin, &searched);
            st = setup::status({locked.string()}, (base / "user").string(), bin, "test");
        } catch (const std::exception&) {
            threw = true;
        }
        check(!threw && !there, "a folder of an image that can't be opened: missing");
        check(found == copy && searched && st.where == copy, "the search goes on past it");
        // ... and one found by the search: a folder named BTECH.EXE beside it makes it look like the game.
        fs::create_directories(base / "above" / "a image" / "BTECH.EXE");
        write(base / "above" / "a image" / "game.adf", std::string(901120, 'x'));
        fs::permissions(base / "above" / "a image" / "game.adf", fs::perms::none);
        check(setup::find_install({}, (base / "user").string(), bin, &searched) == copy,
              "a place of the search's whose image can't be opened hides nothing");
    }
    fs::permissions(image, fs::perms::owner_all);
#endif
    set_env("HOME", home);
}

// What 1.0.0 wrote on Windows, read back. `latin` stands for the system's code page: each byte is the letter with
// that number (as in code page 1252, but for its 0x80 to 0x9f).
void old_names(const fs::path& work) {
    const setup::PageText latin = [](const std::string& bytes) -> std::optional<std::string> {
        std::string out;
        for (const char c : bytes) {
            const auto b = static_cast<unsigned char>(c);
            if (b < 0x80) out += c;
            else out += char(0xc0 | b >> 6), out += char(0x80 | (b & 0x3f));
        }
        return out;
    };
    const setup::PageText none = [](const std::string&) -> std::optional<std::string> { return std::nullopt; };
    const std::string utf8 = "Zo\xc3\xab", old = "Zo\xeb", misread = "Zo\xc3\x83\xc2\xab";

    for (const char* good : {"", "plain", "\xc2\x80", "\xdf\xbf", "\xe0\xa0\x80", "\xed\x9f\xbf", "\xee\x80\x80",
                             "\xe6\x97\xa5 x", "\xf0\x90\x80\x80", "\xf4\x8f\xbf\xbf"})
        check(setup::is_utf8(good), "UTF-8: " + std::to_string(std::string(good).size()) + " bytes of it");
    for (const char* bad : {"\x80", "Zo\xeb", "\xeb x", "\xc0\x80", "\xc1\xbf", "\xe0\x9f\xbf", "\xed\xa0\x80", "\xed\xbf\xbf",
                            "\xf0\x8f\xbf\xbf", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xe6\x97", "\xe6\x97x", "\xc3"})
        check(!setup::is_utf8(bad), "not UTF-8: " + std::to_string(std::string(bad).size()) + " bytes");

    using List = std::vector<std::string>;
    check(setup::folder_readings("plain", latin) == List{"plain"}, "an ASCII name has one reading");
    check(setup::folder_readings(old, {}) == List{old} && setup::folder_readings(utf8, {}) == List{utf8},
          "where the code page is the system's, the bytes as they are");
    check(setup::folder_readings(utf8, latin) == List({utf8, misread}), "UTF-8 first, then as 1.0.0 misread it");
    check(setup::folder_readings(old, latin) == List{utf8}, "what isn't UTF-8 is the old page's");
    check(setup::folder_readings(old, none).empty() && setup::folder_readings(utf8, none) == List{utf8},
          "no text in the old page either: no reading");

    const fs::path base = work / "old";
    const std::string user = (base / "user").string(), games = (base / "games" / "").string();
    const auto remembered = [&](const std::string& bytes, const setup::PageText& page) {
        write(base / "user" / setup::kSettingsFile, "game_folder=" + bytes + "\n");
        return setup::remembered_folder(user, page);
    };
    check(remembered(games + old, latin) == games + utf8, "settings.ini in the old page: the folder it meant");
    check(remembered(games + old, none).empty(), "settings.ini nothing can be made of: no folder");
    check(remembered(games + utf8, latin) == games + utf8, "settings.ini in UTF-8, no such folder: as it is");
    write(fs::path(games + misread) / "BTECH.EXE", "made up");
    write(fs::path(games + misread) / "MAP1.MTP", "made up");
    check(remembered(games + utf8, latin) == games + misread, "the game in the folder 1.0.0 misread: that one");
    check(remembered(games + utf8, {}) == games + utf8, "... and not where the code page is the system's");
    bool searched = true;
    check(setup::find_install({setup::remembered_folder(user, latin)}, user, "", &searched) == games + misread && !searched,
          "a start uses it without a search");
    write(fs::path(games + utf8) / "BTECH.EXE", "made up");
    write(fs::path(games + utf8) / "MAP1.MTP", "made up");
    check(remembered(games + utf8, latin) == games + utf8, "the game in both: the rightly named one");
    const std::string ini = read(base / "user" / setup::kSettingsFile);
    check(fs::exists(fs::path(games + misread) / "MAP1.MTP") && ini == "game_folder=" + games + utf8 + "\n",
          "nothing moved, settings.ini as it was");

    // The saved games 1.0.0 left in the player's folder's misread name.
    const fs::path right = base / "pref" / utf8 / "Inception Rebuild", left = base / "pref" / misread / "Inception Rebuild";
    fs::create_directories(right);
    write(left / "chi.log", "x");
    check(setup::left_saves(right.string(), latin).empty(), "no saved game under the misread name: nothing to say");
    write(left / "Game3", "x");
    check(setup::left_saves(right.string(), latin) == left.string(), "a saved game under the misread name is named");
    check(setup::left_saves(right.string(), {}).empty() && setup::left_saves(right.string(), none).empty(),
          "... not where nothing was misread");
    check(setup::left_saves((base / "user").string(), latin).empty(), "... nor for an ASCII name");
    write(right / "Game1", "x");
    check(setup::left_saves(right.string(), latin).empty(), "... nor once there is one under the right name");
    check(count_files(base / "pref") == 7, "nothing moved");
}

// With an Amiga copy (CHR_AMIGA_COPY: an .adf, or a folder or archive holding one).
void with_amiga(const fs::path& work, const std::string& copy) {
    const setup::AmigaChoice choice = setup::choose_amiga_copy(copy);
    check(choice.copy && choice.copy->files.size() == 18, "Amiga: the copy found, its 18 files");
    if (!choice.copy) return;
    std::string note;
    const rekit::Found f = setup::find_game(copy, &note);
    check(f.kind == rekit::Found::kInstall && f.version == setup::kAmigaVersion, "Amiga: find_game finds it");
    const fs::path out = work / "amiga" / "install";
    setup::Installed r = setup::install(f, out.string());
    const setup::Check c = setup::check_install(out.string());
    check(r.ok && r.files == 18 && c.state == rekit::Status::kReady && c.version == setup::kAmigaVersion,
          "Amiga: installed, ready");
    r = setup::install(f, out.string());
    check(r.ok, "Amiga: installing again replaces our own install");
    const fs::path theirs = work / "amiga-theirs";
    write(theirs / "LETTER.TXT", "the player's");
    r = setup::install(f, theirs.string());
    check(!r.ok && read(theirs / "LETTER.TXT") == "the player's", "break: Amiga: a folder that is not ours is never replaced");
    r = setup::install(f, (work / "amiga-stopped").string(), [](double, const std::string&) { return false; });
    check(r.cancelled && !fs::exists(work / "amiga-stopped") && !fs::exists(work / "amiga-stopped.new"),
          "Amiga: a stop leaves nothing");
    // The files in a folder, and in the disk's layout, are found by the kit's walk.
    check(rekit::find_source(out.string(), setup::rules()).version == setup::kAmigaVersion, "Amiga: the walk finds its folder");
    fs::create_directories(work / "amiga-disk");
    fs::copy(out, work / "amiga-disk" / "Battletech");
    const rekit::Found disk = rekit::find_source((work / "amiga-disk").string(), setup::rules());
    r = setup::install(disk, (work / "amiga-from-disk").string());
    check(r.ok && setup::check_install((work / "amiga-from-disk").string()).state == rekit::Status::kReady,
          "Amiga: installed from the disk's folder");
    check(setup::game_in((work / "amiga-disk").string()) == (work / "amiga-disk" / "Battletech").string(), "game_in finds the Amiga's files");
#if REKIT_HAVE_LIBARCHIVE
    std::vector<std::pair<std::string, fs::path>> entries;
    for (const auto& e : fs::directory_iterator(out)) entries.push_back({"x/Battletech/" + e.path().filename().string(), e.path()});
    const fs::path zip = work / "amiga-files.zip";
    check(make_zip(zip, entries), "a zip of the Amiga's files built in the work folder");
    r = setup::install(setup::find_game(zip.string()), (work / "amiga-zip").string());
    check(r.ok && setup::check_install((work / "amiga-zip").string()).state == rekit::Status::kReady,
          "Amiga: installed from a zip of its files");
#endif
    // A data file gone, and the program changed.
    fs::remove(out / "maps");
    check(setup::check_install(out.string()).state == rekit::Status::kMissing, "Amiga: without the maps: not the game");
    fs::copy_file(work / "amiga-disk" / "Battletech" / "maps", out / "maps");
    fs::remove(out / "snd");
    check(setup::check_install(out.string()).state == rekit::Status::kIncomplete, "Amiga: a data file gone: incomplete");
    write(out / "battletech", "junk");
    check(setup::check_install(out.string()).state == rekit::Status::kMissing, "break: Amiga: another program: not the game");
}

void with_game(const fs::path& work, const fs::path& game) {
    const std::string btech = rekit::any_case_path(game.string(), "BTECH.EXE");
    std::ifstream in(btech, std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    check(setup::btech_version(bytes) == setup::kVersion, "the player's BTECH.EXE judged " + std::string(setup::kVersion));
    check(setup::check_install(game.string()).state == rekit::Status::kReady, "the player's copy is ready");

    // The copy a level down inside the folder picked: found, installed (copied), the original kept.
    const fs::path picked = work / "picked", copy = picked / "BTECH";
    fs::create_directories(copy);
    for (const auto& e : fs::directory_iterator(game))
        if (e.is_regular_file()) fs::copy_file(e.path(), copy / e.path().filename());
    write(copy / "GAME1", "x");
    check(setup::game_in(picked.string()) == copy.string(), "game_in finds the copy a level down");
    const rekit::Found f = rekit::find_source(copy.string(), setup::rules());
    check(f.kind == rekit::Found::kInstall && f.game == "inception" && f.version == setup::kVersion,
          "find_source: the folder is the game");
    check(rekit::find_source((copy / "BTECH.EXE").string(), setup::rules()).kind == rekit::Found::kInstall,
          "a pick of BTECH.EXE stands for its folder");
    const fs::path out = work / "user" / "install";
    setup::Installed r = setup::install(f, out.string());
    check(r.ok && setup::check_install(out.string()).state == rekit::Status::kReady, "installed from a folder: ready");
    check(fs::exists(copy / "BTECH.EXE") && fs::exists(copy / "GAME1"), "the player's folder is left as it was");
    check(fs::exists(out / setup::kInstallMarker), "our install carries its marker");
    r = setup::install(f, out.string());
    check(r.ok, "installing again replaces our own install");

    // Stopped in mid copy: nothing left; and all the while the staging folder is marked ours.
    bool marked = true;
    r = setup::install(f, (work / "stopped").string(), [&](double at, const std::string&) {
        marked = marked && fs::exists(work / "stopped.new" / setup::kInstallMarker);
        return at < 0.5;
    });
    check(!r.ok && r.cancelled && marked && !fs::exists(work / "stopped") && !fs::exists(work / "stopped.new"),
          "break: a folder copy stopped: nothing left, marked ours all the while");
    write(work / "broken.new" / setup::kInstallMarker, "ours, broken off");
    r = setup::install(f, (work / "broken").string());
    check(r.ok && !fs::exists(work / "broken.new") && !fs::exists(work / "broken" / "game"),
          "a copy broken off is cleared up by the next install");

    const fs::path theirs = work / "theirs";
    write(theirs / "LETTER.TXT", "the player's");
    r = setup::install(f, theirs.string());
    check(!r.ok && read(theirs / "LETTER.TXT") == "the player's" && !fs::exists(theirs.string() + ".new"),
          "break: a folder that is not ours is never replaced");

    // status: the player's own install found, ready, nothing written.
    const std::size_t before = count_files(work / "user");
    const rekit::Status st = setup::status({""}, (work / "user").string(), (work / "picked-not").string(), "test");
    check(st.state == rekit::Status::kReady && fs::path(st.where).lexically_normal() == out.lexically_normal() &&
              st.data == setup::kVersion,
          "status: the player's install found, ready, DOS 1.03");
    check(count_files(work / "user") == before, "break: status wrote nothing in the player's folder");

    // A data file gone: incomplete, PLAY still on (the launcher's).
    fs::remove(rekit::any_case_path(out.string(), "MAP9.MTP"));
    const setup::Check c = setup::check_install(out.string());
    check(c.state == rekit::Status::kIncomplete && c.missing.size() == 1, "a data file gone: incomplete");

#if REKIT_HAVE_LIBARCHIVE
    // A download: the game a folder down in a zip, with an entry trying to climb out.
    std::vector<std::pair<std::string, fs::path>> entries;
    for (const auto& e : fs::directory_iterator(game))
        if (e.is_regular_file()) entries.push_back({"chi/BTECH/" + e.path().filename().string(), e.path()});
    entries.push_back({"chi/BTECH/../../ESCAPE.TXT", {}});
    entries.push_back({"chi/OTHER.TXT", {}});
    const fs::path zip = work / "inception.zip";
    check(make_zip(zip, entries), "a zip built in the work folder");
    const rekit::Found z = rekit::find_source(zip.string(), setup::rules());
    check(z.kind == rekit::Found::kInstall && z.version == setup::kVersion && z.root == "chi/BTECH/",
          "find_source: the zip holds the game");
    const fs::path zout = work / "zuser" / "install";
    r = setup::install(z, zout.string());
    check(r.ok && setup::check_install(zout.string()).state == rekit::Status::kReady, "installed from a zip: ready");
    check(!fs::exists(work / "ESCAPE.TXT") && !fs::exists(work / "zuser" / "ESCAPE.TXT") &&
              !fs::exists(zout / "OTHER.TXT"),
          "break: nothing outside the game's folder in the zip is written");
    check(fs::exists(zip), "the zip is left as it was");
#else
    std::printf("skip archives: this build has no libarchive\n");
#endif
}

// The other versions' recognition (setup/versions.h) against a table of the test's own: a
// made-up 1541 disk image holding two made-up files, found loose, in a zip and in a zip in a
// zip; one disk of a two-disk version named with the other missing; a changed variant file.
void other_versions(const fs::path& work) {
    // A 35-track image: the directory at 18/1, file A on track 1, file B on track 2.
    std::string image(174848, '\0');
    auto at = [](int t, int sec) {
        std::size_t before = 0;
        for (int i = 1; i < t; ++i) before += i <= 17 ? 21 : i <= 24 ? 19 : i <= 30 ? 18 : 17;
        return (before + std::size_t(sec)) * 256;
    };
    image[at(18, 0)] = 18, image[at(18, 0) + 1] = 1;
    image[at(18, 1) + 1] = char(0xff);
    const std::string a_data = "made-up file one", b_data = "made-up file two, a variant";
    auto put = [&](int slot, const std::string& name, const std::string& data, int track) {
        const std::size_t e = at(18, 1) + std::size_t(slot) * 32;
        image[e + 2] = char(0x82), image[e + 3] = char(track), image[e + 4] = 0;
        for (std::size_t i = 0; i < 16; ++i) image[e + 5 + i] = i < name.size() ? name[i] : char(0xa0);
        image[at(track, 0)] = 0, image[at(track, 0) + 1] = char(data.size() + 1);
        image.replace(at(track, 0) + 2, data.size(), data);
    };
    put(0, "FIXA", a_data, 1);
    put(1, "FIXB", b_data, 2);
    const fs::path loose = work / "loose" / "fixture.d64";
    write(loose, image);
    auto hash = [](const std::string& s) {
        return chr::formats::sha256_hex({reinterpret_cast<const std::uint8_t*>(s.data()), s.size()});
    };
    const std::string ha = hash(a_data), hb = hash(b_data);
    // Disk 1 of 2, its variant file listed with the wrong hash; disk 2 nowhere.
    const std::vector<setup::KnownDisk> two{
        {setup::Machine::C64, "", 1, 2, {{"fixa", a_data.size(), ha.c_str()}}, {{"", {"fixb", b_data.size(), ha.c_str()}}}},
        {setup::Machine::C64, "", 2, 2, {{"fixz", 1, ha.c_str()}}, {}},
    };
    auto copies = setup::recognise(setup::seen_files((work / "loose").string(), two, nullptr), two);
    check(copies.size() == 1 && copies[0].found == std::vector<int>{1} &&
              copies[0].missing() == std::vector<std::string>{"disk 2 of 2"},
          "a disk image in a folder: disk 1 of 2 named, disk 2 missing");
    check(!copies.empty() && copies[0].variants == std::vector<std::string>{"unknown"} &&
              copies[0].label().find("(changed)") != std::string::npos,
          "a variant file that matches no known variant is named as unknown");
    // The C64's and the Apple II's game isn't DOS's: needed is a copy that is, not the missing disk.
    for (const setup::Machine m : {setup::Machine::C64, setup::Machine::AppleII}) {
        setup::OtherCopy c;
        c.machine = m, c.disks = 2, c.found = {1};
        check(!c.same_game() && c.needed().size() == 1 && c.needed()[0].rfind("a copy of DOS", 0) == 0 &&
                  c.note().find("can't give the same game") != std::string::npos &&
                  c.note().find("missing") == std::string::npos,
              "its note says why it can't be played and what is needed");
    }
    setup::OtherCopy amiga;
    amiga.machine = setup::Machine::Amiga, amiga.version = "2.3", amiga.found = {1};
    check(amiga.same_game() && amiga.note().find("towns") == std::string::npos, "the Amiga's game is the same one");
    const std::vector<setup::KnownDisk> one{
        {setup::Machine::C64, "", 1, 1, {{"fixa", a_data.size(), ha.c_str()}}, {{"", {"fixb", b_data.size(), hb.c_str()}}}},
    };
    copies = setup::recognise(setup::seen_files(loose.string(), one, nullptr), one);
    check(copies.size() == 1 && copies[0].missing().empty() && copies[0].variants == std::vector<std::string>{""} &&
              copies[0].note().rfind("The Commodore 64 version was found", 0) == 0,
          "the whole version, plain variant, named in the note");
#if REKIT_HAVE_LIBARCHIVE
    const fs::path inner = work / "inner.zip", outer = work / "zipped" / "outer.zip";
    fs::create_directories(outer.parent_path());
    check(make_zip(inner, {{"disks/fixture.d64", loose}, {"readme.txt", {}}}) && make_zip(outer, {{"inner.zip", inner}}),
          "the made-up image zipped, and that zip zipped");
    copies = setup::recognise(setup::seen_files(outer.string(), one, nullptr), one);
    check(copies.size() == 1 && copies[0].missing().empty(), "found inside a zip inside a zip");
#endif
    check(setup::recognise(setup::seen_files((work / "loose").string(), {}, nullptr), {}).empty(),
          "nothing named against an empty table");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return std::fprintf(stderr, "usage: setup_test <work folder> [<game folder>]\n"), 2;
    const fs::path work = argv[1];
    std::error_code ec;
    fs::remove_all(work, ec);  // the test's own folder (the build's setup_work)
    fs::create_directories(work);
    game_free(work / "free");
    in_the_way(work / "way");
    old_names(work / "names");
    other_versions(work / "versions");
    const std::string game = argc == 3 ? argv[2] : "";
    if (!game.empty() && !rekit::any_case_path(game, "BTECH.EXE").empty()) with_game(work / "game", game);
    else std::printf("skip the game parts: no game folder\n");
    const char* amiga = std::getenv("CHR_AMIGA_COPY");
    std::error_code e2;
    if (amiga && fs::exists(amiga, e2)) with_amiga(work / "with-amiga", amiga);
    else std::printf("skip the Amiga parts: no Amiga copy (CHR_AMIGA_COPY)\n");
    fs::remove_all(work, ec);  // nothing of the game's kept
    std::printf("%s (%d failed)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
