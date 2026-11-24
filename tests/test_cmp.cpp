#include <array>
#include <vector>

#include "formats/cmp.h"
#include "formats/game_files.h"
#include "formats/palette.h"
#include "formats/tables.h"
#include "test.h"

using namespace chr::formats;

namespace {

// A .CMP file around a stream: length, type byte, stream.
std::vector<std::uint8_t> make_file(std::uint8_t type, const std::vector<std::uint8_t>& stream) {
    const std::size_t length = stream.size() + 1;
    std::vector<std::uint8_t> file{std::uint8_t(length), std::uint8_t(length >> 8), type};
    file.insert(file.end(), stream.begin(), stream.end());
    return file;
}

}  // namespace

TEST(literal_short_and_long_runs) {
    // 3 literals, a run of 5 (code -5), then a 16-bit run of 31,992 to fill.
    const std::uint16_t rest = 32000 - 3 - 5;
    const auto file = make_file(1, {0x03, 0x12, 0x34, 0x56, 0xfb, 0x77, 0x00,
                                    std::uint8_t(rest), std::uint8_t(rest >> 8), 0x9a});
    const auto picture = decode_cmp(file);
    CHECK(picture.order == CmpOrder::Rows);
    CHECK(picture.packed[0] == 0x12 && picture.packed[2] == 0x56);
    CHECK(picture.packed[3] == 0x77 && picture.packed[7] == 0x77);
    CHECK(picture.packed[8] == 0x9a && picture.packed[31999] == 0x9a);
    CHECK(picture.stream_bytes_left == 0);
}

TEST(zero_count_fills_the_picture) {
    const auto picture = decode_cmp(make_file(1, {0x00, 0x00, 0x00, 0x42, 0xee}));
    CHECK(picture.packed[0] == 0x42 && picture.packed[31999] == 0x42);
    CHECK(picture.stream_bytes_left == 1);  // the trailing 0xee is never read
}

TEST(column_order_runs_down_then_across) {
    // 201 literal bytes would need two codes; use runs: 200 of 0x11 then the rest 0x22.
    const std::uint16_t rest = 32000 - 200;
    const auto picture = decode_cmp(
        make_file(2, {0x00, 200, 0, 0x11, 0x00, std::uint8_t(rest), std::uint8_t(rest >> 8), 0x22}));
    CHECK(picture.order == CmpOrder::Columns);
    CHECK(picture.packed[0] == 0x11);                      // row 0, column 0
    CHECK(picture.packed[199 * kCmpRowBytes] == 0x11);     // row 199, column 0
    CHECK(picture.packed[1] == 0x22);                      // row 0, column 1
}

TEST(run_stops_when_the_picture_is_full) {
    // Two 16-bit runs of 20,000: the second is cut at 12,000.
    const auto picture = decode_cmp(
        make_file(1, {0x00, 0x20, 0x4e, 0x01, 0x00, 0x20, 0x4e, 0x02}));
    CHECK(picture.packed[19999] == 0x01 && picture.packed[20000] == 0x02);
}

TEST(short_data_is_an_error) {
    bool threw = false;
    try {
        decode_cmp(make_file(1, {0xfb, 0x77}));
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        decode_cmp(std::vector<std::uint8_t>{0xff, 0x00, 0x01});  // length past the end
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(pixels_are_high_nibble_first) {
    const auto picture = decode_cmp(make_file(1, {0x00, 0x00, 0x00, 0x5a}));
    const auto pixels = picture.pixels();
    CHECK(pixels.size() == std::size_t(kCmpWidth) * kCmpHeight);
    CHECK(pixels[0] == 0x5 && pixels[1] == 0xa);
}

TEST(ega_palette_colours) {
    const auto* tables = chrtest::tables();
    if (!tables) return chrtest::skip("no imported tables (set CHR_GAME_DIR)");
    CHECK((ega_200_line_colour(0x00) == Rgb{0, 0, 0}));
    CHECK((ega_200_line_colour(0x06) == Rgb{0xaa, 0x55, 0x00}));  // brown
    CHECK((ega_200_line_colour(0x17) == Rgb{0xff, 0xff, 0xff}));
    const auto& palette = tables->palette;
    CHECK(palette[1] == palette[0]);                                // 1 is black
    CHECK((palette[5] == Rgb{0x55, 0x55, 0xff}));                   // light blue
    CHECK((palette[9] == Rgb{0x00, 0x00, 0xaa}));                   // blue
    CHECK((palette[15] == Rgb{0xff, 0xff, 0xff}));
}

// The player's own files: every .CMP the game ships decodes to a full
// picture and uses its stream to the last byte.
TEST(shipped_cmp_files_decode) {
    const auto dir = chrtest::game_dir();
    if (dir.empty()) {
        chrtest::skip("no game folder (set CHR_GAME_DIR)");
        return;
    }
    for (const char* name : {"BTBORDER.CMP", "BTSTATS.CMP", "BTTITLE.CMP", "ENDMECH.CMP",
                             "INFOCOM.CMP", "MECHSHAP.CMP", "TINYLAND.CMP"}) {
        const auto path = find_game_file(dir, name);
        CHECK(path.has_value());
        if (!path) continue;
        const auto picture = decode_cmp(read_file(*path));
        CHECK(picture.packed.size() == kCmpPackedSize);
        CHECK(picture.stream_bytes_left == 0);
    }
}
