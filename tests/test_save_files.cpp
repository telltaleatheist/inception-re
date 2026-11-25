// The saves on disk and the journal that does each of their reads and writes
// once (platform/save_files.h). They need no game folder: the packaging runs
// them on every system (tools/build_package.py), against the code it packages.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#include <sys/resource.h>
#endif

#include "game/save.h"
#include "platform/save_files.h"
#include "save_disk.h"
#include "test.h"

#if defined(_WIN32)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace chr;
using chrtest::Bytes;
using chrtest::did;
using chrtest::Disk;
using platform::SaveFiles;
using platform::SaveJournal;

// What can't happen: a replay that asks for something else. The saves stay
// as they are, and every later write fails.
TEST(saves_journal_strays) {
    const Bytes a{1, 2, 3}, b{4, 5};
    Disk disk;
    disk.files[4] = a;
    SaveJournal journal(disk);
    CHECK(journal.read(4) == a && journal.write(2, a) && !journal.read(0) && !journal.strayed());
    journal.rewind();
    CHECK(journal.read(4) == a && journal.write(2, a) && !journal.read(0) && !journal.strayed());
    CHECK(did(disk, {"r5", "w3", "r1"}));
    // Other bytes: what was written stays, and the game may be saved again.
    journal.rewind();
    CHECK(journal.read(4) == a && journal.write(2, b) && journal.strayed() && disk.files[2] == a);
    CHECK(!journal.read(0) && journal.write(3, b) && disk.files[3] == b);
    // Another action.
    journal.rewind();
    CHECK(!journal.write(4, b) && disk.files[4] == a);
    CHECK(!journal.read(2));
    CHECK(!journal.read(0) && journal.write(3, b));
    CHECK(!journal.write(5, b) && !disk.files.contains(5));
    CHECK(did(disk, {"r5", "w3", "r1", "w4"}));
}

// On disk: the folder is made, a file is written whole, and what can't be
// read or written says so.
TEST(saves_files) {
    const auto dir = std::filesystem::path(CHR_TEST_SCRATCH) / "save-files";
    std::filesystem::remove_all(dir);
    SaveFiles files(dir / "deeper");
    CHECK(!files.read(0));
    CHECK(files.write(0, Bytes{1, 2, 3}) && std::filesystem::exists(dir / "deeper" / "Game1"));
    CHECK(files.read(0) == Bytes({1, 2, 3}));
    CHECK(files.write(0, Bytes{9}) && files.read(0) == Bytes({9}));
    std::filesystem::create_directories(dir / "deeper" / "Game2");
    CHECK(!files.write(1, Bytes{1}));
}

namespace {

namespace fs = std::filesystem;

// An empty folder of the test's own.
fs::path scratch(const char* name) {
    const auto dir = fs::path(CHR_TEST_SCRATCH) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

Bytes bytes_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

void put(const fs::path& path, const Bytes& bytes) {
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

std::vector<std::string> names(const fs::path& dir) {
    std::vector<std::string> out;
    for (const auto& e : fs::directory_iterator(dir)) out.push_back(e.path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

using Names = std::vector<std::string>;

// Saves on disk whose nth step of a write fails.
struct Failing : SaveFiles {
    using SaveFiles::SaveFiles;
    int at = -1;  // the step that fails, in order; -1: none
    int steps = 0;
    Bytes before;  // GameN when the failing step came
    fs::path file;
    bool may(Step) override {
        if (steps++ != at) return true;
        before = bytes_of(file);
        return false;
    }
};

}  // namespace

// Every byte value, and a saved game's size, as they are.
TEST(saves_written_as_they_are) {
    const auto dir = scratch("save-bytes");
    SaveFiles files(dir);
    const std::size_t size = game::save::kFileSize;
    Bytes all(size);
    for (std::size_t i = 0; i < all.size(); ++i) all[i] = std::uint8_t(i * 7 + (i >> 8));
    CHECK(files.write(0, all) && bytes_of(dir / "Game1") == all && names(dir) == Names{"Game1"});
    std::reverse(all.begin(), all.end());
    CHECK(files.write(0, all) && bytes_of(dir / "Game1") == all && names(dir) == Names{"Game1"});
    CHECK(files.write(0, {}) && bytes_of(dir / "Game1").empty() && names(dir) == Names{"Game1"});
}

// A step that fails leaves the save before it, or none, and nothing beside.
TEST(saves_each_step_failing) {
    for (int at = 0; at < 4; ++at) {
        const auto dir = scratch("save-steps");
        Failing files(dir);
        files.file = dir / "Game1";
        files.at = at;
        CHECK(!files.write(0, Bytes{4, 5}) && files.steps == at + 1 && names(dir).empty());
        put(dir / "Game1", {1, 2, 3});
        files.steps = 0;
        CHECK(!files.write(0, Bytes{4, 5}) && files.steps == at + 1);
        CHECK(files.before == Bytes({1, 2, 3}) && bytes_of(dir / "Game1") == Bytes({1, 2, 3}));
        CHECK(names(dir) == Names{"Game1"});
        files.at = -1;
        files.steps = 0;
        CHECK(files.write(0, Bytes{4, 5}) && files.steps == 4);
        CHECK(bytes_of(dir / "Game1") == Bytes({4, 5}) && names(dir) == Names{"Game1"});
    }
}

// The renaming itself refused: a folder has taken GameN's place meanwhile.
TEST(saves_not_replaced) {
    const auto dir = scratch("save-rename");
    struct Taken : SaveFiles {
        using SaveFiles::SaveFiles;
        fs::path file;
        bool may(Step step) override {
            if (step == Step::Replace) {
                fs::remove(file);
                fs::create_directories(file / "in it");
            }
            return true;
        }
    } files(dir);
    files.file = dir / "Game1";
    put(dir / "Game1", {1, 2, 3});
    CHECK(!files.write(0, Bytes{4, 5}) && names(dir) == Names{"Game1"} && fs::is_directory(dir / "Game1" / "in it"));
}

// A write cut short: what it left is written over. A .part that isn't a
// file is left alone, and the write fails.
TEST(saves_part_left) {
    const auto dir = scratch("save-part");
    SaveFiles files(dir);
    put(dir / "Game1", {1, 2, 3});
    put(dir / "Game1.part", {7, 7, 7, 7, 7});
    CHECK(files.read(0) == Bytes({1, 2, 3}));
    CHECK(files.write(0, Bytes{4, 5}) && bytes_of(dir / "Game1") == Bytes({4, 5}) && names(dir) == Names{"Game1"});
    fs::create_directories(dir / "Game1.part");
    CHECK(!files.write(0, Bytes{6}) && bytes_of(dir / "Game1") == Bytes({4, 5}));
    CHECK(names(dir) == Names({"Game1", "Game1.part"}));
}

// A save that may not be written stays, as does what isn't a file.
TEST(saves_read_only) {
    const auto dir = scratch("save-read-only");
    SaveFiles files(dir);
    put(dir / "Game1", {1, 2, 3});
    fs::permissions(dir / "Game1", fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                    fs::perm_options::remove);
    if (std::fstream(dir / "Game1", std::ios::in | std::ios::out)) {
        chrtest::skip("this user may write a read-only file");
    } else {
        CHECK(!files.write(0, Bytes{4, 5}) && bytes_of(dir / "Game1") == Bytes({1, 2, 3}));
        CHECK(names(dir) == Names{"Game1"});
    }
    fs::permissions(dir / "Game1", fs::perms::owner_write, fs::perm_options::add);
    fs::permissions(dir / "Game1", fs::perms::group_all | fs::perms::others_all, fs::perm_options::remove);
    const auto who = fs::status(dir / "Game1").permissions();
    CHECK(files.write(0, Bytes{4, 5}) && bytes_of(dir / "Game1") == Bytes({4, 5}));
    CHECK(fs::status(dir / "Game1").permissions() == who);
    fs::create_directories(dir / "Game2");
    CHECK(!files.write(1, Bytes{1}) && names(dir) == Names({"Game1", "Game2"}));
}

// A link stays one, and the file it leads to is the one written.
TEST(saves_links) {
    const auto dir = scratch("save-links"), other = scratch("save-links-to");
    SaveFiles files(dir);
    put(other / "kept", {1, 2, 3});
    std::error_code ec;
    fs::create_symlink(fs::path("..") / "save-links-to" / "kept", dir / "Game1", ec);
    if (ec) {  // not everyone may make one on Windows
        chrtest::skip("no links here: " + ec.message());
        return;
    }
    fs::create_symlink(dir / "Game1", dir / "Game2");
    fs::create_symlink(other / "not yet", dir / "Game3");
    fs::create_symlink("Game4", dir / "Game4");
    CHECK(files.write(0, Bytes{4, 5}) && bytes_of(other / "kept") == Bytes({4, 5}) && fs::is_symlink(dir / "Game1"));
    CHECK(files.write(1, Bytes{6}) && bytes_of(other / "kept") == Bytes({6}) && fs::is_symlink(dir / "Game2"));
    CHECK(files.write(2, Bytes{7}) && bytes_of(other / "not yet") == Bytes({7}) && fs::is_symlink(dir / "Game3"));
    CHECK(!files.write(3, Bytes{8}) && fs::is_symlink(dir / "Game4"));
    fs::create_symlink(other / "kept", other / "not yet.part");
    CHECK(!files.write(2, Bytes{9}) && bytes_of(other / "kept") == Bytes({6}) && bytes_of(other / "not yet") == Bytes({7}));
    fs::remove(other / "not yet.part");
    CHECK(names(dir) == Names({"Game1", "Game2", "Game3", "Game4"}) && names(other) == Names({"kept", "not yet"}));
}

#if !defined(_WIN32)
// The disk's own failures: a write that doesn't fit, and a folder nothing may
// be made in.
TEST(saves_disk_failures) {
    const auto dir = scratch("save-disk");
    SaveFiles files(dir);
    const std::size_t size = game::save::kFileSize;
    const Bytes old(size, 1), now(size, 2);
    CHECK(files.write(0, old));
    rlimit was{};
    getrlimit(RLIMIT_FSIZE, &was);
    const auto handler = std::signal(SIGXFSZ, SIG_IGN);
    rlimit small = was;
    small.rlim_cur = 1000;
    setrlimit(RLIMIT_FSIZE, &small);
    const bool written = files.write(0, now);
    setrlimit(RLIMIT_FSIZE, &was);
    std::signal(SIGXFSZ, handler);
    CHECK(!written && bytes_of(dir / "Game1") == old && names(dir) == Names{"Game1"});
    fs::permissions(dir, fs::perms::owner_write, fs::perm_options::remove);
    const bool may = bool(std::ofstream(dir / "any"));  // a user who may write anything
    const bool made = files.write(0, now);
    fs::permissions(dir, fs::perms::owner_write, fs::perm_options::add);
    if (may) chrtest::skip("this user may write in a read-only folder");
    else CHECK(!made && bytes_of(dir / "Game1") == old && names(dir) == Names{"Game1"});
    fs::remove(dir / "any");
    CHECK(files.write(0, now) && bytes_of(dir / "Game1") == now);
}
#else
TEST(saves_disk_failures) {
    chrtest::skip("no limit on a file's size, and no folder nothing may be made in, on Windows");
}

// GameN held open by another program, by what it lets others do meanwhile.
TEST(saves_held_open) {
    const auto dir = scratch("save-held");
    SaveFiles files(dir);
    for (const DWORD share : {DWORD(FILE_SHARE_READ | FILE_SHARE_WRITE), DWORD(FILE_SHARE_READ), DWORD(0)}) {
        put(dir / "Game1", {1, 2, 3});
        const HANDLE held = CreateFileW((dir / "Game1").c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(held != INVALID_HANDLE_VALUE);
        CHECK(!files.write(0, Bytes{4, 5}));
        CHECK(names(dir) == Names{"Game1"});
        CloseHandle(held);
        CHECK(bytes_of(dir / "Game1") == Bytes({1, 2, 3}));
        CHECK(files.write(0, Bytes{4, 5}) && bytes_of(dir / "Game1") == Bytes({4, 5}) && names(dir) == Names{"Game1"});
    }
    // One that lets it be deleted too: Windows may replace it or not.
    put(dir / "Game1", {1, 2, 3});
    const HANDLE held = CreateFileW((dir / "Game1").c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(held != INVALID_HANDLE_VALUE);
    const bool written = files.write(0, Bytes{4, 5});
    std::printf("  held open with delete sharing: %s\n", written ? "replaced" : "refused");
    CloseHandle(held);
    CHECK(bytes_of(dir / "Game1") == (written ? Bytes({4, 5}) : Bytes({1, 2, 3})) && names(dir) == Names{"Game1"});
}
#endif

// A folder's name as a player's may be: spaces, letters no code page of
// Windows has together, and longer than its first part.
TEST(saves_folder_names) {
    const auto dir = scratch("save-names") / fs::path(u8"Inception Rebuild \u00e9\u0142\u65e5") / fs::path(u8"saves \u00fc");
    SaveFiles files(dir);
    CHECK(!files.read(0));
    CHECK(files.write(0, Bytes{1, 2, 3}) && fs::is_directory(dir) && names(dir) == Names{"Game1"});
    CHECK(files.write(0, Bytes{4, 5}) && files.read(0) == Bytes({4, 5}) && names(dir) == Names{"Game1"});
    Failing failing(dir);
    failing.file = dir / "Game1";
    failing.at = 3;
    CHECK(!failing.write(0, Bytes{6}) && files.read(0) == Bytes({4, 5}) && names(dir) == Names{"Game1"});
}

// The journal over the disk: each action is done once, and answered the same
// when played again, whatever happens to the files meanwhile.
TEST(saves_journal_on_disk) {
    const auto dir = scratch("save-journal");
    SaveFiles files(dir);
    SaveJournal journal(files);
    put(dir / "Game1", {1, 2, 3});
    fs::create_directories(dir / "Game3");
    const auto play = [&] {
        CHECK(journal.read(0) == Bytes({1, 2, 3}) && journal.write(0, Bytes{4, 5}));
        CHECK(!journal.read(1) && journal.write(1, Bytes{6}) && !journal.write(2, Bytes{7}));
    };
    play();
    CHECK(bytes_of(dir / "Game1") == Bytes({4, 5}) && bytes_of(dir / "Game2") == Bytes({6}));
    put(dir / "Game1", {8});
    fs::remove(dir / "Game2");
    fs::remove(dir / "Game3");
    for (int again = 0; again < 3; ++again) {
        journal.rewind();
        play();
        CHECK(bytes_of(dir / "Game1") == Bytes({8}) && names(dir) == Names{"Game1"} && !journal.strayed());
    }
    CHECK(journal.write(2, Bytes{9}) && bytes_of(dir / "Game3") == Bytes({9}));
}
