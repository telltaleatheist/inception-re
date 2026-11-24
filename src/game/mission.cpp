// The training missions' driver (docs/combat.md 1.3): a mission's set-up,
// the walk it shares with every mode, and the results.
#include "formats/anm.h"
#include "formats/mtp.h"
#include "game/text_provider.h"
#include "game/tile_animation.h"
#include "game/town.h"

#include <cstdio>

namespace chr::game {

namespace {

// P:200a, the map store's byte 0xfed (block 63, row 5, column 5): the
// course clears it while it runs and puts it back after.
constexpr std::size_t kCourseCell = 0xfed;

// The door of the training center: a walk back to it ends a mission.
bool at_the_door(formats::MapPosition p) { return p.x <= 0x0c3c && p.y >= 0xc049 && p.y <= 0xc04f; }

}  // namespace

// The map store changes at once; the cell window (P:07ad) only when
// 1000:218f next builds it, unless `rebuild`.
void Town::set_map_cell(std::size_t index, int tile, bool rebuild) {
    map_.blocks[index] = std::uint8_t(tile);
    if (rebuild) {
        terrain_.map_changed();
    } else {
        window_stale_ = true;
    }
}

// BTECH.EXE 1d43:0c72 (Ghidra FUN_1d43_0c72)
// The Arena's wall: map-store cells 0xc00-0xc25 kept (DG:5804) and those
// the table names overwritten, or with `restore` the kept cells put back.
// Only the store is written; the window shows it when next built.
void Town::arena_wall(bool restore) {
    constexpr std::size_t kFirst = 0xc00;
    for (std::size_t i = 0; i < arena_wall_kept_.size() && kFirst + i < map_.blocks.size(); ++i) {
        if (restore) {
            set_map_cell(kFirst + i, arena_wall_kept_[i], false);
        } else {
            arena_wall_kept_[i] = map_.blocks[kFirst + i];
            if (const int tile = tables_.arena_wall[i]; tile != 0) set_map_cell(kFirst + i, tile, false);
        }
    }
}

// BTECH.EXE 17dc:0629 (Ghidra FUN_17dc_0629)
// Sets up the mission of `mode`, then walks: a key is a step, and without
// one the tiles animate and the townspeople move. A mission ends when the
// party is back at the training center's door once it is done; then the
// results.
bool Town::run_mission(int mode, KeySource& keys) {
    MissionWalk w;
    w.mode = mode;
    switch (mode) {
    case 0:
        if (map_.blocks.size() > kCourseCell) {
            w.saved_cell = map_.blocks[kCourseCell];
            set_map_cell(kCourseCell, 0);
        }
        w.cell = kCourseCell;
        // 1000:218f(0): a step on the spot.
        if (!step_party({0, 0}, keys)) return false;
        break;
    case 1: {
        // One of eight spots is marked with tile 0x50.
        w.spot = random_.next() & 7;
        const int sx = tables_.marked_spot_x[std::size_t(w.spot)];
        const int sy = tables_.marked_spot_y[std::size_t(w.spot)];
        w.cell = std::size_t(((sy & 0x78) << 6) + ((sy & 7) << 3) + ((sx & 0x78) << 3) + (sx & 7));
        log_.push_back("mission 1: spot " + std::to_string(w.spot) + " (" + std::to_string(sx) + ", " +
                       std::to_string(sy) + ")");
        if (map_.blocks.size() > w.cell) {
            w.saved_cell = map_.blocks[w.cell];
            set_map_cell(w.cell, 0x50);
        }
        if (mechs_[0][0] == 'L') {
            open_message_box();
            print_id("mission.failed");  // 24d3:17ea, then a key
            if (!read_key(keys)) return false;
        }
        if (!step_party({0, 0}, keys)) return false;
        break;
    }
    case 2: {
        place_mission(1, 0);
        // The target can't shoot back: 'Mech record 4 loses its weapons and
        // ammunition (bytes 0x24, 0x25 and 0x27-0x32; 0x26 stays).
        auto& target = mechs_[4];
        target[0x24] = target[0x25] = 0;
        for (int b = 0x27; b <= 0x32; ++b) target[std::size_t(b)] = 0;
        if (!fight(1, keys)) return false;
        party_to_jason();
        w.done = true;
        break;
    }
    case 3:
        place_mission(0, (random_.next() & 3) + 3);
        if (!fight(1, keys)) return false;
        party_to_jason();
        w.done = true;
        break;
    case 4:
    case 5:
    case 6:
    case 7:
        // 1-4 enemy 'Mechs. Mission 4 alone (17dc:0765; 5-7 enter at 0770)
        // has no computer to fight for the party (F:0090). Mission 6 is the
        // raid one time in two, mission 7 always.
        no_computer_order_ = mode == 4;
        if (mode == 6 && (random_.next() & 1)) raid_ = true;
        if (mode == 7) raid_ = true;
        // The raid: the map store's last 16 cells (block 63's last two
        // rows) become tile 0x40.
        if (raid_)
            for (std::size_t c = 0xff0; c < 0x1000 && c < map_.blocks.size(); ++c) set_map_cell(c, 0x40);
        place_mission(mode - 3, 0);
        if (!fight(1, keys)) return false;
        party_to_jason();
        w.done = true;
        no_computer_order_ = false;
        break;
    case 8:
        // The Arena (17dc:07f7): one enemy 'Mech by 0d49's bit 0x80 (and
        // the spectator), fight kind 2; the party is not walked back to
        // Jason (17dc:110b is skipped).
        place_mission(0x81, 0);
        if (!fight(2, keys)) return false;
        w.done = true;
        break;
    case 9:
        // The jail break (17dc:05d4): F:398e = 1 has the figure drawing add
        // the four bays (docs/combat.md 1.7); the window built at the party
        // (287f:1314) and everything drawn.
        bays_drawn_ = true;
        window_at_ = position_;
        terrain_.map_changed();
        mission_draw();
        break;
    default:
        log_.push_back("mission " + std::to_string(mode) + ": not rebuilt");
        fight_unfinished_ = true;
        return false;
    }
    if (!mission_walk(w, keys)) return false;
    mission_results(w);
    return true;
}

bool Town::mission_walk(MissionWalk& w, KeySource& keys) {
    // The raid (F:3772) and the Arena go straight to the results.
    bool end = raid_ || w.mode == 8;
    int countdown = 0;  // [bp-1e]
    while (!end && !w.end) {
        if (at_the_door(position_)) {
            if (w.done) {
                end = true;
                continue;
            }
            if (!w.warned) {
                // 24d3:17c6, then 2656:03f5 (no pause of its own), 273d:086a
                // and a key.
                open_message_box();
                print_id("mission.not_done");
                if (!read_key(keys)) return false;
            }
            w.warned = true;
        }
        // 273d:002f: a key waiting?
        const auto waiting = keys.waiting_key();
        if (!waiting) {
            place_open_ = true;
            return false;
        }
        if (*waiting != KeySource::kNoKey) {
            // 2656:0d1d, 1000:2a2b (the rest of the keyboard is thrown away),
            // 1000:231d, 1000:218f.
            const int k = arrow_key(*waiting);
            animation_.key(k, mech_records());
            const auto step = step_for_key(k);
            if (!step_party(step.value_or(Step{}), keys)) return false;
        } else if (--countdown >= 0) {
            continue;  // 273d:0006(1): a frame passes
        }
        countdown = 10;
        mission_tick();
        if (!mission_step_done(w, keys)) return false;
    }
    return true;
}

// The walk's tick: the tiles' next frame (1000:240b), every fourth tick the
// townspeople move (1000:24c2, counted in DG:5802), and the map and the
// figures are drawn (287f:18ef, 1000:051b, 273d:06c3).
void Town::mission_tick() {
    if (tiles_animate(map_number_)) phase_ = (phase_ + 1) % 3;
    if (++mission_ticks_ == 1) move_townspeople();
    mission_ticks_ &= 3;
    mission_draw();
}

// 287f:18ef, 1000:051b, 273d:06c3: the map and the figures drawn.
void Town::mission_draw() {
    const auto figures = walk_in_figures();
    record_party_places();
    text_.forget(13, 0, kTextColumns - 13, kTextRows);
    frozen_ = Frozen{live_view(), figures};
}

// BTECH.EXE 1000:2da8 (Ghidra FUN_1000_2da8), then 287f:1da8
// Map `number` into `grid`: its blocks numbered from 0x90 in the one block
// store, so every grid's map entries now show this map; its tiles (map 11
// has its own sheet), its blocking tile, its entrances and its townspeople,
// placed again with eight random numbers (none out on maps above 10), unless
// DG:014c is set.
bool Town::load_town_map(int number, int grid) {
    const std::string name = "MAP" + std::to_string(number) + ".MTP";
    const auto bytes = source_ ? source_->map(number) : std::nullopt;
    if (!bytes) {
        log_.push_back(name + ": not found");
        return false;
    }
    map_ = formats::decode_mtp(*bytes);
    map_number_ = number;
    tile_sheet_ = tile_sheet_for_map(number);
    blocking_ = blocking_tile(number);
    flags_[flag::kTileEvents] = number == 14 ? 1 : 0;
    terrain_.hold_window();
    terrain_.place_map(map_, grid);
    terrain_.link_blocks();
    // Map 2 loaded while flag 37's count runs: the Arena's wall broken again.
    if (number == 2 && flags_[0x37] != 0) arena_wall(false);  // 1d43:0c72(0)
    if (people_skipped_) {  // DG:014c (the overhead map)
        log_.push_back("map " + std::to_string(number) + " loaded into grid " + std::to_string(grid) + ", no townspeople");
        return true;
    }
    if (!townspeople_away_) {
        // The headings are the fight figures' (F:3920 + 16 + i); placing
        // the walkers leaves them as the last fight set them up.
        auto placed = Townspeople::place(map_, number, random_, tables_);
        for (std::size_t i = 0; i < 8; ++i) placed.walkers()[i].heading = people_.walkers()[i].heading;
        placed.keep_wander_from(people_);
        people_ = placed;
    }
    else people_.place_during_fight(map_, number, random_);
    {
        for (int i = 0; i < 8; ++i) {
            const auto& w = people_.walkers()[std::size_t(i)];
            const auto at = number > 10 ? formats::MapPosition{0, 0} : w.target;
            figs_.x[std::size_t(16 + i)] = at.x;
            figs_.y[std::size_t(16 + i)] = at.y;
            animation_.set_direction(16 + i, -1);
            animation_.set_frame(16 + i, 0x10);
        }
    }
    log_.push_back("map " + std::to_string(number) + " loaded into grid " + std::to_string(grid));
    return true;
}

// BTECH.EXE 24d3:13ec, special 0x28 (40), and 19b8:152f (Ghidra
// FUN_19b8_152f)
// The jail break: Jason alone on foot at 0d10,7024 (the others' first
// record bytes kept aside, the held 'Mechs' letters in F:d452), mode 9,
// then the party at 0d00,7014, everyone back and no 'Mech held. If Jason is up he has the STINGER,
// 'Mech template 2 (27e8:03ea), in the first empty 'Mech record; if he
// fell, flag 28 = 1 (docs/combat.md 1.7).
bool Town::jail_break(KeySource& keys) {
    std::array<std::uint8_t, 8> kept{};  // F:3fe9 + p
    for (int p = 1; p < 8; ++p) {
        kept[std::size_t(p)] = characters_[std::size_t(p)][character::kName];
        characters_[std::size_t(p)][character::kName] = character::kEmpty;
    }
    for (int m = 0; m < 4; ++m) {
        held_mechs_[std::size_t(m)] = mechs_[std::size_t(m)][0];
        mechs_[std::size_t(m)][0] = 0xff;
    }
    position_ = {0x0d10, 0x7024};
    characters_[0][character::kMech] = character::kOnFoot;
    if (!run_mission(9, keys)) return false;
    position_ = {0x0d00, 0x7014};
    for (int p = 1; p < 8; ++p) characters_[std::size_t(p)][character::kName] = kept[std::size_t(p)];
    for (int m = 0; m < 4; ++m) {
        mechs_[std::size_t(m)][0] = held_mechs_[std::size_t(m)];
        held_mechs_[std::size_t(m)] = 0xff;
    }
    if (combat_.fight_on) {
        for (int m = 0; m < 4; ++m) {
            if (mechs_[std::size_t(m)][0] != 0xff) continue;
            mechs_[std::size_t(m)] = rules_->mechs[2].record;
            break;
        }
    } else {
        flags_[0x28] = 1;
    }
    return true;
}

// BTECH.EXE 17dc:134b (Ghidra FUN_17dc_134b)
// After a fight the party stands where Jason's figure is: his 'Mech's
// (figure 0, two half-cells down, into the next area if that crosses its
// edge) or his own (figure 4) when he is on foot. The window is built
// there and the map drawn.
void Town::party_to_jason() {
    if (characters_[0][character::kMech] != character::kOnFoot) {
        std::uint16_t y = std::uint16_t(figs_.y[0] + 2);
        if (y & 0x80) y = std::uint16_t(y + 0x0f80);
        position_ = {figs_.x[0], y};
    } else {
        position_ = {figs_.x[4], figs_.y[4]};
    }
    window_at_ = position_;  // 287f:1314
    if (window_stale_) {
        terrain_.map_changed();
        window_stale_ = false;
    }
    char where[32];
    std::snprintf(where, sizeof where, "back at %04x,%04x", position_.x, position_.y);
    log_.push_back(where);
    mission_draw();
}

// What each mode checks after a tick.
bool Town::mission_step_done(MissionWalk& w, KeySource& keys) {
    switch (w.mode) {
    case 0:
        // The course counts ticks, two more for each in water (F:32ae of
        // figure 0, 8 or 16 by 1000:051b, divided by 8).
        ++w.count;
        if (combat_.terrain[0] != 0) w.count += combat_.terrain[0] >> 3;
        if (position_.x >= 0x0c78 && position_.y >= 0xc07c && !w.done) {
            w.done = true;
            open_message_box();
            print_id("mission.course_corner");  // 24d3:17ea: a pause, then a key
            if (!read_key(keys)) return false;
        }
        return true;
    case 1: {
        // Standing on the cell just below the spot, or below and to the
        // left: in cells of the area, (x & 0x7f) / 2 and (y & 0x7f) / 2.
        const int hx = (position_.x & 0x7f) >> 1, hy = (position_.y & 0x7f) >> 1;
        const int sx = tables_.marked_spot_x[std::size_t(w.spot)];
        const int sy = tables_.marked_spot_y[std::size_t(w.spot)];
        if (hx < sx - 1 || hx > sx || hy != sy + 1 || w.done) return true;
        w.done = true;
        open_message_box();
        if (mechs_[0][0] == 'L') {
            print_id("mission.no_hands");
        } else {
            // The 'Mech picks it up: the spot is put back at once.
            print_id("mission.hands");
            if (w.saved_cell) set_map_cell(w.cell, *w.saved_cell, false);
        }
        return read_key(keys).has_value();  // 273d:086a, 273d:0259
    }
    case 9:
        return jail_bays(w, keys);
    default:
        return true;
    }
}

// 17dc:0868..09af, mode 9's part of the walk (docs/combat.md 1.7): a fight
// of kind 3 every 81 ticks, and the bays on row 702d. The third bay tried
// holds the 'Mech that starts, whichever it is.
bool Town::jail_bays(MissionWalk& w, KeySource& keys) {
    if (w.count++ >= 80) {
        place_mission(0, 0x88);
        if (!fight(3, keys)) return false;
        w.count = 0;
        if (!combat_.fight_on) {
            w.end = true;
            w.done = true;
        }
    }
    if (!combat_.fight_on) return true;
    if (position_.y != 0x702d) return true;
    for (int i = 0; i < 4; ++i) {
        if (position_.x != 0x0d14 + 4 * i || w.bays[std::size_t(i)]) continue;
        w.bays[std::size_t(i)] = true;
        text_.select_window(6);
        clear_window();           // 2656:0388
        text_.frame(0);           // 2656:0004(0)
        print_id("mission.board");
        print_id("mission.board_2");
        if (!read_key(keys)) return false;  // 273d:086a, 273d:0259
        print_id("mission.switch");
        if (w.found++ != 2) {
            if (!failed_start()) return false;
            text_.select_window(6);
            print_id("mission.wont_start");  // 24d3:17ea, with its pause
        } else {
            show_picture(0, 0);   // 1000:48b7(0, 0)
            text_.select_window(6);
            print_id("mission.starts");
            w.done = true;
            w.end = true;
        }
        if (!read_key(keys)) return false;  // 273d:0259
    }
    return true;
}

// BTECH.EXE 19b8:16b2 (Ghidra FUN_19b8_16b2)
// The 'Mech that won't start: O0.ANM's frames while the frame list's entry
// is below 0x49, sound 14, a wait of 60 and
// the picture box cleared. 1000:28cc asks for the disk; there are no disks.
bool Town::failed_start() {
    int last = 0;
    if (source_)
        if (const auto picture = source_->place_picture(0)) {
            const auto& anm = *picture;
            int ticks = 0;
            for (std::size_t k = 0; k < anm.sequence.size() && k < anm.delays.size(); ++k) {
                if (k > 0 && anm.sequence[k] >= 0x49) break;
                last = int(k);
                ticks += anm.delays[k];
            }
            picture_ = PlacePicture{0, last};
            hold_screen(ticks);
        }
    play_sound(14);   // 1000:19bf
    hold_screen(60);  // 273d:0006(0x3c)
    text_.select_window(4);
    clear_window();
    picture_.reset();
    return true;
}

// 17dc:0bf2: flag 01 says whether the mission was passed.
void Town::mission_results(const MissionWalk& w) {
    flags_[flag::kPassed] = 0;
    switch (w.mode) {
    case 0: {
        // Below 215, or 265 in a Locust (record 0's name starts with L).
        int count = w.count;
        if (mechs_[0][0] == 'L') count -= 50;
        if (w.done && count < 215) flags_[flag::kPassed] = 1;
        if (w.saved_cell) set_map_cell(w.cell, *w.saved_cell, false);
        break;
    }
    case 1: {
        // Passed unless in a Locust; Piloting goes up to 3 either way.
        if (w.saved_cell) set_map_cell(w.cell, *w.saved_cell, false);
        if (mechs_[0][0] != 'L') flags_[flag::kPassed] = 1;
        auto& piloting = characters_[0][std::size_t(character::kSkills + 4)];
        if (piloting < 3) ++piloting;
        break;
    }
    default:
        if (w.mode > 7) break;
        // Modes 2-7: passed when Jason's 'Mech is still there and nobody
        // fled (F:3992).
        if (mechs_[0][0] != 0xff && !combat_.person_ran) flags_[flag::kPassed] = 1;
        if (w.mode > 4) {
            // Gunnery, then Piloting: + r & 1 each, the draw only below 4.
            for (int s : {3, 4}) {
                auto& skill = characters_[0][std::size_t(character::kSkills + s)];
                if (skill < 4) skill = std::uint8_t(skill + (random_.next() & 1));
            }
        }
        if (raid_) {
            // Outdoors now (flag 04); flag 05 if the 'Mech came through.
            flags_[flag::kOutdoors] = 1;
            if (mechs_[0][0] != 0xff) flags_[flag::kRaidMech] = 1;
        }
        raid_ = false;
        break;
    }
    // The town's own drawing takes over again, unless the leader fell: the
    // main loop then skips its drawing (1000:0467) for the game over, and
    // the fight's last page stays.
    if (combat_.fight_on) frozen_.reset();
    bays_drawn_ = false;  // F:398e = 0
    // F:398e = 0. F:4594 has the place's script deciphered again
    // (17dc:1d30); the rebuild keeps the script apart from the fight.
    log_.push_back("mission " + std::to_string(w.mode) + ": " + (flags_[flag::kPassed] ? "passed" : "not passed") +
                   " (count " + std::to_string(w.count) + ")");
}

}  // namespace chr::game
