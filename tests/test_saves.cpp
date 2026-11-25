// The saves of a sitting (platform/save_files.h): the window's loop plays the
// Town again from the start for every menu key, and what it read and wrote
// must be done once (docs/resuming.md).

#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "formats/game_files.h"
#include "formats/game_source.h"
#include "formats/import.h"
#include "formats/mtp.h"
#include "formats/tables.h"
#include "game/save.h"
#include "game/save_store.h"
#include "game/text_provider.h"
#include "game/town.h"
#include "platform/save_files.h"
#include "test.h"

using namespace chr;
using namespace chr::game;
using platform::SaveFiles;
using platform::SaveJournal;

namespace {

using Bytes = std::vector<std::uint8_t>;

// Saves in memory, with everything done to them.
struct Disk : SaveStore {
    std::map<int, Bytes> files;
    std::set<int> full;                // the writes (by count) that fail
    std::vector<std::string> actions;  // "r1", "w1": the slot
    int writes = 0;
    std::optional<Bytes> read(int slot) override {
        actions.push_back("r" + std::to_string(slot + 1));
        const auto it = files.find(slot);
        if (it == files.end()) return std::nullopt;
        return it->second;
    }
    bool write(int slot, std::span<const std::uint8_t> file) override {
        actions.push_back("w" + std::to_string(slot + 1));
        if (full.contains(writes++)) return false;
        files[slot] = Bytes(file.begin(), file.end());
        return true;
    }
};

struct Game {
    formats::MtpMap map;
    std::unique_ptr<TextTable> imported;
    std::unique_ptr<MapText> map_text;
    std::unique_ptr<LayeredText> texts;
    std::shared_ptr<const formats::GameSource> source;

    static const Game* get() {
        static const auto game = []() -> std::unique_ptr<Game> {
            const auto dir = chrtest::game_dir();
            if (dir.empty() || !chrtest::tables()) return nullptr;
            auto g = std::make_unique<Game>();
            g->source = std::make_shared<const formats::DosGameSource>(dir);
            g->map = formats::decode_mtp(*g->source->map(1));
            g->imported = std::make_unique<TextTable>(TextTable::load(formats::imported_text_path(chrtest::imported_data_dir())));
            g->map_text = std::make_unique<MapText>(g->map, 1);
            g->texts = std::make_unique<LayeredText>(std::vector<const TextProvider*>{g->imported.get(), g->map_text.get()});
            return g;
        }();
        return game.get();
    }

    std::unique_ptr<Town> play(const std::deque<ScriptItem>& keys, SaveStore& saves, bool start_up) const {
        auto town = std::make_unique<Town>(map, 1, new_game_position(), *texts, *chrtest::tables());
        town->set_source(source);
        town->set_saves(&saves);
        if (start_up) town->start_up();
        else town->draw_panel(true);
        ScriptKeys script(keys);
        town->run(script);
        return town;
    }
};

// The saved game's x, its last four bytes' first two.
int saved_x(const Bytes& b) { return b.size() == std::size_t(save::kFileSize) ? b[b.size() - 4] | b[b.size() - 3] << 8 : -1; }

// The menu: the items the runs use, from the one it opens on.
const std::string kSave = "space down*5 enter", kLoad = "space down*4 enter";  // the first time

// Plays `script` as the window's loop does, a key more each time and each
// time from the start, several times over; after every one the saves and
// what was done to them must be what one straight run of as many keys does.
// Returns the last of them.
Disk sitting(const Disk& before, const std::string& script, bool start_up = false) {
    const Game& game = *Game::get();
    const auto items = parse_key_script(script);
    Disk disk = before;
    SaveJournal journal(disk);
    int wrong = 0;
    for (std::size_t n = 0; n <= items.size(); ++n) {
        const std::deque<ScriptItem> keys(items.begin(), items.begin() + std::ptrdiff_t(n));
        Disk once = before;
        const auto straight = game.play(keys, once, start_up);
        for (int again = 0; again < 3; ++again) {
            journal.rewind();
            const auto town = game.play(keys, journal, start_up);
            if (disk.files != once.files || disk.actions != once.actions) ++wrong;
            if (town->position() != straight->position() || town->log() != straight->log()) ++wrong;
        }
    }
    if (wrong) chrtest::fail(__FILE__, __LINE__, "replays of \"" + script + "\": " + std::to_string(wrong) + " went wrong");
    CHECK(!journal.strayed());
    return disk;
}

bool did(const Disk& d, const std::vector<std::string>& actions) { return d.actions == actions; }

// A disk whose Game1 is a new game's.
std::optional<Disk> one_save() {
    if (!Game::get()) {
        chrtest::skip("no game folder (set CHR_GAME_DIR)");
        return std::nullopt;
    }
    Disk disk;
    Game::get()->play(parse_key_script(kSave + " enter"), disk, false);
    CHECK(disk.files.size() == 1 && saved_x(disk.files[0]) == 0x0c45);
    disk.actions.clear();
    disk.writes = 0;
    return disk;
}

}  // namespace

// Load a slot, walk, save over it, then questions: once a lost step for each.
TEST(saves_load_and_save_the_same_slot) {
    const auto before = one_save();
    if (!before) return;
    const std::string script = kLoad + " enter left*3 space down enter enter left*2 space enter";
    const auto after = sitting(*before, script);
    CHECK(did(after, {"r1", "w1"}));
    CHECK(saved_x(after.files.at(0)) == 0x0c42);
    // What it is for: without the journal every replay takes the walk again.
    Disk without = *before;
    for (int i = 0; i < 3; ++i) Game::get()->play(parse_key_script(script), without, false);
    CHECK(saved_x(without.files.at(0)) == 0x0c3c);
}

TEST(saves_load_and_save_again_and_again) {
    const auto before = one_save();
    if (!before) return;
    const auto after = sitting(*before, kLoad + " enter left*2 space down enter enter left space up enter enter left*3 "
                                                "space down enter enter space enter enter left idle:30 space");
    CHECK(did(after, {"r1", "w1", "r1", "w1", "w1"}));
    CHECK(saved_x(after.files.at(0)) == 0x0c45 - 5);
}

TEST(saves_load_one_slot_and_save_another) {
    const auto before = one_save();
    if (!before) return;
    const auto after = sitting(*before, kLoad + " enter left*3 space down enter down enter left space enter");
    CHECK(did(after, {"r1", "w2"}));
    CHECK(after.files.at(0) == before->files.at(0) && saved_x(after.files.at(1)) == 0x0c42);
}

TEST(saves_save_then_load_the_same_slot) {
    const auto before = one_save();
    if (!before) return;
    const auto after = sitting(*before, kSave + " enter left*3 space up enter enter left space down enter enter left space enter");
    CHECK(did(after, {"w1", "r1", "w1"}));
    CHECK(saved_x(after.files.at(0)) == 0x0c44);
}

// A load that finds nothing, the slot saved, and loaded: each as it was when
// it was done.
TEST(saves_missing_then_there) {
    const auto before = one_save();
    if (!before) return;
    const auto after = sitting(*before, kLoad + " down*2 enter enter left*2 space down enter down*2 enter left space up enter enter left space");
    CHECK(after.actions.size() == 3 && after.actions[0] == "r3" && after.actions[1].substr(0, 1) == "w");
    CHECK(after.files.at(0) == before->files.at(0));
}

TEST(saves_not_a_saved_game) {
    auto before = one_save();
    if (!before) return;
    before->files[0] = {0x00, 1, 2, 3};
    before->files[1] = {};
    const auto after = sitting(*before, kLoad + " enter enter left space down enter enter left space up enter down enter enter left space");
    CHECK(after.actions.size() >= 2 && after.actions[0] == "r1" && after.actions[1] == "w1");
    CHECK(after.files.at(1).empty());
}

// A write that fails, then one that doesn't: the failed one isn't tried again.
TEST(saves_failing_write) {
    auto before = one_save();
    if (!before) return;
    before->full = {0};
    const auto after = sitting(*before, kLoad + " enter left*3 space down enter enter enter left space enter enter left space");
    CHECK(did(after, {"r1", "w1", "w1"}));
    CHECK(after.writes == 2 && saved_x(after.files.at(0)) == 0x0c41);
}

// The start-up's question: an old game loaded, walked and saved over.
TEST(saves_start_up_load) {
    const auto before = one_save();
    if (!before) return;
    const auto after = sitting(*before, "enter enter n y enter left*3 space down*5 enter enter left space", true);
    CHECK(did(after, {"r1", "w1"}));
    CHECK(saved_x(after.files.at(0)) == 0x0c42);
}

// A file another program replaced is what a later load reads, and the
// earlier load, played again, reads what it read.
TEST(saves_replaced_meanwhile) {
    const auto before = one_save();
    if (!before) return;
    Disk disk = *before;
    SaveJournal journal(disk);
    const auto play = [&](const std::string& script) {
        journal.rewind();
        return Game::get()->play(parse_key_script(script), journal, false)->position().x;
    };
    const std::string loaded = kLoad + " enter left*3";
    CHECK(play(loaded) == 0x0c42);
    Disk other;
    Game::get()->play(parse_key_script("left*9 " + kSave + " enter"), other, false);
    disk.files[0] = other.files[0];
    CHECK(play(loaded) == 0x0c42);
    CHECK(play(loaded + " space enter enter") == 0x0c3c);
    CHECK(play(loaded + " space enter enter left") == 0x0c3b);
    CHECK(did(disk, {"r1", "r1"}) && disk.files[0] == other.files[0]);
}

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
