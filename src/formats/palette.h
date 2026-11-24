// Screen colours. A .CMP, .ICN or .ANM file holds colour numbers only; what they look like comes
// from the palettes the program sets before each picture is shown (docs/formats/cmp.md,
// "Palette"), which the importer writes as plain colours whatever the copy's hardware.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace chr::formats {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb&) const = default;
};

using Palette16 = std::array<Rgb, 16>;

// The colour an EGA card in its 200-line 16-colour mode shows for a palette
// register value: the standard IRGB colours (bit 4 is the bright bit).
Rgb ega_200_line_colour(std::uint8_t register_value);

// What DOS's stored colour numbers (0-15) look like on the 256K EGA/VGA
// display (choice 3).
std::vector<Rgb> ega_colours(std::span<const std::uint8_t> numbers);

}  // namespace chr::formats
