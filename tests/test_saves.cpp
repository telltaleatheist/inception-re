// A sitting (platform/sitting.h) and its saves (platform/save_files.h): the
// window's loop plays the Town again for every menu key, from the start or from
// the last place kept, and what it read and wrote must be done once
// (docs/resuming.md). These play the game, so they need its folder; what needs
// none is in test_save_files.cpp. tests/keep_places_test.py plays the rig's
// scripts both ways.

#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "formats/game_files.h"
#include "formats/game_source.h"
#include "formats/import.h"
#include "formats/mtp.h"
#include "formats/rules.h"
#include "formats/tables.h"
#include "game/save.h"
#include "game/save_store.h"
#include "game/text_provider.h"
#include "game/town.h"
#include "platform/save_files.h"
#include "platform/sitting.h"
#include "save_disk.h"
#include "test.h"

using namespace chr;
using namespace chr::game;
using chrtest::Bytes;
using chrtest::did;
using chrtest::Disk;
using platform::SaveJournal;
using platform::Sitting;

namespace {

struct Game {
    formats::MtpMap map;
    std::unique_ptr<TextTable> imported;
    std::unique_ptr<MapText> map_text;
    std::unique_ptr<LayeredText> texts;
    std::shared_ptr<const formats::GameSource> source;
    formats::Rules rules;

    static const Game* get() {
        static const auto game = []() -> std::unique_ptr<Game> {
            const auto dir = chrtest::game_dir();
            if (dir.empty() || !chrtest::tables()) return nullptr;
            auto g = std::make_unique<Game>();
            g->rules = formats::read_rules(chrtest::imported_data_dir());
            g->source = std::make_shared<const formats::DosGameSource>(dir);
            g->map = formats::decode_mtp(*g->source->map(1));
            g->imported = std::make_unique<TextTable>(TextTable::load(formats::imported_text_path(chrtest::imported_data_dir())));
            g->map_text = std::make_unique<MapText>(g->map, 1);
            g->texts = std::make_unique<LayeredText>(std::vector<const TextProvider*>{g->imported.get(), g->map_text.get()});
            return g;
        }();
        return game.get();
    }

    std::unique_ptr<Town> begun(SaveStore& saves, bool start_up) const {
        auto town = std::make_unique<Town>(map, 1, new_game_position(), *texts, *chrtest::tables());
        town->set_source(source);
        town->set_saves(&saves);
        town->set_rules(&rules);
        if (start_up) town->start_up();
        else town->draw_panel(true);
        return town;
    }

    std::unique_ptr<Town> play(const std::deque<ScriptItem>& keys, SaveStore& saves, bool start_up) const {
        auto town = begun(saves, start_up);
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

namespace {

bool same(const TextCell& a, const TextCell& b) {
    return a.kind == b.kind && a.code == b.code && a.fg == b.fg && a.bg == b.bg && a.bars == b.bars && a.box_marks == b.box_marks;
}

// Everything a later key could depend on, as far as it can be seen.
bool same(const Town& a, const Town& b) {
    if (a.save_block() != b.save_block() || a.position() != b.position() || a.random().state() != b.random().state()) return false;
    if (a.ticks() != b.ticks() || a.question_open() != b.question_open() || a.place_open() != b.place_open()) return false;
    for (int row = 0; row < kTextRows; ++row)
        for (int col = 0; col < kTextColumns; ++col)
            if (!same(a.text().cell(col, row), b.text().cell(col, row))) return false;
    return true;
}

// A walk into another area, the command menu, a place and back out, an idle stretch.
const std::string kWalk = "left*12 idle:40 down*3 space down*2 enter space left*30 up*4 idle:25 space down*8 enter";

}  // namespace

// A copy is the Town it was made from and its own: neither has anything of the other's, whichever goes on first.
TEST(town_copied_at_the_main_loop) {
    if (!Game::get()) return chrtest::skip("no game folder (set CHR_GAME_DIR)");
    Disk disk;
    const auto items = parse_key_script(kWalk);
    int copies = 0;
    for (std::size_t n = 1; n < items.size(); ++n) {
        const std::deque<ScriptItem> first(items.begin(), items.begin() + std::ptrdiff_t(n)), rest(items.begin() + std::ptrdiff_t(n), items.end());
        auto town = Game::get()->play(first, disk, false);
        if (!town->at_main_loop()) continue;
        ++copies;
        auto copy = town->copy();
        CHECK(same(*town, *copy) && copy->terrain().placed_map() == &copy->map() && town->terrain().placed_map() == &town->map());
        auto again = copy->copy();
        ScriptKeys a(rest), b(rest);
        town->run(a);
        CHECK(!same(*town, *copy) && same(*copy, *again));
        const auto straight = Game::get()->play(items, disk, false);
        CHECK(same(*town, *straight));
        again.reset();
        town.reset();
        copy->run(b);
        CHECK(same(*copy, *straight));
    }
    CHECK(copies > 20);
    bool refused = false;
    try {
        Game::get()->play(parse_key_script("space"), disk, false)->copy();
    } catch (const std::logic_error&) {
        refused = true;
    }
    CHECK(refused);
}

// A sitting as the window's loop has it, the place kept wherever it can be: the same game as one straight run, the
// saves done once, and the Town begun once only.
TEST(sitting_keeps_its_place) {
    const auto before = one_save();
    if (!before) return;
    const std::string script = kLoad + " enter left*3 space down enter enter " + kWalk;
    const auto items = parse_key_script(script);
    Disk disk = *before;
    SaveJournal journal(disk);
    int begun = 0, kept = 0;
    Sitting sitting([&] { return ++begun, Game::get()->begun(journal, false); }, journal);
    CHECK(!sitting.town() && !sitting.keep_place());
    sitting.play();
    for (std::size_t n = 0; n < items.size(); ++n) {
        const bool can = sitting.town()->at_main_loop();
        CHECK(sitting.keep_place() == can);
        if (can) CHECK(sitting.time() == 0 && sitting.town()->events().empty() && sitting.town()->log().empty());
        kept += can;
        sitting.take(items[n]);
        Disk once = *before;
        const auto straight = Game::get()->play({items.begin(), items.begin() + std::ptrdiff_t(n) + 1}, once, false);
        CHECK(same(*sitting.town(), *straight) && disk.files == once.files && disk.actions == once.actions);
        for (int again = 0; again < 2; ++again) {
            sitting.play();
            CHECK(same(*sitting.town(), *straight) && disk.actions == once.actions);
        }
    }
    CHECK(begun == 1 && kept > 20 && !journal.strayed());
}

// A long sitting: a question's key plays again what came since the last key of the main loop, no more.
TEST(sitting_plays_again_from_the_place) {
    if (!Game::get()) return chrtest::skip("no game folder (set CHR_GAME_DIR)");
    Disk disk;
    SaveJournal journal(disk);
    Sitting sitting([&] { return Game::get()->begun(journal, false); }, journal);
    sitting.play();
    for (int i = 0; i < 200; ++i) {
        sitting.keep_place();
        sitting.key(i % 2 ? key::kLeft : key::kRight);
        for (int j = 0; j < 12; ++j) sitting.pass();
    }
    CHECK(sitting.time() == 12);
    sitting.keep_place();
    const int ticks = sitting.town()->ticks();
    sitting.key(key::kSpace);
    CHECK(sitting.town()->place_open() || sitting.town()->question_open());
    for (int i = 0; i < 5; ++i) sitting.key(key::kDown);
    CHECK(sitting.town()->ticks() <= ticks + 1 && !sitting.keep_place());
    // The log since the place is all a replay has: it did not play the walk again.
    CHECK(sitting.town()->log().size() < 20);
}

// Keys pressed while what the logic showed is played wait for the next play: no place is kept past them.
TEST(sitting_keys_meanwhile) {
    if (!Game::get()) return chrtest::skip("no game folder (set CHR_GAME_DIR)");
    Disk disk;
    SaveJournal journal(disk);
    Sitting sitting([&] { return Game::get()->begun(journal, false); }, journal);
    sitting.play();
    sitting.keep_place();
    sitting.key(key::kLeft);
    sitting.add({key::kLeft, 0});
    sitting.add({key::kLeft, 0});
    CHECK(sitting.town()->at_main_loop() && !sitting.keep_place());
    sitting.play();
    const auto straight = Game::get()->play(parse_key_script("left*3"), disk, false);
    CHECK(same(*sitting.town(), *straight));
    CHECK(sitting.keep_place());
    sitting.add_time(30);
    CHECK(!sitting.keep_place());
    sitting.add_time(10);
    sitting.play();
    CHECK(sitting.time() == 40 && same(*sitting.town(), *Game::get()->play(parse_key_script("left*3 idle:40"), disk, false)));
}

// What throws leaves no Town, and the next play has one again.
TEST(sitting_after_a_throw) {
    if (!Game::get()) return chrtest::skip("no game folder (set CHR_GAME_DIR)");
    Disk disk;
    SaveJournal journal(disk);
    bool bad = true;
    Sitting sitting([&] {
        if (bad) throw std::runtime_error("made up");
        return Game::get()->begun(journal, false);
    }, journal);
    bool threw = false;
    try {
        sitting.play();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw && !sitting.town() && !sitting.keep_place());
    bad = false;
    sitting.play();
    CHECK(sitting.keep_place());
    ScriptItem wrong;
    wrong.tune_end = true;
    threw = false;
    try {
        sitting.take(wrong);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw && !sitting.town());
}
