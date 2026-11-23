// The land around the party: the nine areas of the world nearest it, each
// a grid of 8x8 blocks (docs/wilderness.md). A block is either one of the
// loaded map's own or a terrain block, a terrain type the program makes up
// from the world's heights. No pixels here: the window hands out tile
// numbers.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "formats/mtp.h"
#include "game/fixes.h"

namespace chr::formats {
struct Tables;
}

namespace chr::game {

class Terrain {
public:
    static constexpr int kGrids = 9;         // 3x3 areas, the party's in the middle (grid 4)
    static constexpr int kGridBlocks = 64;   // 8x8 blocks an area
    static constexpr int kFirstMapBlock = 0x90;  // a grid entry naming the map's block 0
    static constexpr std::size_t kMapStoreBytes = 0x1000;  // P:101d: 64 blocks of a map

    Terrain(const formats::Tables& tables, const Fixes& fixes);

    // BTECH.EXE 287f:104e (Ghidra FUN_287f_104e)
    // The nine grids around `area` (0xYX), each made from the heights of
    // its area's four corners, then their neighbour masks (287f:1886).
    void generate(int area);

    // BTECH.EXE 287f:158c / 163b / 16e3 / 17c5 (Ghidra FUN_287f_158c etc.),
    // the grids' part
    // The party has stepped into the next area, `area` (0xYX), by one on
    // one axis (dy -1 up, 1 down; dx -1 left, 1 right): the grids move over
    // by a row or a column, the three new ones are made for the areas
    // around `area` (287f:18d8, 0bfb), and the masks rebuilt (287f:1886).
    // The three new grids are those the original queues for a map load
    // (P:09f3, docs/wilderness.md).
    std::array<int, 3> step_area(int dx, int dy, int area);

    // BTECH.EXE 1000:2da8 (Ghidra FUN_1000_2da8), the part that places the
    // map: its blocks go into grid `grid` at the block column and row the
    // map's header gives, numbered from kFirstMapBlock. The rest of the
    // grid keeps its terrain.
    void place_map(const formats::MtpMap& map, int grid);

    // BTECH.EXE 287f:1da8 (Ghidra FUN_287f_1da8), 287f:1886 and 287f:11bb
    // Each block's neighbour mask: which of its four neighbours have the
    // same terrain type. A map block's mask is its number in the block store.
    void link_blocks();

    // The cell window (P:07ad) and what it holds (P:07a4).
    static constexpr int kWindowCells = 24;
    struct Window {
        std::array<std::uint8_t, kWindowCells * kWindowCells> cells{};  // tile numbers, row by row
        std::array<std::uint8_t, 9> blocks{};  // each block's mask OR grid entry, row by row
    };
    // BTECH.EXE 287f:1314 (Ghidra FUN_287f_1314)
    // The 3x3 blocks around the block holding `position`, from the grids:
    // map blocks from `map`, terrain blocks from the templates.
    Window window(formats::MapPosition position, const formats::MtpMap& map) const;
    // The window around `position` with the map last placed, built once
    // for each block the position is in.
    const Window& window(formats::MapPosition position) const;
    // The placed map's cells have changed: the window is built again.
    void map_changed() { built_.reset(); }
    // A map load (1000:2da8) leaves the cell window as it was until 287f:1314
    // next builds one: the window as last built is held for what reads the
    // original's memory (the rig), until release_window().
    void hold_window() {
        if (!built_) return;
        held_ = built_;
        held_column_ = built_column_;
        held_row_ = built_row_;
        held_area_ = built_area_;
    }
    // BTECH.EXE 287f:1ece (Ghidra FUN_287f_1ece)
    // The fight saves the window's cells after a round's movement
    // (22e8:126a, around the party) and each redraw during a shot
    // (1e31:1f09) copies them back, leaving the window's place and block
    // values as they were. Its callers build the window first (287f:1314),
    // from the map store as it is now.
    void save_cells(formats::MapPosition position) const {
        release_window();
        built_.reset();
        saved_ = window(position).cells;
    }
    void restore_cells() const {
        if (saved_) restored_ = saved_;
    }
    // The round's map drawing (287f:1314) builds the window again.
    void release_window() const {
        held_.reset();
        restored_.reset();
    }
    // The window the original's memory holds for `position`'s block.
    Window held_window(formats::MapPosition position) const {
        const int column = (position.x & 0xff) >> 4, row = (position.y & 0xff) >> 4;
        if (held_ && column == held_column_ && row == held_row_ && area_of(position) == held_area_) return *held_;
        if (restored_) {
            static const formats::MtpMap kNoMap{};
            Window w = window(position, map_ ? *map_ : kNoMap);
            w.cells = *restored_;
            return w;
        }
        return window(position);
    }
    // Cell `index` of that window. Before it, as in the original's memory,
    // the nine block values (P:07a4), the grids (P:0564) and the masks
    // (P:0324); after it the corner words (P:09ed..09f2, as 287f:1df8 sets
    // them for `position`), the map-load queue (P:09f3), the noise cursor
    // (P:09f9), the noise table, the heights and the block store up to the
    // map's last block (past a full map, the program's blocks 0x50-0x51).
    // Nothing before or past those (memory the rebuild does not hold).
    std::optional<int> window_cell(formats::MapPosition position, int index) const;
    // P:09f3..09f8: the grids the last area step queued for a map load (ff:
    // none, or done) and their areas. 0 at a new game, so the first step
    // looks at area 0, which has no map.
    std::array<std::uint8_t, 3> queued_grids{};
    std::array<std::uint8_t, 3> queued_areas{};
    // P:09ef and P:09f1 are 0 until the first 287f:1df8 (the map drawer,
    // 287f:18ef, sets only P:09ed).
    bool corner_words_set = false;
    // The cell (x, y) of the position's area (either may be outside 0..63:
    // a neighbouring area's) if the window around `position` holds it.
    std::optional<int> cell(formats::MapPosition position, int x, int y) const;

    // Every entry of the nine grids set to `entry` (1b5d:0004, 1b5d:055a, 1b5d:079c).
    void fill_grids(std::uint8_t entry) {
        grids_.fill(entry);
        built_.reset();
    }
    // P:0564: the grids, 64 entries each, row by row.
    const std::array<std::uint8_t, kGrids * kGridBlocks>& grids() const { return grids_; }
    void set_grid_entry(std::size_t index, std::uint8_t entry) {
        grids_[index] = entry;
        built_.reset();
    }
    // P:0324: the masks, in the same order.
    const std::array<std::uint8_t, kGrids * kGridBlocks>& masks() const { return masks_; }

    // BTECH.EXE 1000:50c8 (Ghidra FUN_1000_50c8), its first part
    // P:09fb: the 256 random numbers start-up draws first, from the
    // program image's random state. The generator takes its noise from them.
    static std::array<std::uint8_t, 256> start_up_noise();

private:
    // The height the generator reads at `index` from the world table's
    // start (docs/wilderness.md: it reads past the table at the world's edges).
    int height(int index) const;
    // BTECH.EXE 287f:13d9 (Ghidra FUN_287f_13d9)
    // Block (column, row) of grid 4, -1..8 (one past its edge is the next
    // grid's), into `out` (stride kWindowCells); returns its mask OR entry.
    std::uint8_t copy_block(int column, int row, const formats::MtpMap& map, std::uint8_t* out) const;
    // BTECH.EXE 287f:0bfb (Ghidra FUN_287f_0bfb)
    // One area's 8x8 terrain types from its four corner heights.
    std::array<std::uint8_t, kGridBlocks> area_types(int nw, int ne, int sw, int se) const;
    // Grid k of the nine around `area` from its corner heights (no wrapping
    // at the world's edges).
    void make_grid(int k, int area);

    const formats::Tables& tables_;
    const Fixes& fixes_;
    std::array<std::uint8_t, 256> noise_{};
    std::array<std::uint8_t, kGrids * kGridBlocks> grids_{};
    std::array<std::uint8_t, kGrids * kGridBlocks> masks_{};
    const formats::MtpMap* map_ = nullptr;
    // P:09f9: where the generator's noise index was left (287f:0bfb).
    mutable std::uint16_t noise_cursor_ = 0;
    mutable std::optional<Window> built_;
    mutable int built_column_ = -1, built_row_ = -1, built_area_ = -1;
    mutable std::optional<Window> held_;
    int held_column_ = -1, held_row_ = -1, held_area_ = -1;
    // 0xYX: the area a position is in.
    static int area_of(formats::MapPosition p) { return ((p.y >> 8) & 0xf0) | ((p.x >> 8) & 0x0f); }
    mutable std::optional<std::array<std::uint8_t, kWindowCells * kWindowCells>> saved_, restored_;
};

}  // namespace chr::game
