// The program's start-up, from the first picture to the town
// (docs/startup.md). The hardware questions the original asks first in text
// mode (the graphics adapter, the disk drives) belong to the platform and
// are not rebuilt.
#include "formats/game_files.h"
#include "game/map_view.h"
#include "game/money.h"
#include "game/records.h"
#include "game/town.h"

#include <stdexcept>

namespace chr::game {

namespace {

// BTECH.EXE 1527:01e6: the Infocom screen stays up for at most 700 display
// frames (one 273d:0006 wait each), or until a key.
constexpr int kInfocomFrames = 700;

}  // namespace

void Town::start_up() { stage_ = Stage::Infocom; }

DemoKeys::DemoKeys(std::vector<std::uint8_t> bytes, ScriptKeys& time) : bytes_(std::move(bytes)), time_(&time) {
    if (bytes_.size() > kMaxBytes) bytes_.resize(kMaxBytes);
}

std::optional<int> DemoKeys::next() {
    // Past the data the program reads whatever the buffer holds; the
    // rebuild ends the demo there.
    if (pos_ >= bytes_.size()) {
        ended_ = true;
        return std::nullopt;
    }
    // The pauses (273d:0006(30) each), then 287f:0b8a: a key the player
    // pressed is read and dropped, and ends the demo (DG:0152); then
    // 273d:0006(1).
    if (stop_at_ && pos_ >= *stop_at_) return std::nullopt;
    if (!wait(frames_to_next())) return std::nullopt;
    if (player_key_) ended_ = true;
    player_key_ = false;
    while (bytes_[pos_] == kPause) ++pos_;
    const int b = bytes_[pos_++];
    if (b == kEndMark) ended_ = true;
    return int(std::int8_t(b));
}

bool DemoKeys::wait(int frames) {
    while (frames_ < frames) {
        const auto item = time_->next_item();
        if (!item) return false;
        if (item->is_set() || item->tune_end) throw std::invalid_argument("a set: or tune-end item in the demo");
        if (item->idle > 0) {
            frames_ += item->idle;
            continue;
        }
        // A key: it waits for the next read, which ends the demo.
        player_key_ = true;
        frames_ = frames;
    }
    frames_ -= frames;
    return true;
}

std::optional<bool> DemoKeys::poll() {
    // 273d:002f takes nothing, and the flush that follows a wait (1000:2a2b)
    // throws nothing away while the demo plays: the key is the next read's.
    return true;
}

int DemoKeys::frames_to_next() const {
    int frames = 1;
    for (std::size_t i = pos_; i < bytes_.size() && bytes_[i] == kPause; ++i) frames += kPauseFrames;
    return frames;
}

// BTECH.EXE 1000:51f9..525c (in Ghidra FUN_1000_50c8)
// The demo's start: DEMOFILE into the key buffer, the random numbers set to
// 25 13 90, F:3938 set, and a new game (1000:4dc7). The disk prompt
// (1000:28cc(2)) has nothing to do, and the F:3988 it sets the new game
// clears again.
void Town::start_demo(ScriptKeys& input) {
    std::vector<std::uint8_t> bytes;
    if (game_dir_)
        if (const auto path = formats::find_game_file(*game_dir_, "DEMOFILE")) bytes = formats::read_file(*path);
    if (bytes.empty()) log_.push_back("demo: no DEMOFILE, so only the end mark");
    random_ = Random({0x25, 0x13, 0x90});  // 1000:522c
    demo_.emplace(std::move(bytes), input);
    if (demo_stop_) demo_->stop_at(*demo_stop_);
    demo_ran_ = true;
    new_game();
    // 1000:0024: the main loop's 1000:48b7(0x0f, 0) draws first.
    random_.next();
    stage_ = Stage::Demo;
    log_.push_back("demo");
}

// BTECH.EXE 1000:0000(1) (Ghidra FUN_1000_0000) under the demo
// 273d:002f always finds a key, so every pass is a key half (1000:002d) with
// the demo's next key, and no pass idles. After the pass in which DG:0152 is
// set (1000:050d), the title and its tune come again (1000:46a7, 476d).
// The demo's keys keep the time (DemoKeys).
bool Town::run_demo() {
    while (stage_ == Stage::Demo) {
        const auto k = demo_->next();
        if (!k && !demo_->ended()) return false;
        if (k) key(*k, *demo_);
        if (stopped()) return false;  // the time ran out inside the pass
        if (!demo_->ended()) continue;
        demo_.reset();  // 1000:476d clears F:3938
        stage_ = Stage::Title;
        log_.push_back("demo end");
    }
    return true;
}

// BTECH.EXE 1000:476d (Ghidra FUN_1000_476d)
// Reads WWOODBT.SIF whole, ends it with four 0 bytes, and plays it on the
// speaker (on Tandy graphics, DG:4fba = 1, its three voices instead: not
// rebuilt, docs/sound.md) until a key or its end.
std::vector<MusicStep> Town::title_tune() const {
    if (!game_dir_) return {};
    const auto path = formats::find_game_file(*game_dir_, "WWOODBT.SIF");
    if (!path) return {};
    const auto bytes = formats::read_file(*path);
    return tune_steps(bytes, tables_.music_note_hz);
}

std::optional<AssetId> Town::full_screen() const {
    if (stage_ == Stage::Infocom) return assets::kInfocomLogo;
    if (stage_ == Stage::Title) return assets::kTitleScreen;
    return screen_picture_;
}

// BTECH.EXE 1000:4dc7 (Ghidra FUN_1000_4dc7), as far as the screen goes: a
// new game's panel. Then 1000:5154..51b6: the notice in window 6 and the
// first-time question.
void Town::new_game_screen() {
    text_.clear_all();  // 287f:1fbe
    draw_panel(true);   // 1000:4cac(1)
    clear_window();     // 2656:0388
    text_.select_window(6);
    text_.clear_window();
    text_.frame(0);
    for (const char* id : {"intro.notice.0", "intro.notice.1", "intro.notice.2", "intro.notice.3",
                           "intro.first_time"})
        print_id(id);
}

// BTECH.EXE 1000:4ec5..4edd (in Ghidra FUN_1000_4dc7)
// A new game marks part of the seen map, around the starting town, as seen.
void Town::preset_seen(std::array<std::uint8_t, kSeenBytes>& s) {
    for (int r = 2; r < 8; ++r) s[std::size_t(0x60c + 0x10 * r)] |= 0x1f;
}

// BTECH.EXE 1000:4dc7 (Ghidra FUN_1000_4dc7)
// A new game from the game-over question: the party, its 'Mechs, the flags
// and the money as at the start; the bays' sprite bases; the panel; map 1
// in the middle area with the party at the Citadel; then the main loop
// goes on (docs/startup.md, "The new game"). What it does not reset stays.
void Town::new_game() {
    held_mechs_.fill(0xff);
    for (auto& m : mechs_) m[0] = 0xff;
    for (int i = 1; i < 8; ++i) characters_[std::size_t(i)][character::kName] = character::kEmpty;
    characters_[0] = new_game_characters()[0];  // every byte of Jason's record
    flags_ = new_game_flags();                  // F:d30c-d36f 0, then F:d33f = 50
    item_used_.fill(0);
    money_ = Money{};
    next_name_ = 1;
    preset_seen(seen_);  // 4ec5; the rest of the seen map stays
    figs_.sprite_base[4] = 0x96;
    for (int f = 16; f < 24; ++f) figs_.sprite_base[std::size_t(f)] = 0xfe;
    // 4f30: the 'Mech figures 0-3 and 12-15, each 0x92 on an odd draw.
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 13; j += 12)
            if (random_.next() & 1) figs_.sprite_base[std::size_t(i + j)] = 0x92;
    kits_ = {};  // F:d450, d451 (4f7e)
    marks_.next = 0;
    for (auto& m : marks_.marks) m.area = m.x = m.y = 0;  // the codes stay
    text_.clear_all();  // 287f:1fbe
    draw_panel(true);   // 1000:4cac(1)
    clear_window();    // 2656:0388
    position_ = new_game_position();
    terrain_.generate(0xcc);  // 287f:104e
    load_town_map(1, 4);      // 1000:2da8(4, 1), 287f:1da8
    build_window();           // 287f:1314, 18ef
    for (int f = 0; f < 12; ++f) {
        animation_.set_frame(f, f < 4 ? 0 : 0x10);  // F:409a; F:40a6 not modelled
        animation_.set_direction(f, -1);            // F:396c; F:3978 not modelled
    }
    record_party_places();  // 1000:051b
    area_maps_[0x34] = 0;   // 27e8:0064
    game_won_ = false;      // DG:01a8
    bays_drawn_ = false;    // F:398e
    agent_found_ = false;   // F:374a
    area_maps_[0xcc] = 1;   // 27e8:00fc
    combat_.fight_on = true;  // DG:014a
    // The map is drawn on the page, which only the next tick's flip shows:
    // until then the screen's map is as 287f:1fbe left it, black.
    frozen_ = Frozen{MapView{}, {}};
    tick_redraws_ = true;
    text_.forget(13, 0, kTextColumns - 13, kTextRows);
    log_.push_back("new game");
}

// BTECH.EXE 1527:0131..0216 (the Infocom screen, in Ghidra FUN_1527_0044),
// 1000:46a7 (the title), and 1000:50c8 (Ghidra FUN_1000_50c8):
// the title's tune and the questions.
bool Town::run_start_up(ScriptKeys& input) {
    while (stage_ == Stage::Infocom) {
        const auto item = input.next_item();
        if (!item) return false;
        if (item->is_set()) throw std::invalid_argument("a set: item in the start-up");
        if (item->idle == 0) {
            stage_ = Stage::Title;  // a key ends it (and is not used for anything else)
        } else {
            infocom_frames_ += item->idle;
            if (infocom_frames_ >= kInfocomFrames) stage_ = Stage::Title;
        }
    }
    // BTECH.EXE 1000:5126..5276 (in Ghidra FUN_1000_50c8): the title and its
    // tune (1000:476d) until a key; when the tune runs out first, the demo,
    // then the title again.
    while (stage_ == Stage::Title || stage_ == Stage::Demo) {
        if (stage_ == Stage::Demo) {
            if (!run_demo()) return false;
            continue;
        }
        const auto item = input.next_item();
        if (!item) return false;
        if (item->is_set()) throw std::invalid_argument("a set: item in the start-up");
        if (item->tune_end) {
            start_demo(input);
            continue;
        }
        if (item->idle != 0) continue;  // the tune plays on
        // 1000:5131: a key is waiting; 1000:2a2b throws it away. A new game
        // (1000:4dc7): the Town as made is one, unless a demo has played.
        log_.push_back("title");
        if (demo_ran_) new_game();
        stage_ = Stage::Question;
        new_game_screen();
    }
    // The first-time question (intro.first_time): Yes goes to the town; No asks
    // whether to load a saved game.
    const auto first = yes_no(true, input);
    if (!first) return false;
    if (!*first) {
        print_id("intro.load_old_game");
        const auto load = yes_no(true, input);
        if (!load) return false;
        // 1000:51dd: the command menu's load (1000:32b3), over the new game
        // 1000:5142 set up before the questions; then the main loop.
        if (*load) {
            stage_ = Stage::Town;
            log_.push_back("new game");
            return load_game(input);
        }
    }
    log_.push_back("new game");
    stage_ = Stage::Town;
    return true;
}

}  // namespace chr::game
