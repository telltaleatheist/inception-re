#include "game/save.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "game/flags.h"
#include "game/town.h"

namespace chr::game {

namespace save {

std::string file_name(int slot) { return "Game" + std::string(1, char('1' + slot)); }

}  // namespace save

namespace {

void put16(std::uint8_t* p, int v) {
    p[0] = std::uint8_t(v);
    p[1] = std::uint8_t(v >> 8);
}
void put32(std::uint8_t* p, std::int32_t v) {
    put16(p, int(std::uint32_t(v) & 0xffff));
    put16(p + 2, int(std::uint32_t(v) >> 16));
}
std::uint16_t get16(const std::uint8_t* p) { return std::uint16_t(p[0] | p[1] << 8); }
std::int32_t get32(const std::uint8_t* p) { return std::int32_t(std::uint32_t(get16(p)) | std::uint32_t(get16(p + 2)) << 16); }

}  // namespace

// The block as the original holds it at F:c614: the shadow, with every part
// the rebuild holds (save::kHeld) written over it.
save::Block Town::save_block() const {
    using save::at;
    save::Block b = save_shadow_;
    for (std::size_t c = 0; c < characters_.size(); ++c)
        std::copy(characters_[c].begin(), characters_[c].end(), b.begin() + std::ptrdiff_t(at(0xc614) + 0x11 * c));
    for (std::size_t m = 0; m < mechs_.size(); ++m)
        std::copy(mechs_[m].begin(), mechs_[m].end(), b.begin() + std::ptrdiff_t(at(0xc724) + 0x7d * m));
    std::copy(seen_.begin(), seen_.end(), b.begin() + std::ptrdiff_t(at(0xcb0c)));
    std::copy(flags_.begin(), flags_.end(), b.begin() + std::ptrdiff_t(at(0xd30c)));
    put32(&b[at(0xd370)], money_.cbills);
    for (std::size_t i = 0; i < 3; ++i) put32(&b[at(0xd374) + 4 * i], money_.invested[i]);
    for (std::size_t i = 0; i < 8; ++i) {
        auto* r = &b[at(0xd390) + 0x1a * i];
        const auto& w = people_.walkers()[i];
        put16(r + 0, people_.kept()[i].x);
        put16(r + 2, people_.kept()[i].y);
        put16(r + 4, w.target.x);
        put16(r + 6, w.target.y);
        r[8] = w.route;
        r[9] = w.wait;
    }
    b[at(0xd450)] = kits_[0];
    b[at(0xd451)] = kits_[1];
    std::copy(held_mechs_.begin(), held_mechs_.end(), b.begin() + std::ptrdiff_t(at(0xd452)));
    b[at(0xd456)] = next_name_;
    for (std::size_t k = 0; k < std::size_t(MapMarks::kCount); ++k) {
        const auto& m = marks_.marks[k];
        b[at(0xd457) + k] = m.code;
        b[at(0xd497) + k] = m.area;
        b[at(0xd4d7) + k] = m.x;
        b[at(0xd517) + k] = m.y;
    }
    b[at(0xd557)] = std::uint8_t(marks_.next);
    return b;
}

// The other way: the shadow keeps the whole block, and each part the
// rebuild holds is read back out of it.
void Town::load_block(const save::Block& b) {
    using save::at;
    save_shadow_ = b;
    for (std::size_t c = 0; c < characters_.size(); ++c)
        std::copy_n(b.begin() + std::ptrdiff_t(at(0xc614) + 0x11 * c), 0x11, characters_[c].begin());
    for (std::size_t m = 0; m < mechs_.size(); ++m)
        std::copy_n(b.begin() + std::ptrdiff_t(at(0xc724) + 0x7d * m), 0x7d, mechs_[m].begin());
    std::copy_n(b.begin() + std::ptrdiff_t(at(0xcb0c)), seen_.size(), seen_.begin());
    std::copy_n(b.begin() + std::ptrdiff_t(at(0xd30c)), flags_.size(), flags_.begin());
    money_.cbills = get32(&b[at(0xd370)]);
    for (std::size_t i = 0; i < 3; ++i) money_.invested[i] = get32(&b[at(0xd374) + 4 * i]);
    for (std::size_t i = 0; i < 8; ++i) {
        const auto* r = &b[at(0xd390) + 0x1a * i];
        auto& w = people_.walkers()[i];
        people_.kept()[i] = {get16(r + 0), get16(r + 2)};
        w.target = {get16(r + 4), get16(r + 6)};
        w.route = r[8];
        w.wait = r[9];
    }
    kits_ = {b[at(0xd450)], b[at(0xd451)]};
    std::copy_n(b.begin() + std::ptrdiff_t(at(0xd452)), held_mechs_.size(), held_mechs_.begin());
    next_name_ = b[at(0xd456)];
    for (std::size_t k = 0; k < std::size_t(MapMarks::kCount); ++k) {
        auto& m = marks_.marks[k];
        m.code = b[at(0xd457) + k];
        m.area = b[at(0xd497) + k];
        m.x = b[at(0xd4d7) + k];
        m.y = b[at(0xd517) + k];
    }
    marks_.next = b[at(0xd557)];
}

// BTECH.EXE 1000:35d3 (Ghidra FUN_1000_35d3)
// The command menu's save: refused in the cache's second level; otherwise a
// slot from menu 0x28 and the file GameN written (docs/formats/save.md).
bool Town::save_game(KeySource& keys) {
    bool wait_key = false;
    if (flags_[flag::kCacheLevel] != 0) {
        open_message_box();  // 24d3:17c6, 17ea
        print_id("save.refused");
        wait_key = true;
    } else {
        text_.select_window(3);
        clear_window();
        print_id("save.which");
        const auto slot = menu(0x28, keys);
        if (!slot) {
            place_open_ = true;
            return false;
        }
        if (*slot != save::kSlots) {
            // 1000:28cc(3) and the INFOCOM.CMP check with its disk prompt
            // (1000:2913) ask for the game's disk: no disks in the rebuild.
            if (!write_save(*slot)) {
                clear_window();
                text_.set_fg(TextRole::Warning);  // DG:37fe
                print_id("save.failed");
                wait_key = true;
            }
        }
    }
    if (wait_key && !read_key(keys)) {  // 273d:0259
        place_open_ = true;
        return false;
    }
    redraw_map_screen();  // 1e31:032f(0)
    draw_panel(true);          // 1000:4cac(1); 1000:28cc(1) asks for no disk
    return true;
}

// BTECH.EXE 1000:32b3 (Ghidra FUN_1000_32b3)
// The command menu's load: a slot from menu 0x28, the file GameN read back
// over the block and the position, then the maps around the party loaded
// again and the 'Mech figures set up (docs/formats/save.md).
bool Town::load_game(KeySource& keys) {
    text_.select_window(3);
    clear_window();
    print_id("load.which");
    const auto slot = menu(0x28, keys);
    if (!slot) {
        place_open_ = true;
        return false;
    }
    if (*slot != save::kSlots) {
        // 1000:28cc(3) and the INFOCOM.CMP check with its disk prompt
        // (1000:2913) ask for the game's disk: no disks in the rebuild.
        // 287f:33d0 (open, 0x8000) and 287f:3580.
        const auto file = saves_ ? saves_->read(*slot) : std::nullopt;
        bool wait_key = false;
        if (!file) {
            clear_window();
            print_id("load.not_found");
            text_.set_fg(TextRole::FileName);  // DG:37fe
            text_.print(save::file_name(*slot));
            print_id("load.not_found_end");
            wait_key = true;
        } else if (file->empty() || (*file)[0] != save::kMark) {
            clear_window();
            print_id("load.not_a_save");
            wait_key = true;
        }
        if (wait_key && !read_key(keys)) {  // 273d:0259
            place_open_ = true;
            return false;
        }
        if (!file) {
            redraw_map_screen();  // 1e31:032f(0)
            draw_panel(true);     // 1000:4cac(1); 1000:28cc(1) asks for no disk
            return true;
        }
        // 1b5d:0004: a game saved in the cache goes into it again.
        if (!file->empty() && (*file)[0] == save::kMark && !read_saved_game(*file) && !enter_cache(keys)) return false;
        // 1000:34d3: the 'Mech figures 0-3, after a load or a file that is
        // not a saved game.
        for (int i = 0; i < 4; ++i) {
            figs_.sprite_base[std::size_t(i)] = mechs_[std::size_t(i)][0] == 'L' ? 0 : 0x92;
            animation_.set_frame(i, 0);       // F:409a
            figs_.heading[std::size_t(i)] = 0;  // F:3920
            animation_.set_direction(i, 0);   // F:396c
            animation_.start_script(i, FigureAnimation::mech_script(0));  // DG:01f6 + 4i = 27e8:0270
        }
        agent_found_ = false;  // F:374a
        // 1000:4621 (BTTLTECH.ICN back after the cache's tile set) is the
        // tile sheet of the map loaded last (load_town_map).
    }
    redraw_map_screen();  // 1e31:032f(0)
    draw_panel(true);     // 1000:4cac(1); 1000:28cc(1) asks for no disk
    return true;
}

// BTECH.EXE 1000:3383..34cc: a saved game read back (its mark checked).
// The reads (287f:3580) do not check their counts: whatever a short file
// leaves out stays as it was. False for a game saved in the cache (flag 3a),
// whose maps are the cache's own load (the caller's).
bool Town::read_saved_game(const std::vector<std::uint8_t>& file) {
    area_maps_[0xcc] = 1;  // 27e8:00fc
    area_maps_[0x34] = 0;  // 27e8:0064
    auto block = save_block();
    std::uint8_t place[4] = {std::uint8_t(position_.x), std::uint8_t(position_.x >> 8), std::uint8_t(position_.y),
                             std::uint8_t(position_.y >> 8)};
    for (std::size_t i = 1; i < file.size() && i < std::size_t(save::kFileSize); ++i) {
        if (i <= std::size_t(save::kBlockSize)) block[i - 1] = file[i];
        else place[i - 1 - std::size_t(save::kBlockSize)] = file[i];
    }
    load_block(block);
    position_ = {get16(place), get16(place + 2)};
    item_used_.fill(0);  // F:45de
    menu_last_[0x26] = std::int8_t(flags_[flag::kPictureChance]);  // menu 0x26's last item (+6)
    if (flags_[flag::kOutdoors] != 0) area_maps_[0xcc] = 11;
    if (flags_[flag::kDiskSeen] != 0) area_maps_[0x34] = 12;
    if (flags_[flag::kTileEvents] != 0) return false;
    // The nine maps around the position: its area (x | y, high byte, the
    // same as Terrain::area_of for any position), the terrain, and each
    // of the nine areas around it that has a map loaded into its grid.
    const int area = ((position_.x | position_.y) >> 8) & 0xff;
    terrain_.generate(area);  // 287f:104e
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            const int a = area - 0x11 + 0x10 * r + c;
            if (a < 0 || a >= 0x100 || area_maps_[std::size_t(a)] == 0) continue;
            load_town_map(area_maps_[std::size_t(a)], 3 * r + c);  // 1000:2da8
        }
    terrain_.link_blocks();  // 287f:1da8
    return true;
}

// 287f:33d0 (create, 0x8101 / 0x180) and four writes (287f:366a): the mark,
// the block, x and y. False if any of it fails.
bool Town::write_save(int slot) {
    std::vector<std::uint8_t> file(save::kFileSize);
    file[0] = save::kMark;
    const auto block = save_block();
    std::copy(block.begin(), block.end(), file.begin() + 1);
    put16(&file[1 + save::kBlockSize], position_.x);
    put16(&file[3 + save::kBlockSize], position_.y);
    if (!saves_ || !saves_->write(slot, file)) return false;
    log_.push_back("saved " + save::file_name(slot));
    return true;
}

// BTECH.EXE 1e31:032f (Ghidra FUN_1e31_032f)
// The map screen drawn again after a save or a load, or (`fight`) after the
// scan list's stats screen: the window built at the party, the map and the
// figures (the fight's, in a fight) drawn on the page and copied to the
// screen, then windows 4 and 3 each cleared and framed.
void Town::redraw_map_screen(bool fight) {
    build_window();  // 287f:1314
    if (fight) {
        page_ = Frozen{live_view(), fight_figures_drawn()};  // 287f:18ef, 1000:0e4b
        page_sprites_.clear();
        show_page();
    } else {
        record_party_places();  // 287f:18ef, 1000:051b
    }
    text_.forget(13, 0, kTextColumns - 13, kTextRows);  // 273d:06c3
    for (int w : {4, 3}) {
        text_.select_window(w);  // 2656:0281
        clear_window();          // 2656:0388
        text_.frame(w);          // 2656:0004
    }
}

}  // namespace chr::game
