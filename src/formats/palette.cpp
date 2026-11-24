#include "formats/palette.h"

namespace chr::formats {

Rgb ega_200_line_colour(std::uint8_t value) {
    // In the 200-line modes the card takes bits 0-2 as blue, green and red at
    // two-thirds strength and bit 4 as a third added to all three. Colour 6
    // is the exception every IBM-compatible monitor makes: brown, not dark yellow.
    const bool bright = value & 0x10;
    const std::uint8_t on = 0xaa, add = bright ? 0x55 : 0x00;
    Rgb c{std::uint8_t((value & 4 ? on : 0) + add), std::uint8_t((value & 2 ? on : 0) + add),
          std::uint8_t((value & 1 ? on : 0) + add)};
    if ((value & 0x17) == 0x06) c.g = 0x55;
    return c;
}

// BTECH.EXE 273d:0525 (Ghidra FUN_273d_0525)
// Sets the 16 palette registers for the chosen display. For EGA it takes 16
// colour numbers from a table and moves 8-15 up to 16-23, where the 200-line
// mode keeps its bright colours. The standard table (37e8:0000) was part of
// BTECH.EXE; it is now imported (tables.json: palette).
std::vector<Rgb> ega_colours(std::span<const std::uint8_t> numbers) {
    std::vector<Rgb> out;
    for (const std::uint8_t n : numbers) {
        const int number = n & 0x0f;
        out.push_back(ega_200_line_colour(std::uint8_t(number > 7 ? number + 8 : number)));
    }
    return out;
}

}  // namespace chr::formats
