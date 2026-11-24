// The Amiga's colours in DOS's numbering (docs/formats/amiga.md, "Colours"): its pictures, texts
// and tables use colour numbers of their own, so its readers renumber them, and its palettes to
// match, and the rest of the program sees DOS's numbers and the copy's colours.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "formats/palette.h"

namespace chr::formats {

// DOS's number for each Amiga colour number.
inline constexpr std::array<std::uint8_t, 16> kAmigaToDosColour = {0, 1, 4, 14, 9, 5, 6, 12, 3, 2, 10, 8, 13, 11, 7, 15};

constexpr std::uint8_t dos_colour(std::uint8_t amiga) { return kAmigaToDosColour[amiga & 15]; }

// Two colour numbers in a byte, as the packed pictures hold them.
constexpr std::uint8_t dos_colour_pair(std::uint8_t pair) {
    return std::uint8_t(dos_colour(std::uint8_t(pair >> 4)) << 4 | dos_colour(pair));
}

// A colour word: 0x0RGB, of which the program keeps 3 bits a component and shows twice that on
// the Amiga's 0-15 scale.
Rgb amiga_colour(std::uint16_t word);

// A palette of 16 big-endian words, in DOS's order. Throws FormatError if it is short.
Palette16 amiga_palette(std::span<const std::uint8_t> words);

// Renumbers the colour codes' arguments (docs/text.md) in a text's bytes, as far as its end.
void renumber_text_colours(std::span<std::uint8_t> text);

}  // namespace chr::formats
