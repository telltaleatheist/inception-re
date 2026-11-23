// The program's start-up, from the first picture to the town
// (docs/startup.md). The hardware questions the original asks first in text
// mode (the graphics adapter, the disk drives) belong to the platform and
// are not rebuilt.
#include "formats/game_files.h"
#include "game/map_view.h"
#include "game/money.h"
#include "game/records.h"
#include "game/town.h"

namespace chr::game {

namespace {

// BTECH.EXE 1527:01e6: the Infocom screen stays up for at most 700 display
// frames (one 273d:0006 wait each), or until a key.
constexpr int kInfocomFrames = 700;

}  // namespace

void Town::start_up() { stage_ = Stage::Infocom; }

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
    draw_panel();
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
    draw_panel(true);  // 1000:4cac(1)
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
    if (stage_ == Stage::Title) {
        // 1000:476d plays the title's tune (WWOODBT.SIF) until a key; if the
        // tune ends first, the demo plays (DEMOFILE). docs/startup.md.
        if (!input.next()) return false;
        log_.push_back("title");
        stage_ = Stage::Question;
        new_game_screen();
    }
    // "Is this your first time playing?" Yes goes to the town; No asks
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
