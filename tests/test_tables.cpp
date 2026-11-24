#include <string>

#include "formats/exe_data.h"
#include "formats/import.h"
#include "formats/json.h"
#include "formats/tables.h"
#include "test.h"

using namespace chr;

// tables.json gives back exactly what the importer read from the program.
TEST(tables_round_trip) {
    const auto dir = chrtest::game_dir();
    if (dir.empty()) return chrtest::skip("no game folder (set CHR_GAME_DIR)");
    const auto exe = formats::OriginalExe::load(dir);
    const auto tables = formats::tables_from_original(exe);
    const auto back = formats::read_tables_file(formats::write_tables_file(tables, formats::kImportVersion, "a test"));
    CHECK(back == tables);
    // Each piece table ends with its end mark, and each frame style names one.
    for (const auto& pieces : tables.frame_pieces) CHECK(pieces.back() == formats::kFramePieceEnd);
    for (const int p : tables.frame_style_pieces) CHECK(p >= 0 && p < formats::kFramePieceTables);
}

// A file whose numbers don't fit where the program kept them is refused.
TEST(tables_file_out_of_range_is_refused) {
    const auto* tables = chrtest::tables();
    if (!tables) return chrtest::skip("no imported tables (set CHR_GAME_DIR)");
    auto bad = *tables;
    bad.animated_tiles[0] = 300;  // stored as a byte
    bool refused = false;
    try {
        formats::read_tables_file(formats::write_tables_file(bad, formats::kImportVersion, "a test"));
    } catch (const std::exception&) {
        refused = true;
    }
    CHECK(refused);
}

// The palettes are kept as #rrggbb; anything else is refused.
TEST(tables_file_colours_are_hex) {
    const auto* tables = chrtest::tables();
    if (!tables) return chrtest::skip("no imported tables (set CHR_GAME_DIR)");
    auto changed = *tables;
    changed.stats_pulse[2] = {0x12, 0xab, 0xef};
    const auto text = formats::write_tables_file(changed, formats::kImportVersion, "a test");
    CHECK(formats::read_tables_file(text).stats_pulse[2] == changed.stats_pulse[2]);
    const auto at = text.find("#12abef");
    CHECK(at != std::string::npos);
    for (const char* wrong : {"12abef0", "#12abeg", "#12abe"}) {
        auto bad = text;
        bad.replace(at, 7, wrong);
        bool refused = false;
        try {
            formats::read_tables_file(bad);
        } catch (const std::exception&) {
            refused = true;
        }
        CHECK(refused);
    }
}
