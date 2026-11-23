// The overhead map: the command menu's item 7 (docs/overhead_map.md).

#include <algorithm>
#include <cstdio>
#include <string>

#include "formats/game_files.h"
#include "game/movement.h"
#include "game/town.h"

namespace chr::game {

namespace {

constexpr int kLandPieces = 0x42;  // TINYLAND.CMP's pieces (1527:0044)
constexpr int kBlockBytes = 64;
constexpr std::size_t kMtpHeader = 0x21d;
constexpr formats::MapPosition kPointerTarget{0x0a38, 0x8038};  // 1000:43f7
constexpr int kPointerSkipArea = 0x8a;
constexpr std::size_t kSeenCleared = 0x66c;  // F:d178 in the seen map (1000:3da5)

int area_of(formats::MapPosition p) { return ((p.x | p.y) >> 8) & 0xff; }

}  // namespace

// BTECH.EXE 1000:3d40 (Ghidra FUN_1000_3d40)
// The overhead map: drawn at a cursor that the arrow keys move two areas at
// a time, until Space (or after one key in the cache or on a town map
// indoors). Then the nine maps around the party are loaded again without
// placing the townspeople, and the map screen drawn again.
bool Town::overhead_map(KeySource& keys) {
    people_skipped_ = true;  // DG:014c
    // The cell window stays as last built until 287f:1314 on leaving.
    terrain_.hold_window();
    const formats::MapPosition party = position_;
    formats::MapPosition cursor = position_;
    const bool in_cache = flags_[flag::kTileEvents] != 0;
    for (;;) {
        const bool done = in_cache || flags_[flag::kOutdoors] == 0;
        if (done && in_cache) seen_[kSeenCleared] &= 0x7f;
        const auto key = draw_overhead(party, cursor, keys);
        if (!key) return false;
        if (arrow_key(*key) == key::kSpace) break;  // 2656:0d1d
        // Four tests in turn, so a diagonal moves on both axes.
        const int k = arrow_key(*key);
        using namespace key;
        if ((k == kUp || k == kPageUp || k == kHome) && cursor.y >= 0x2000) cursor.y = std::uint16_t(cursor.y - 0x2000);
        if ((k == kDown || k == kPageDown || k == kEnd) && cursor.y < 0xe000) cursor.y = std::uint16_t(cursor.y + 0x2000);
        if ((k == kLeft || k == kHome || k == kEnd) && cursor.x >= 0x300) cursor.x = std::uint16_t(cursor.x - 0x200);
        if ((k == kRight || k == kPageUp || k == kPageDown) && cursor.x < 0xd00) cursor.x = std::uint16_t(cursor.x + 0x200);
        if (done) break;
    }
    overhead_.reset();
    position_ = party;
    if (!in_cache) {
        // The nine-map load, the townspeople skipped (1000:3ebd).
        const int area = area_of(position_);
        terrain_.generate(area);  // 287f:104e
        for (int k = 0; k < 9; ++k) {
            const int a = area - 0x11 + 16 * (k / 3) + k % 3;
            if (a < 0 || a >= 0x100 || area_maps_[std::size_t(a)] == 0) continue;
            load_town_map(area_maps_[std::size_t(a)], k);  // 1000:2da8
        }
        terrain_.queued_grids.fill(0xff);  // P:09f3..09f5
        terrain_.link_blocks();            // 287f:1da8
    }
    build_window();          // 287f:1314
    record_party_places();   // 287f:18ef, 1000:051b
    text_.forget(13, 0, kTextColumns - 13, kTextRows);  // 273d:06c3
    draw_panel(true);        // 1000:4cac(1)
    clear_window();          // 2656:0388
    people_skipped_ = false;
    log_.push_back("overhead map closed");
    return true;
}

// BTECH.EXE 1000:3fae (Ghidra FUN_1000_3fae)
// One showing of the overhead map at `cursor`, the party's dot at `party`,
// until a key: the key, or nothing if the keys ran out.
std::optional<int> Town::draw_overhead(formats::MapPosition party, formats::MapPosition cursor, KeySource& keys) {
    OverheadMap shown;
    const bool in_cache = flags_[flag::kTileEvents] != 0;
    if (in_cache)
        shown.colours = flags_[flag::kCacheLevel] == 0 ? OverheadMap::Colours::CacheLevel0 : OverheadMap::Colours::CacheLevel1;
    text_.clear_all();  // 287f:1fbe
    const int col0 = std::clamp(((cursor.x & 0x0f00) >> 8) - 2, 0, 11);
    const int rowbase = std::clamp(((cursor.y & 0xf000) >> 8) - 0x10, 0, 0xd0);  // area row x 16

    // P:244b, the view: one byte per small tile. P:644b, the store of the
    // town blocks it names from 0x90, and which of its bytes were written.
    std::array<std::uint8_t, OverheadMap::kColumns * OverheadMap::kRows> view{};
    std::vector<std::uint8_t> store;
    std::vector<bool> written;
    const auto put = [&](std::size_t at, std::span<const std::uint8_t> bytes) {
        if (store.size() < at + bytes.size()) {
            store.resize(at + bytes.size());
            written.resize(at + bytes.size());
        }
        std::copy(bytes.begin(), bytes.end(), store.begin() + std::ptrdiff_t(at));
        std::fill_n(written.begin() + std::ptrdiff_t(at), bytes.size(), true);
    };
    // BTECH.EXE 287f:1f04 (Ghidra FUN_287f_1f04)
    // The middle grid into the view at `at`: a block number as it is, the
    // plain block as 0x40, a terrain type with its mask.
    const auto copy_middle = [&](int at) {
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                const std::size_t i = std::size_t(4 * Terrain::kGridBlocks + r * 8 + c);
                std::uint8_t b = terrain_.grids()[i];
                if (b < Terrain::kFirstMapBlock) {
                    if (b == 0x10) b = 0x40;
                    else b = std::uint8_t((b > 0x1f ? b - 0x10 : b) | terrain_.masks()[i]);
                }
                view[std::size_t(at + r * OverheadMap::kColumns + c)] = b;
            }
    };

    if (!in_cache) {
        int counter = Terrain::kFirstMapBlock;  // the running block number
        std::size_t offset = 0;                  // where the next map's cells go in the store
        for (int row = 0; row < 0x30; row += 0x10) {
            const int rowarea = rowbase + row;
            terrain_.generate(col0 | rowarea);  // 287f:104e
            for (int col = 0; col < 5; ++col) {
                bool placed = false;
                const int base = ((col0 + col) | rowarea) - 0x11;
                for (int k = 0; k < 9; ++k) {
                    const int a = base + 16 * (k / 3) + k % 3;
                    if (a < 0 || a >= 0x100 || area_maps_[std::size_t(a)] == 0) continue;
                    const int map = area_maps_[std::size_t(a)];
                    const std::size_t m = std::size_t(map - 1);
                    if (m >= tables_.overhead_cell_bytes.size()) {
                        log_.push_back("overhead map: map " + std::to_string(map) + " has no entry: not rebuilt");
                        continue;
                    }
                    placed = true;
                    const bool middle = k == 4;
                    const int bx = tables_.overhead_block_x[m], by = tables_.overhead_block_y[m];
                    const int across = tables_.overhead_blocks_across[m], down = tables_.overhead_blocks_down[m];
                    for (int r = 0; r < down; ++r)
                        for (int c = 0; c < across; ++c) {
                            const std::size_t at = std::size_t(k * Terrain::kGridBlocks + (by + r) * 8 + bx + c);
                            if (middle) terrain_.set_grid_entry(at, std::uint8_t(counter++));
                            else if (terrain_.grids()[at] < Terrain::kFirstMapBlock)
                                terrain_.set_grid_entry(at, std::uint8_t(counter));
                        }
                    if (!middle) continue;
                    // 1000:40bd: the header, then the cells over it.
                    const std::string name = "MAP" + std::to_string(map) + ".MTP";
                    const auto path = game_dir_ ? formats::find_game_file(*game_dir_, name) : std::nullopt;
                    if (!path) {
                        log_.push_back("overhead map: " + name + " not found");
                        continue;
                    }
                    const auto file = formats::read_file(*path);
                    const std::span<const std::uint8_t> bytes(file);
                    const std::size_t want = std::size_t(tables_.overhead_cell_bytes[m]);
                    put(offset, bytes.first(std::min(kMtpHeader, bytes.size())));
                    if (bytes.size() > kMtpHeader) put(offset, bytes.subspan(kMtpHeader, std::min(want, bytes.size() - kMtpHeader)));
                    offset += fixes_.fix_overhead_store ? std::size_t(across * down * kBlockBytes) : want;
                }
                if (placed) terrain_.link_blocks();  // 287f:1da8
                copy_middle((row / 0x10) * OverheadMap::kColumns * 8 + col * 8);
                // 287f:17c5, the position's x at the area's last cell: the
                // grids step right (past the view after the fifth).
                terrain_.step_area(1, 0, rowarea | ((col0 + col + 1) & 0x0f));
            }
        }
    } else {
        // 1000:42d7: the store as the level left it, and the level's block.
        std::vector<std::uint8_t> blocks(map_.blocks.begin(), map_.blocks.end());
        blocks.resize(Terrain::kMapStoreBytes);
        for (const int b : tables_.store_tail) blocks.push_back(std::uint8_t(b));
        put(0, blocks);
        view.fill(std::uint8_t(flags_[flag::kCacheLevel] - 0x30));
        copy_middle(0x150);
    }

    // 1000:432c: the seen small tiles, rowbase x 8 + col0 on, 16 bytes a row.
    std::size_t seen_at = std::size_t(rowbase * 8 + col0);
    bool stale = false;
    for (int r = 0; r < OverheadMap::kRows; ++r) {
        int bit = 0x80;
        for (int c = 0; c < OverheadMap::kColumns; ++c) {
            const std::uint8_t v = view[std::size_t(r * OverheadMap::kColumns + c)];
            auto& tile = shown.tiles[std::size_t(r * OverheadMap::kColumns + c)];
            if (seen_at < seen_.size() && (seen_[seen_at] & bit) != 0) {
                if (v < Terrain::kFirstMapBlock) {
                    if (v >= kLandPieces)
                        log_.push_back("overhead map: piece " + std::to_string(v) + " is past TINYLAND's: not rebuilt");
                    tile = {OverheadMap::Tile::Kind::Land, v};
                } else {
                    // BTECH.EXE 1000:45c2, 287f:1f51: block v - 0x90 of the store.
                    OverheadMap::Block block{};
                    const std::size_t from = std::size_t(v - Terrain::kFirstMapBlock) * kBlockBytes;
                    for (std::size_t i = 0; i < block.size(); ++i) {
                        if (from + i < store.size() && written[from + i]) block[i] = store[from + i];
                        else stale = true;
                    }
                    tile = {OverheadMap::Tile::Kind::Block, int(shown.blocks.size())};
                    shown.blocks.push_back(block);
                }
            }
            bit >>= 1;
            if (bit == 0) {
                bit = 0x80;
                ++seen_at;
            }
        }
        seen_at += 0x0b;
    }
    if (stale) log_.push_back("overhead map: store bytes no pass wrote, drawn as 0: not rebuilt");

    // 1000:43d6: the pointer, from the cursor toward 0a38,8038.
    if (!in_cache && flags_[flag::kOverheadPointer] != 0 && (area_of(cursor) & 0xff) != kPointerSkipArea) {
        const int dir = direction_toward(cursor, kPointerTarget, tables_);  // 287f:0971
        if (dir >= 0) {
            const char glyph[2] = {char(dir + 1), 0};
            text_.put_text(std::string_view(glyph, 1), tables_.overhead_pointer_column[std::size_t(dir)],
                           tables_.overhead_pointer_row[std::size_t(dir)], TextRole::MapPointer, TextRole::Background);
        }
    }
    // 1000:4449: the caption on row 24.
    text_.fill(0, 24, kTextColumns, 1, TextRole::Background);  // 273d:01fb
    const bool outdoors = !in_cache && flags_[flag::kOutdoors] != 0;
    text_.put_text(lookup(texts_, outdoors ? "overhead.caption_outdoors" : "overhead.caption"), 0, 24, TextRole::Text,
                   TextRole::Background);

    // 1000:44aa: the party's dot; each pass draws a random number for its
    // colour while it is on the view, then asks for a key.
    int x = ((party.x & 0x7f) >> 1) + ((((party.x & 0x0f00) >> 8) - col0) << 6);
    const int y = ((party.y & 0x7f) >> 1) + ((((party.y & 0xf000) >> 8) - rowbase) << 2);
    const bool on_view = x >= 0 && x < 0x140 && y >= 0 && y < 0xc0;
    x = std::min(x, 0x13e);
    // While the demo plays (F:3938, 1000:4578) the map stays up for 600
    // frames and more, and the key is the demo's next.
    int demo_frames = 0x258;  // 1000:4449
    for (;;) {
        if (on_view) shown.dot = OverheadMap::Dot{x, y, random_.next() & 0x0f};  // 287f:0bc0
        overhead_ = shown;
        if (demo_) {
            const bool done = demo_frames-- == 0;
            if (!demo_->wait(1)) {
                place_open_ = true;
                return std::nullopt;
            }
            if (!done) continue;
        }
        const auto k = keys.waiting_key();  // 273d:002f, 273d:0259
        if (!k) {
            place_open_ = true;
            poll_open_ = true;
            return std::nullopt;
        }
        if (*k != KeySource::kNoKey) return *k;  // then 1000:2a2b
    }
}

}  // namespace chr::game
