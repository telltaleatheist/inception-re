#include "game/mech_stats.h"

#include <algorithm>
#include <string>

#include "game/text_provider.h"
#include "game/town.h"

namespace chr::game {

namespace {

// 15ab:1afe's frame counter and pulse step are stack words it never sets,
// which vary from run to run; these are what two runs held (docs/town_loop.md).
constexpr int kFirstFrame = 0, kFirstStep = 3;
constexpr int kFramesPerStep = 16;

}  // namespace

// BTECH.EXE 15ab:1afe (Ghidra FUN_15ab_1afe)
// The 'Mech stats screen for figure `fig` (a party 'Mech 0-3, or an enemy
// 12-15, whose record is fig - 8): BTSTATS.CMP with the record's name,
// tonnage, crew, pips, weapons and actuators written over it, then its bars
// drawn once a frame, with the screen's pulsing colour, until a key. False if
// the keys ran out (the screen stays shown).
bool Town::mech_stats(int fig, KeySource& keys) {
    const int m = fig >= 12 ? fig - 8 : fig;
    const auto& rec = mechs_[std::size_t(m)];
    // 2656:0281, 0388: window 4, which stays the current window.
    text_.select_window(4);
    clear_window();
    // 1000:28cc(1) asks for the disk with BTSTATS.CMP unless F:4594 is set;
    // there are no disks, and the rebuild keeps the place's script apart from
    // the buffer the picture is read into (so it has no F:4594). The picture
    // covers the whole screen and everything on it.
    text_.clear_all();
    // 273d:00d5: text onto the screen on black; a carriage return goes back
    // to the first column, a row down.
    const auto put = [this](const std::string& text, int col, int row, TextColour fg) {
        std::size_t start = 0;
        for (;;) {
            const auto end = text.find('\r', start);
            text_.put_text(text.substr(start, end - start), col, row++, fg, TextRole::Background);
            if (end == std::string::npos) break;
            start = end + 1;
        }
    };
    const auto put_id = [&](const std::string& id, int col, int row, TextColour fg) {
        put(lookup(texts_, id), col, row, fg);
    };
    put_id("stats.labels", 0, 0, TextRole::Text);
    // The name, cut at eight characters (DG:0012, cut at DG:001a), and the
    // tonnage in decimal.
    std::string name;
    for (int i = 0; i < 8 && rec[std::size_t(i)] != 0; ++i) name += char(rec[std::size_t(i)]);
    put(name, 6, 0, TextRole::StatValue);
    put(std::to_string(rec[mech::kTonnage]), 6, 1, TextRole::StatValue);
    // A party 'Mech's pilot and second (by name); an enemy's are unknown.
    const auto crew_name = [&](int c) -> std::string {
        if (c >= int(characters_.size())) {
            log_.push_back("the stats screen: crew record " + std::to_string(c) + " past the table (not printed)");
            return {};
        }
        const int n = characters_[std::size_t(c)][character::kName];
        if (n == character::kEmpty) {
            log_.push_back("the stats screen: crew record " + std::to_string(c) + " empty (not printed)");
            return {};
        }
        return lookup(texts_, "character." + std::to_string(n));
    };
    if (m < 4) {
        put(crew_name(rec[mech::kPilot]), 6, 2, TextRole::StatValue);
        if (rec[mech::kSecond] != 0xff) put(crew_name(rec[mech::kSecond]), 6, 3, TextRole::StatValue);
        else put_id("stats.none", 6, 3, TextRole::StatValue);
    } else {
        put_id("stats.unknown", 6, 2, TextRole::StatValue);
    }
    // 15ab:1858's pips: what is whole of the engine, gyro and sensors, and
    // the heat sinks (the engine's own and one for each heat sink item).
    StatsScene scene;
    scene.engine = {3, 3 - rec[mech::kEngineHits]};
    scene.gyro = {2, 2 - rec[mech::kGyroHits]};
    scene.sensors = {2, 2 - rec[mech::kSensorHits]};
    int sinks = rec[mech::kEngineSinks];
    for (int i = mech::kItems; i < mech::kItemsEnd; ++i)
        if (rec[std::size_t(i)] == mech::kHeatSinkItem) ++sinks;
    scene.heat_sinks = {10, sinks};
    // The weapons from row 6, each with its location by its slot; grey when
    // destroyed (bit 7).
    int row = 6;
    for (int i = mech::kItems; i < mech::kItemsEnd; ++i) {
        const int item = rec[std::size_t(i)] & 0x7f;
        if (item < 0x10 || item > 0x20) continue;
        const TextColour fg = rec[std::size_t(i)] & 0x80 ? TextColour(TextRole::Destroyed) : TextRole::StatValue;
        put_id(formats::weapon_text_id(item - 1), 0, row, fg);
        static constexpr int kLocationStarts[] = {0x3a, 0x41, 0x48, 0x4f, 0x51, 0x53, 0x55};
        int location = 0;
        for (int start : kLocationStarts)
            if (i >= start) ++location;
        put_id("stats.location." + std::to_string(location), 11, row++, fg);
    }
    put_id("stats.actuators", 0, 0x13, TextRole::Text);
    // F:37fe = 14, never put back: the window's text stays yellow after.
    text_.set_fg(TextRole::StatValue);
    put_id("stats.actuator_names", 0, 0x14, TextRole::Text);
    // The legs (low nibbles) are whole at f, the arms (high nibbles) when they
    // match bytes 0x69 and 0x6a.
    const auto actuator = [&](int value, int whole, int row) {
        if (value == whole) put_id("stats.ok", 10, row, TextRole::Text);
        else if (value == 0) put_id("stats.gone", 10, row, TextRole::Destroyed);
        else put_id("stats.hit", 10, row, TextRole::Text);
    };
    actuator(rec[mech::kLeftActuators] & 0x0f, 0x0f, 0x14);
    actuator(rec[mech::kRightActuators] & 0x0f, 0x0f, 0x15);
    actuator(rec[mech::kLeftActuators] & 0xf0, rec[0x69] & 0xf0, 0x16);
    actuator(rec[mech::kRightActuators] & 0xf0, rec[0x6a] & 0xf0, 0x17);

    // The bars: each location's armour above the structure DG:1306 names.
    for (int i = 0; i < StatsScene::kBars; ++i) {
        const int structure = tables_.stats_bar_structure[std::size_t(i)];
        scene.bars[std::size_t(i)] = {rec[std::size_t(mech::kArmour + i)], structure ? rec[std::size_t(structure)] : 0};
    }
    int frame = kFirstFrame, step = kFirstStep;
    int demo_frames = 0x258;  // 15ab:21b7
    for (;;) {
        // The heat, kept to 0-30 in the record's own place; with any, its
        // part of the bar blinks: a change every 10 - heat / 6 draws.
        auto& heat = combat_.heat[std::size_t(m)];
        heat = std::int8_t(std::clamp<int>(heat, 0, StatsScene::kHeatMax));
        scene.heat = heat;
        if (heat != 0) {
            if (heat_blink_ == 0) heat_blink_ = 1;
            if (--heat_blink_count_ < 0) {
                heat_blink_count_ = 10 - heat / 6;  // DG:121a
                heat_blink_ = 3 - heat_blink_;
            }
            scene.heat_flash = heat_blink_ == 2;
        }
        // 273d:0006: a frame. Every 16th the screen's colours are set, with
        // colour 4 as DG:134c holds it, and DG:134c moves on a step.
        if (++frame == kFramesPerStep) {
            scene.pulse = stats_pulse_;
            step = (step + 1) & 3;
            stats_pulse_ = step;
        }
        frame &= kFramesPerStep - 1;
        stats_ = scene;
        // 273d:002f, then 273d:0259 for the key it found. While the demo
        // plays (F:3938, 15ab:22c5) it shows 600 frames, then reads the
        // demo's next key.
        if (demo_) {
            if (!demo_->wait(1)) {
                place_open_ = true;
                return false;
            }
            if (--demo_frames != 0) continue;
        }
        const auto key = keys.waiting_key();
        if (!key) {
            place_open_ = true;
            return false;
        }
        if (*key != KeySource::kNoKey) break;
    }
    // 37e8:0000: the standard colours again.
    stats_.reset();
    return true;
}

}  // namespace chr::game
