// The Amiga place pictures (docs/formats/amiga.md, "The place pictures").

#include "formats/amiga_anm.h"

#include <string>

#include "formats/cmp.h"  // FormatError

namespace chr::formats {

namespace {

std::size_t offset_of(std::span<const std::uint8_t> offsets, int index) {
    const std::size_t at = std::size_t(index) * 4;
    return std::size_t(offsets[at]) << 24 | std::size_t(offsets[at + 1]) << 16 | std::size_t(offsets[at + 2]) << 8 |
           offsets[at + 3];
}

}  // namespace

// Amiga battletech 0:23f7a: seeks to table[n] in animation.anm and reads at most 22,000 bytes;
// 0:1ad54 then unpacks each frame as DOS does (formats/anm.h).
std::vector<std::uint8_t> amiga_place_picture(std::span<const std::uint8_t> anm, std::span<const std::uint8_t> offsets,
                                              int n) {
    if (n < 0 || n >= kAmigaPlacePictures || offsets.size() < std::size_t(kAmigaPlacePictures) * 4)
        throw FormatError("no Amiga place picture " + std::to_string(n));
    const std::size_t start = offset_of(offsets, n);
    const std::size_t end = n + 1 == kAmigaPlacePictures ? anm.size() : offset_of(offsets, n + 1);
    if (start > end || end > anm.size()) throw FormatError("animation.anm is shorter than its offsets");
    return {anm.begin() + std::ptrdiff_t(start), anm.begin() + std::ptrdiff_t(end)};
}

}  // namespace chr::formats
