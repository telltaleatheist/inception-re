#include "game/terrain.h"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

#include "formats/tables.h"
#include "game/random.h"

namespace chr::game {

namespace {

// The generator's lattice: 9x9 heights, an area's 8x8 blocks plus the
// next area's first row and column. kEmpty marks a point not yet made.
constexpr int kSide = 9;
constexpr std::uint8_t kEmpty = 0xff;
using Lattice = std::array<std::uint8_t, kSide * kSide>;

// What `cells` lattice cells of distance add at a midpoint: the noise byte
// masked to 0..4 cells - 1, less 2 cells; an 8-bit sum, and anything at
// 0x80 or more (a sum past the top or below zero) is 0.
std::uint8_t displaced(int average, std::uint8_t noise, int cells) {
    const std::uint8_t delta = std::uint8_t((noise & std::uint8_t(4 * cells - 1)) - 2 * cells);
    const std::uint8_t h = std::uint8_t(average + delta);
    return h >= 0x80 ? 0 : h;
}

// The noise table's next byte; the index is 8 bits and wraps.
struct Noise {
    const std::array<std::uint8_t, 256>& table;
    std::uint8_t index;
    std::uint8_t next() { return table[index++]; }
};

// BTECH.EXE 287f:0d07 (Ghidra FUN_287f_0d07), 287f:0d79 (FUN_287f_0d79)
// One edge, points a and b of the lattice `step` apart per cell: halved
// again and again, each new midpoint the average of its ends displaced by
// the noise. The halves wait on a stack, the second half first.
void subdivide_edge(Lattice& h, Noise& noise, int a, int b, int step) {
    std::vector<std::pair<int, int>> stack{{a, b}};
    while (!stack.empty()) {
        const auto [lo, hi] = stack.back();
        stack.pop_back();
        if (hi - lo == step) continue;
        const int mid = lo + (hi - lo) / 2;
        stack.push_back({lo, mid});
        stack.push_back({mid, hi});
        if (h[std::size_t(mid)] != kEmpty) continue;
        const int cells = (mid - lo) / step;
        h[std::size_t(mid)] = displaced((h[std::size_t(lo)] + h[std::size_t(hi)]) >> 1, noise.next(), cells);
    }
}

// BTECH.EXE 287f:0df8 (Ghidra FUN_287f_0df8)
// The inside: each square gets its centre (the average of its corners, no
// noise), then the midpoints of its top, left, bottom and right sides that
// are still empty (each its ends' average displaced), then its four
// quarters, the last quarter first.
void subdivide_inside(Lattice& h, Noise& noise, bool fix_noise) {
    struct Square { int nw, ne, sw, se; };
    std::vector<Square> stack{{0, kSide - 1, kSide * (kSide - 1), kSide * kSide - 1}};
    const auto side = [&](int at, int a, int b, int cells) {
        if (h[std::size_t(at)] != kEmpty) return;
        h[std::size_t(at)] = displaced((h[std::size_t(a)] + h[std::size_t(b)]) >> 1, noise.next(), cells);
    };
    while (!stack.empty()) {
        const Square s = stack.back();
        stack.pop_back();
        if (s.ne - s.nw == 1) continue;
        const int centre = s.nw + (s.se - s.nw) / 2;
        if (h[std::size_t(centre)] == kEmpty)
            h[std::size_t(centre)] = std::uint8_t(
                (h[std::size_t(s.nw)] + h[std::size_t(s.se)] + h[std::size_t(s.ne)] + h[std::size_t(s.sw)]) >> 2);
        const int cells = (s.ne - s.nw) / 2;
        const int top = s.nw + cells, left = s.nw + cells * kSide;
        const int bottom = s.sw + cells, right = s.ne + cells * kSide;
        side(top, s.nw, s.ne, cells);
        side(left, s.nw, s.sw, cells);
        side(bottom, s.sw, s.se, cells);
        // Bug 37: the right side's noise is scaled by cells / 8, which is 0:
        // the whole noise byte is added.
        side(right, s.ne, s.se, fix_noise ? cells : cells >> 3);
        stack.push_back({s.nw, top, left, centre});
        stack.push_back({top, s.ne, centre, right});
        stack.push_back({left, centre, s.sw, bottom});
        stack.push_back({centre, right, bottom, s.se});
    }
}

}  // namespace

Terrain::Terrain(const formats::Tables& tables, const Fixes& fixes)
    : tables_(tables), fixes_(&fixes), noise_(start_up_noise()) {}

void Terrain::copied(const formats::MtpMap& from, const formats::MtpMap& to, const Fixes& fixes) {
    if (map_ && map_ != &from) throw std::logic_error("the terrain is another map's");
    if (map_) map_ = &to;
    fixes_ = &fixes;
}

std::array<std::uint8_t, 256> Terrain::start_up_noise() {
    Random random;
    std::array<std::uint8_t, 256> noise{};
    for (auto& n : noise) n = random.next();
    return noise;
}

int Terrain::height(int index) const {
    // Before the table the noise table's end, after it 18 bytes of 0 and
    // then the templates.
    if (index < 0) return noise_[std::size_t(256 + index)];
    if (index < 256) return tables_.world_heights[std::size_t(index)];
    if (index < 256 + 18) return 0;
    return tables_.terrain_templates[std::size_t(index - 256 - 18)];
}

std::array<std::uint8_t, Terrain::kGridBlocks> Terrain::area_types(int nw, int ne, int sw, int se) const {
    Lattice h;
    h.fill(kEmpty);
    const int last = kSide - 1;
    h[0] = std::uint8_t(nw);
    h[std::size_t(last)] = std::uint8_t(ne);
    h[std::size_t(kSide * last)] = std::uint8_t(sw);
    h[std::size_t(kSide * last + last)] = std::uint8_t(se);
    // Each edge's noise starts at the sum of its corners, so two areas
    // make the same points along the edge they share.
    const auto edge = [&](int a, int b, int step) {
        Noise noise{noise_, std::uint8_t(h[std::size_t(a)] + h[std::size_t(b)])};
        subdivide_edge(h, noise, a, b, step);
        return noise.index;
    };
    edge(0, last, 1);
    edge(kSide * last, kSide * last + last, 1);
    edge(0, kSide * last, kSide);
    const std::uint8_t index = edge(last, kSide * last + last, kSide);
    Noise noise{noise_, index};
    subdivide_inside(h, noise, fixes_->fix_terrain_noise);
    noise_cursor_ = noise.index;
    // The type is the height's top four bits; the ninth row and column
    // belong to the neighbours.
    std::array<std::uint8_t, kGridBlocks> types{};
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) types[std::size_t(8 * r + c)] = h[std::size_t(kSide * r + c)] & 0xf0;
    return types;
}

void Terrain::generate(int area) {
    built_.reset();
    // Grid k is area + 16 (k / 3 - 1) + (k % 3 - 1), its corners the heights
    // of it, the next area east, south and south-east; no wrapping at the
    // world's edges.
    for (int k = 0; k < kGrids; ++k) make_grid(k, area);
    link_blocks();
}

void Terrain::make_grid(int k, int area) {
    const int a = area - 0x11 + 16 * (k / 3) + k % 3;
    const auto types = area_types(height(a), height(a + 1), height(a + 16), height(a + 17));
    std::copy(types.begin(), types.end(), grids_.begin() + k * kGridBlocks);
}

std::array<int, 3> Terrain::step_area(int dx, int dy, int area) {
    // The cell window stays as it was built until 287f:1314 builds it again.
    hold_window();
    built_.reset();
    const auto copy = [&](int to, int from) {
        std::copy_n(grids_.begin() + from * kGridBlocks, kGridBlocks, grids_.begin() + to * kGridBlocks);
    };
    std::array<int, 3> fresh{};
    if (dy < 0) {
        for (int k = kGrids - 1; k >= 3; --k) copy(k, k - 3);
        fresh = {0, 1, 2};
    } else if (dy > 0) {
        for (int k = 0; k < 6; ++k) copy(k, k + 3);
        fresh = {6, 7, 8};
    } else if (dx < 0) {
        for (int r = 0; r < 3; ++r) {
            copy(3 * r + 2, 3 * r + 1);
            copy(3 * r + 1, 3 * r);
        }
        fresh = {0, 3, 6};
    } else {
        for (int r = 0; r < 3; ++r) {
            copy(3 * r, 3 * r + 1);
            copy(3 * r + 1, 3 * r + 2);
        }
        fresh = {2, 5, 8};
    }
    for (const int k : fresh) make_grid(k, area);
    link_blocks();
    for (std::size_t i = 0; i < 3; ++i) {
        queued_grids[i] = std::uint8_t(fresh[i]);
        queued_areas[i] = std::uint8_t(area - 0x11 + 16 * (fresh[i] / 3) + fresh[i] % 3);
    }
    return fresh;
}

void Terrain::place_map(const formats::MtpMap& map, int grid) {
    built_.reset();
    map_ = &map;
    int n = kFirstMapBlock;
    for (int r = 0; r < map.height / formats::kBlockCells; ++r)
        for (int c = 0; c < map.width / formats::kBlockCells; ++c)
            grids_[std::size_t(grid * kGridBlocks + (map.block_y + r) * 8 + map.block_x + c)] = std::uint8_t(n++);
}

void Terrain::link_blocks() {
    built_.reset();
    // The grids sit side by side as the 24x24 blocks around the party's area.
    const auto entry = [&](int bx, int by) -> int {
        if (bx < 0 || by < 0 || bx >= 24 || by >= 24) return -1;
        return grids_[std::size_t(((by / 8) * 3 + bx / 8) * kGridBlocks + (by % 8) * 8 + bx % 8)];
    };
    for (int k = 0; k < kGrids; ++k)
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                const int bx = (k % 3) * 8 + c, by = (k / 3) * 8 + r;
                const int e = entry(bx, by);
                auto& mask = masks_[std::size_t(k * kGridBlocks + 8 * r + c)];
                // A map block's mask is its number in the block store, except
                // that block 0x80 on a grid's top or bottom row (not a corner)
                // is compared like a terrain type.
                const bool middle_of_top_or_bottom = (r == 0 || r == 7) && c > 0 && c < 7;
                if (e >= 0x80 && !(e == 0x80 && middle_of_top_or_bottom)) {
                    mask = std::uint8_t(e - 0x80);
                    continue;
                }
                mask = 0;
                if (entry(bx, by - 1) == e) mask |= 1;  // north
                if (entry(bx + 1, by) == e) mask |= 2;  // east
                if (entry(bx, by + 1) == e) mask |= 4;  // south
                if (entry(bx - 1, by) == e) mask |= 8;  // west
            }
}

// BTECH.EXE 287f:13d9 (Ghidra FUN_287f_13d9)
std::uint8_t Terrain::copy_block(int column, int row, const formats::MtpMap& map, std::uint8_t* out) const {
    // Only row -1 / 8 and column -1 / 8 step into the next grid; any other
    // row is taken as eight blocks a row from the centre grid's start (a
    // byte), so a place off the map (y ffff, rows 14-16) reads the grids
    // after it (docs/movement.md).
    int at = row == -1 ? 1 * kGridBlocks + 7 * 8 : row == 8 ? 7 * kGridBlocks : 4 * kGridBlocks + ((row * 8) & 0xff);
    if (column == -1) at += 7 - kGridBlocks;
    else if (column == 8) at += kGridBlocks;
    else at += column;
    const int mask = masks_[std::size_t(at)], entry = grids_[std::size_t(at)];
    // The block store: the templates, then the map's blocks.
    const auto stored = [&](int n, int i) -> int {
        if (n < 16) return tables_.terrain_templates[std::size_t(n * 64 + i)];
        const std::size_t b = std::size_t((n - 16) * 64 + i);
        if (b < map.blocks.size()) return int(map.blocks[b]);
        // Past the map store's 64 blocks (P:201d): blocks 0x50 and 0x51 of
        // the program's own; the cache's grids are all block 0x50 (d0).
        const std::size_t tail = b - kMapStoreBytes;
        return b >= kMapStoreBytes && tail < tables_.store_tail.size() ? tables_.store_tail[tail] : 0;
    };
    std::array<int, 64> block{};
    if (entry >= 0x80) {
        for (int i = 0; i < 64; ++i) block[std::size_t(i)] = stored(mask, i);
    } else if (entry == 0x10) {
        for (int i = 0; i < 64; ++i) block[std::size_t(i)] = tables_.plain_block[std::size_t(i)];
    } else {
        // The type's tiles are the template's (low four bits) plus an offset.
        const int offset = entry == 0 ? 0 : std::min(entry - 0x10, 0x30);
        // Turned by its place: 1 upside down, 2 mirrored, 3 both; the mask
        // and the template's bytes below 0x40 are turned with it.
        const int turn = (row + column) & 3;
        const auto turned = [&](int b) {
            if (turn & 1) b = tables_.mirror_north_south[std::size_t(b)];
            if (turn & 2) b = tables_.mirror_east_west[std::size_t(b)];
            return b;
        };
        const int n = turned(mask);
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                int b = stored(n, 8 * r + c);
                if (b < 0x40) b = turned(b);
                const int to_r = (turn & 1) ? 7 - r : r, to_c = (turn & 2) ? 7 - c : c;
                block[std::size_t(8 * to_r + to_c)] = b < 0x40 ? (b & 0x0f) + offset : b;
            }
    }
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) out[r * kWindowCells + c] = std::uint8_t(block[std::size_t(8 * r + c)]);
    return std::uint8_t(mask | entry);
}

Terrain::Window Terrain::window(formats::MapPosition position, const formats::MtpMap& map) const {
    Window w;
    const int column = (position.x & 0xff) >> 4, row = (position.y & 0xff) >> 4;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            w.blocks[std::size_t(3 * r + c)] =
                copy_block(column + c - 1, row + r - 1, map, &w.cells[std::size_t(8 * r * kWindowCells + 8 * c)]);
    return w;
}

const Terrain::Window& Terrain::window(formats::MapPosition position) const {
    static const formats::MtpMap kNoMap{};
    const int column = (position.x & 0xff) >> 4, row = (position.y & 0xff) >> 4;
    if (!built_ || column != built_column_ || row != built_row_ || area_of(position) != built_area_) {
        built_ = window(position, map_ ? *map_ : kNoMap);
        built_column_ = column;
        built_row_ = row;
        built_area_ = area_of(position);
    }
    return *built_;
}

std::optional<int> Terrain::window_cell(formats::MapPosition position, int index) const {
    constexpr int kCells = kWindowCells * kWindowCells, kBlocks = 9, kEntries = kGrids * kGridBlocks;
    if (index >= kCells) {
        // From P:09ed on. 287f:1df8's corner: column ((x & 0xff) >> 1 & 7)
        // + 2, row likewise from y, times 24 (the window's row length).
        int at = index - kCells;
        const int column = (((position.x & 0xff) >> 1) & 7) + 2;
        const int row = ((((position.y & 0xff) >> 1) & 7) + 2) * kWindowCells;
        const std::array<int, 3> words{column + row, corner_words_set ? column : 0, corner_words_set ? row : 0};
        if (at < 6) return (words[std::size_t(at / 2)] >> (8 * (at & 1))) & 0xff;
        at -= 6;
        if (at < 3) return queued_grids[std::size_t(at)];
        at -= 3;
        if (at < 3) return queued_areas[std::size_t(at)];
        at -= 3;
        if (at < 2) return (noise_cursor_ >> (8 * at)) & 0xff;
        at -= 2;
        // P:09fb: the noise table, then 16 bytes of 0, the heights (P:0b0b),
        // 18 bytes of 0 (height() reads them the same way).
        if (at < 256) return noise_[std::size_t(at)];
        at -= 256;
        if (at < 16) return 0;
        at -= 16;
        if (at < 256 + 18) return height(at);
        at -= 256 + 18;
        // P:0c1d: the block store, 64 bytes a block: the 16 templates, then
        // the map's blocks (P:101d).
        if (at < 16 * 64) return tables_.terrain_templates[std::size_t(at)];
        at -= 16 * 64;
        if (map_ && std::size_t(at) < map_->blocks.size()) return map_->blocks[std::size_t(at)];
        if (map_ && map_->blocks.size() == kMapStoreBytes && std::size_t(at) - kMapStoreBytes < tables_.store_tail.size())
            return tables_.store_tail[std::size_t(at) - kMapStoreBytes];
        return std::nullopt;
    }
    if (index >= 0) return window(position).cells[std::size_t(index)];
    if (index >= -kBlocks) return window(position).blocks[std::size_t(kBlocks + index)];
    index += kBlocks;
    if (index >= -kEntries) return grids_[std::size_t(kEntries + index)];
    index += kEntries;
    if (index >= -kEntries) return masks_[std::size_t(kEntries + index)];
    return std::nullopt;
}

std::optional<int> Terrain::cell(formats::MapPosition position, int x, int y) const {
    const int left = (((position.x & 0xff) >> 4) - 1) * 8, top = (((position.y & 0xff) >> 4) - 1) * 8;
    if (x < left || y < top || x >= left + kWindowCells || y >= top + kWindowCells) return std::nullopt;
    return window(position).cells[std::size_t((y - top) * kWindowCells + x - left)];
}

}  // namespace chr::game
