// The Star League cache, map 14: going in and its doors (docs/cache.md).

#include <algorithm>

#include "game/town.h"

namespace chr::game {

namespace {

constexpr int kCacheMap = 14;
constexpr std::uint8_t kCacheGrid = 0xd0;  // every grid entry while in the cache
constexpr int kWayInDoor = 11;             // the door that needs no items
constexpr int kDoors = 12;
constexpr std::uint8_t kLowerGrid = 0xd1;  // every grid entry while on the second level
constexpr std::size_t kLowerBytes = 0x300; // the second level's twelve blocks

}  // namespace

// BTECH.EXE 1b5d:0004 (Ghidra FUN_1b5d_0004)
// Into the cache: the 'Mechs parked outside, everyone on foot, map 14 alone
// in the middle grid with the party at its way in, the way-in door opening,
// then INSTRUCT, and the doors already open shown open again.
bool Town::enter_cache(KeySource& keys) {
    for (std::size_t m = 0; m < 4; ++m) {
        parked_mechs_[m] = mechs_[m][0];
        mechs_[m][0] = 0xff;
    }
    for (std::size_t i = 0; i < 8; ++i) characters_[i][character::kMech] = 0;
    position_ = {0x0c06, 0xc07e};
    // 287f:00d1 (CGA colours) and 1000:28cc (the disk) have nothing to do here.
    terrain_.fill_grids(kCacheGrid);
    load_town_map(kCacheMap, 4);
    terrain_.queued_grids.fill(0xff);
    // 273d:063b, 049d and 287f:0260 load the tile set (STARLEAG.ICN, with
    // the map); F:3988 = 2 stops the tile animation (tiles_animate).
    terrain_.link_blocks();  // 287f:1da8
    build_window();          // 287f:1314
    mission_draw();          // 287f:18ef, 273d:06c3
    tick_redraws_ = true;
    hold_screen(60);         // 273d:0006(0x3c)
    if (!open_door(0x0c03, 0x097d, -1, keys)) return false;
    for (std::size_t i = 0; i < 8; ++i) characters_[i][character::kMech] = character::kOnFoot;
    if (!visit_place(0x17, keys)) return false;
    for (int d = 0; d < kDoors - 1; ++d)
        if (flags_[std::size_t(flag::kCacheDoors + d)] != 0 && !open_door(0, 0, d, keys)) return false;
    return true;
}

// BTECH.EXE 1b5d:0ab6 (Ghidra FUN_1b5d_0ab6)
// Door `door`, or with -1 the door at half-cells (x, y) that a step reached:
// unless it is the way in, the three items carried must be the door's own,
// or it says which are wrong and the step ends. An opened door marks its
// items used (the way in empties the slots instead), sets its flag, and its
// four tiles go through three frames into the map (only the last without a
// step, and then without the sound and waits).
bool Town::open_door(int x, int y, int door, KeySource& keys) {
    int found = door;
    std::array<int, 3> needs{};
    if (door < 0) {
        x = (x + 1) & 0x7f;
        y &= 0x7e;
        found = -1;
        for (int d = 0; d < kDoors; ++d)
            if (tables_.door_y[std::size_t(d)] == y && tables_.door_x[std::size_t(d)] == (x & 0x7e)) {
                found = d;
                break;
            }
        if (found < 0) return true;
        for (std::size_t s = 0; s < 3; ++s) needs[s] = tables_.door_items[s][std::size_t(found)] - 1;
        if (found != kWayInDoor) {
            bool wrong = false;
            for (std::size_t s = 0; s < 3; ++s) wrong = wrong || flags_[std::size_t(flag::kCacheItems) + s] != std::uint8_t(needs[s]);
            if (wrong) {
                open_message_box();  // 24d3:17c6
                for (std::size_t s = 0; s < 3; ++s)
                    if (flags_[std::size_t(flag::kCacheItems) + s] != std::uint8_t(needs[s]))
                        print_id("cache.door_wants." + std::to_string(s));  // 2656:03f5
                // 273d:086a, then a key (273d:0259); the step ends (F:d55c).
                turn_message_ = true;
                if (!read_key(keys)) return false;
                return true;
            }
        }
    } else {
        x = tables_.door_x[std::size_t(door)];
        y = tables_.door_y[std::size_t(door)];
        for (std::size_t s = 0; s < 3; ++s) needs[s] = tables_.door_items[s][std::size_t(door)] - 1;
    }
    if (found == kWayInDoor) {
        for (std::size_t s = 0; s < 3; ++s) flags_[std::size_t(flag::kCacheItems) + s] = 0xff;
    } else {
        for (const int item : needs) item_used_[std::size_t(item)] = 1;
    }
    flags_[std::size_t(flag::kCacheDoors + found)] = 1;
    if (door < 0) play_sound(10);  // 1000:19bf
    const int column = (x >> 1) & 7;
    const std::size_t corner = std::size_t(((y & 0x70) << 5) + ((y & 0x0e) << 2) + ((x & 0x70) << 2) + column);
    for (int frame = door < 0 ? 0 : 2; frame < 3; ++frame) {
        // Four across; past the block's eighth column, on into the next block.
        std::size_t at = corner;
        for (int i = 0; i < 4; ++i) {
            set_map_cell(at, tables_.door_tiles[std::size_t(frame * 4 + i)]);
            at += column + i + 1 == 8 ? 0x39 : 1;
        }
        build_window();  // 287f:1314
        mission_draw();  // 287f:18ef, 1000:051b, 273d:06c3
        tick_redraws_ = true;
        if (door < 0) hold_screen(20);  // 273d:0006(0x14)
    }
    return true;
}

// BTECH.EXE 1b5d:03aa (Ghidra FUN_1b5d_03aa)
// The items whose three half-cells hold (x, y): each in turn, unless a door
// has used it, offers itself for its slot, and yes puts it there in place
// of what the slot held. A used item says so and waits for a key.
bool Town::take_item(int x, int y, KeySource& keys) {
    x = (x + 1) & 0x7f;
    y &= 0x7f;
    for (std::size_t i = 0; i < item_used_.size(); ++i) {
        const int left = tables_.item_x[i];
        if (tables_.item_y[i] != y || x < left || x >= left + 3) continue;
        open_message_box();  // 24d3:17c6
        if (item_used_[i] == 0) {
            const int slot = tables_.item_slot[i];
            print_id("cache.item_take");                             // 2656:03f5
            print_id("cache.item_kind." + std::to_string(slot));     // through DG:2234
            print_id("cache.item_number");
            text_.print(std::to_string(i + 1));                      // 273d:0053
            print_id("cache.item_take_end");
            const auto yes = yes_no(true, keys);                     // 1000:1a13(1)
            if (!yes) {
                place_open_ = true;
                return false;
            }
            if (*yes) flags_[std::size_t(flag::kCacheItems + slot)] = std::uint8_t(i);
        } else {
            print_id("cache.item_used");  // 24d3:17ea
            if (!read_key(keys)) return false;  // 273d:0259
        }
        turn_message_ = true;  // F:d55c
    }
    return true;
}

// BTECH.EXE 1b5d:01e9 (Ghidra FUN_1b5d_01e9)
// The wall at cells x 0-2, y 42-47 becomes the six rows of three tiles in
// the table, the window is redrawn, and the text says so.
bool Town::open_cache_wall(KeySource& keys) {
    for (std::size_t row = 0; row < 6; ++row)
        for (std::size_t col = 0; col < 3; ++col)
            set_map_cell(0xa10 + row * 8 + col, tables_.cache_wall[row * 3 + col]);
    build_window();  // 287f:1314
    mission_draw();  // 287f:18ef, 1000:051b, 273d:06c3
    tick_redraws_ = true;
    return cache_message("cache.wall_opened", keys);
}

// BTECH.EXE 1b5d:02d2 (Ghidra FUN_1b5d_02d2)
// Only at its own place (x 4e, y 0c-10 in half-cells): the text, and from
// then on the way out answers.
bool Town::cache_hint(int x, int y, KeySource& keys) {
    x = (x + 1) & 0x7e;
    y &= 0x7e;
    if (x != 0x4e || y < 0x0c || y > 0x11) return true;
    if (!cache_message("cache.hint", keys)) return false;
    flags_[flag::kCacheHint] = 1;
    return true;
}

// The message box, the text (24d3:17ea) and a key (273d:0259); the step
// shows no direction (F:d55c).
bool Town::cache_message(const std::string& id, KeySource& keys) {
    open_message_box();  // 24d3:17c6
    print_id(id);
    turn_message_ = true;
    return read_key(keys).has_value();
}

// BTECH.EXE 1b5d:055a (Ghidra FUN_1b5d_055a)
// Tile 28 at two places: FINDIT's (x 7c, y 04 in half-cells), and the way
// down (x 48, y 38), which once the puzzle is solved only says so, and
// otherwise takes the party to the second level.
bool Town::cache_stairs(int x, int y, KeySource& keys) {
    x = (x + 1) & 0x7e;
    y &= 0x7e;
    if (x == 0x7c && y == 0x04) {
        if (!visit_place(0x18, keys)) return false;  // FINDIT
        flags_[flag::kCacheFindit] = 1;
        turn_message_ = true;
        return true;
    }
    if (x != 0x48 || y != 0x38) return true;
    open_message_box();  // 24d3:17c6
    turn_message_ = true;
    if (flags_[flag::kCachePuzzle] == 1) {
        print_id("cache.puzzle_done");  // 24d3:17ea
        return read_key(keys).has_value();
    }
    print_id("cache.way_down");
    if (!read_key(keys)) return false;
    clear_window();  // 2656:0388
    print_id("cache.way_down_2");
    if (!read_key(keys)) return false;
    steps_per_key_ = 1;  // DG:015a (docs/town_loop.md, "Steps a key")
    position_ = {0x0c04, 0xc022};
    // 287f:00d1 (CGA colours) and 1000:28cc (the disk) have nothing to do here.
    terrain_.fill_grids(kLowerGrid);
    tile_sheet_ = "MAP.ICN";  // DG:2355, 273d:0814 / 287f:0260; F:3988 = 3
    std::copy_n(map_.blocks.begin(), kLowerBytes, upper_saved_.begin());  // to F:4614
    if (!load_lower_level()) return false;  // DG:235d into P:101d
    flags_[flag::kCacheLevel] = 1;
    // Grid 4's first three rows of four name the level's twelve blocks.
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 4; ++col)
            terrain_.set_grid_entry(4 * Terrain::kGridBlocks + row * 8 + col, std::uint8_t(0x90 + row * 4 + col));
    terrain_.link_blocks();  // 287f:1da8
    build_window();          // 287f:1314
    mission_draw();          // 287f:18ef, 273d:06c3
    tick_redraws_ = true;
    blocking_ = 0x8b;        // DG:0150
    return true;
}

// BTECH.EXE 1b5d:0288 (Ghidra FUN_1b5d_0288)
// The tiles above f5: ENDMECH, while F:d34b is clear.
bool Town::cache_endmech(KeySource& keys) {
    if (flags_[flag::kCacheEndmech] != 0) return true;
    return visit_place(0x16, keys);
}

// BTECH.EXE 1b5d:04ab (Ghidra FUN_1b5d_04ab)
// Tile 4d at the way out (x 04, y 04 or x 02, y 0e in half-cells): with
// the hint seen, the puzzle solved and FINDIT seen, WINSCENE and the game
// is won; otherwise a text saying which is missing, and a key.
bool Town::cache_way_out(int x, int y, KeySource& keys) {
    x = (x + 1) & 0x7e;
    y &= 0x7e;
    if (!((x == 0x04 && y == 0x04) || (x == 0x02 && y == 0x0e))) return true;
    const char* text = nullptr;
    if (flags_[flag::kCacheHint] == 0)
        text = "cache.out_closed";
    else if (flags_[flag::kCachePuzzle] != 1)
        text = "cache.out_puzzle";
    else if (flags_[flag::kCacheFindit] == 0)
        text = "cache.out_findit";
    if (!text) {
        if (!visit_place(0x19, keys)) return false;  // 1000:7dc8: WINSCENE
        turn_message_ = true;
        game_won_ = true;
        return true;
    }
    open_message_box();  // 24d3:17c6
    print_id(text);      // 24d3:17ea
    turn_message_ = true;
    return read_key(keys).has_value();
}

// BTECH.EXE 1b5d:079c (Ghidra FUN_1b5d_079c)
// Tile 94: back up to the first level at 0c45,c039, its blocks as they were.
bool Town::cache_way_up(KeySource& keys) {
    open_message_box();  // 24d3:17c6
    print_id("cache.way_up");
    if (!read_key(keys)) return false;
    position_ = {0x0c45, 0xc039};
    terrain_.fill_grids(kCacheGrid);
    for (std::size_t i = 0; i < kLowerBytes; ++i) set_map_cell(i, upper_saved_[i]);
    turn_message_ = true;
    tile_sheet_ = "STARLEAG.ICN";  // DG:23a5; F:3988 = 2
    for (int i = 0; i < Terrain::kGridBlocks; ++i)
        terrain_.set_grid_entry(4 * Terrain::kGridBlocks + i, std::uint8_t(0x90 + i));
    terrain_.link_blocks();
    build_window();
    mission_draw();
    tick_redraws_ = true;
    flags_[flag::kCacheLevel] = 0;
    blocking_ = 0x21;
    return true;
}

// BTECH.EXE 1b5d:0913 (Ghidra FUN_1b5d_0913)
// A puzzle cell turns into its pair (odd t to t + 1, even t to t - 1),
// in the level's store of four blocks a row; the window shows it when it
// is next built. Sound 15.
bool Town::cache_puzzle_cell(int x, int y) {
    x = (x + 1) & 0x7f;
    const std::size_t at = std::size_t(((y & 0x70) << 4) + ((y & 0x0e) << 2) + ((x & 0x70) << 2) + ((x >> 1) & 7));
    set_map_cell(at, ((map_.blocks[at] + 1) ^ 1) - 1, false);
    play_sound(15);  // 1000:19bf
    return true;
}

// BTECH.EXE 1b5d:0980 (Ghidra FUN_1b5d_0980)
// Solved when the seven cells in the table are all on (even) and no other
// cell of the level is: sound 16, the text, flag 3e. Otherwise sound 17,
// the text, and every cell turned off again. A key either way.
bool Town::cache_puzzle_check(KeySource& keys) {
    const auto on = [](int t) { return t >= 0x97 && t <= 0xf0 && (t & 1) == 0; };
    bool solved = true;
    for (const int at : tables_.puzzle_cells)
        if (map_.blocks[std::size_t(at)] & 1) solved = false;
    if (solved)
        for (std::size_t i = 0; i < kLowerBytes; ++i)
            if (on(map_.blocks[i]) && std::find(tables_.puzzle_cells.begin(), tables_.puzzle_cells.end(), int(i)) == tables_.puzzle_cells.end())
                solved = false;
    open_message_box();
    if (solved) {
        play_sound(16);  // 1000:19bf
        print_id("cache.puzzle_solved");
        flags_[flag::kCachePuzzle] = 1;
    } else {
        play_sound(17);  // 1000:19bf
        print_id("cache.puzzle_wrong");
        for (std::size_t i = 0; i < kLowerBytes; ++i)
            if (on(map_.blocks[i])) set_map_cell(i, map_.blocks[i] - 1, false);
    }
    turn_message_ = true;
    return read_key(keys).has_value();
}

// BTECH.EXE 287f:0260 and 1000:2da8's file read, for DG:235d
// MAP15.MTP: the second level's twelve blocks, 0x300 bytes with no header,
// over the first twelve of the cache's.
bool Town::load_lower_level() {
    const auto map = source_ ? source_->map(15) : std::nullopt;
    const auto bytes = map ? *map : std::vector<std::uint8_t>{};
    if (bytes.size() < kLowerBytes) {
        log_.push_back("MAP15.MTP: not found");
        return false;
    }
    std::copy_n(bytes.begin(), kLowerBytes, map_.blocks.begin());
    terrain_.map_changed();
    return true;
}

// BTECH.EXE 1000:1c12 (Ghidra FUN_1000_1c12), while F:d346 is set
// The tile `tile` that stopped a character's step, at half-cell `at`, does
// what it does on the cache's level; any other tile only blocks.
bool Town::cache_step(int tile, formats::MapPosition at, KeySource& keys) {
    const int x = std::int16_t(at.x), y = std::int16_t(at.y);
    if (flags_[flag::kCacheLevel] == 0) {
        // A door's three tiles, each naming the door from its own cell.
        if (tile >= 0x7e && tile <= 0x80) return open_door(x - 2 * (tile - 0x7e), y, -1, keys);
        if (tile == 0xf5 && at.x >= 0x0c01 && at.x <= 0x0c04 && at.y == 0xc054) return open_cache_wall(keys);
        if (tile == 0xb6 || tile == 0xb7) return take_item(x, y, keys);
        if (tile >= 0xf6) return cache_endmech(keys);
        if (tile == 0x83 || (tile >= 0xa5 && tile <= 0xa7)) return cache_message("cache.screen", keys);  // 1b5d:02a8
        if (tile == 0x4d) return cache_way_out(x, y, keys);
        if (tile == 0x3a || tile == 0x3d) return cache_hint(x, y, keys);
        if (tile == 0x28) return cache_stairs(x, y, keys);
        return true;
    }
    if (tile == 0x94) return cache_way_up(keys);
    if (tile >= 0x97 && tile <= 0xf0) return cache_puzzle_cell(x, y);
    if (tile == 0x8c || tile == 0x8d) return cache_puzzle_check(keys);
    return true;
}

}  // namespace chr::game
