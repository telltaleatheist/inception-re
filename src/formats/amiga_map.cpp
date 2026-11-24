// The Amiga maps (docs/formats/amiga.md, "The maps").

#include "formats/amiga_map.h"

#include <algorithm>
#include <string>

#include "formats/cmp.h"  // FormatError

namespace chr::formats {

namespace {

constexpr std::size_t kWordTables = 0x185, kWordTablesEnd = 0x205;  // formats/mtp.h: 4 x 16 words
constexpr std::size_t kNames = 0x005, kNameBytes = 16;              // the 8 townspeople's names

std::size_t offset_of(std::span<const std::uint8_t> offsets, int index) {
    const std::size_t at = std::size_t(index) * 4;
    return std::size_t(offsets[at]) << 24 | std::size_t(offsets[at + 1]) << 16 | std::size_t(offsets[at + 2]) << 8 |
           offsets[at + 3];
}

// Map `number`'s bytes as stored, from its offset to the next map's (the last, to the file's end).
std::span<const std::uint8_t> stored(std::span<const std::uint8_t> maps, std::span<const std::uint8_t> offsets,
                                     int number) {
    const std::size_t start = offset_of(offsets, number - 1);
    const std::size_t end = number == kAmigaMaps ? maps.size() : offset_of(offsets, number);
    if (start > end || end > maps.size()) throw FormatError("the Amiga maps file is shorter than its offsets");
    return maps.subspan(start, end - start);
}

}  // namespace

// Amiga battletech 0:21d12: seeks to table[number - 1] and reads the map's fields one after
// another into place, the position words as the 68000 stores them.
std::vector<std::uint8_t> amiga_map(std::span<const std::uint8_t> maps, std::span<const std::uint8_t> offsets,
                                    int number, bool dos_rules) {
    if (number < 1 || number > kAmigaMaps || offsets.size() < std::size_t(kAmigaMaps) * 4)
        throw FormatError("no Amiga map " + std::to_string(number));
    const auto bytes = stored(maps, offsets, number);
    std::vector<std::uint8_t> map(bytes.begin(), bytes.end());
    if (number == kAmigaMaps) return map;  // map 15 has no header
    if (map.size() < kWordTablesEnd) throw FormatError("an Amiga map is shorter than its header");
    for (std::size_t w = kWordTables; w < kWordTablesEnd; w += 2) std::swap(map[w], map[w + 1]);
    if (dos_rules && number == 2) {
        const auto first = stored(maps, offsets, 1);
        if (first.size() < kNames + kNameBytes) throw FormatError("an Amiga map is shorter than its header");
        std::copy_n(first.begin() + kNames, kNameBytes, map.begin() + kNames);
    }
    return map;
}

}  // namespace chr::formats
