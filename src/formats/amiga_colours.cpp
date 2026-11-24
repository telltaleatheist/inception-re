#include "formats/amiga_colours.h"

#include "formats/cmp.h"  // FormatError

namespace chr::formats {

// Amiga battletech 0:1c4a0 (Ghidra FUN_0002c4a0)
// Sets the screen's 16 colours from a palette's words: the top bit of each component's four is
// dropped and the other three doubled.
Rgb amiga_colour(std::uint16_t word) {
    const auto component = [&](int shift) { return std::uint8_t(((word >> shift) & 7) * 2 * 0x11); };
    return {component(8), component(4), component(0)};
}

Palette16 amiga_palette(std::span<const std::uint8_t> words) {
    if (words.size() < 32) throw FormatError("an Amiga palette is short");
    Palette16 out{};
    for (std::size_t n = 0; n < out.size(); ++n)
        out[dos_colour(std::uint8_t(n))] = amiga_colour(std::uint16_t(words[2 * n] << 8 | words[2 * n + 1]));
    return out;
}

void renumber_text_colours(std::span<std::uint8_t> text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const std::uint8_t c = text[i];
        if (c == 0 || (c & 0x80)) return;
        if (c != 0x02 && c != 0x06 && c != 0x09 && c != 0x13) continue;
        if (++i < text.size() && (c == 0x02 || c == 0x06)) text[i] = dos_colour(text[i]);
    }
}

}  // namespace chr::formats
