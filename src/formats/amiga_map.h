// The Amiga version's maps, the file `maps` (docs/formats/amiga.md, "The maps"): DOS's
// MAP1-MAP15.MTP one after another, found by the program's table of offsets. This cuts out one
// map in DOS form (formats/mtp.h decode_mtp reads it).
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace chr::formats {

inline constexpr int kAmigaMaps = 15;

// Map `number` (1-15) from `maps` and the program's maps.offsets (amiga_program.h), as DOS's
// MAPn.MTP: in maps 1-14 the four tables of 16 position words at 0x185, big-endian on the
// Amiga, are turned little-endian, and the Amiga's one change to the maps' contents is undone
// (docs/versions.md, "Amiga rules"): map 2's first townsperson gets the name map 1's first one
// has, as in DOS. Throws FormatError.
std::vector<std::uint8_t> amiga_map(std::span<const std::uint8_t> maps, std::span<const std::uint8_t> offsets,
                                    int number);

}  // namespace chr::formats
