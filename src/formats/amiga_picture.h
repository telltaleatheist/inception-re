// The Amiga version's pictures and tile sheets, its .cmp and .icn files (docs/formats/amiga.md,
// "Pictures and tile sheets"): each the DOS picture or sheet with a palette of its own in front,
// stored as bit planes. These read them into the DOS working forms (formats/cmp.h CmpPicture,
// formats/icn.h TileSheet), the colour numbers indexing the file's own palette.
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "formats/cmp.h"
#include "formats/icn.h"

namespace chr::formats {

// 16 Amiga colours, each 0x0RGB (4 bits a component).
using AmigaPalette = std::array<std::uint16_t, 16>;

struct AmigaPicture {
    AmigaPalette palette{};
    CmpPicture picture;  // DOS's working form; `order` as the file says
};

struct AmigaTileSheet {
    AmigaPalette palette{};
    TileSheet sheet;  // DOS's working form: tile n at n * 128
};

// A full-screen picture (every .cmp): palette, length,
// type byte and the run-length stream, unpacked to four planes of 8,000 bytes.
AmigaPicture decode_amiga_cmp(std::span<const std::uint8_t> file);

// A compressed tile sheet (bttltech.icn, destruct.icn, starleag.icn, animate.icn): the same
// container; tile n's row y is 2 bytes at n * 32 + y * 2 of each plane.
AmigaTileSheet decode_amiga_icn(std::span<const std::uint8_t> file);

// map.icn: palette, length, then 170 tiles stored as they are, each 16 rows of four planes' 2
// bytes. Its tile n is DOS MAP.ICN's tile n + 80 (DOS's first 80 are blank, and stay blank here).
AmigaTileSheet decode_amiga_map_icn(std::span<const std::uint8_t> file);

// The first map.icn tile's number in the DOS sheet.
inline constexpr int kAmigaMapIcnFirstTile = 80;

}  // namespace chr::formats
