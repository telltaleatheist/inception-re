// The rebuild's font file (docs/formats/font.md): a plain text picture of
// every character, written by the importer from the player's BTECH.EXE and
// readable and editable by hand, so a mod can bring its own font at any
// size.
//
//   chr-font 1
//   size 8 8
//   glyph 65
//   ..##....
//   .#..#...
//   ...       (height rows of width characters: # set, . clear)
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace chr::formats {

// Our own font, next to the program: shown for a copy that has none.
inline constexpr const char* kOwnFontFile = "data/font/font.txt";

struct Font {
    int width = 0, height = 0;
    // For each character 0..127, width*height values, 1 where the pixel is set.
    std::vector<std::vector<std::uint8_t>> glyphs;
};

// The original's 1-bit 8x8 font: 8 bytes per character, a byte per row, the
// top bit leftmost (BTECH.EXE 2c6c:a661).
Font font_from_original(std::span<const std::uint8_t> bits);

std::string write_font_file(const Font& font);
// Throws FormatError naming the line.
Font parse_font_file(std::string_view text);

}  // namespace chr::formats
