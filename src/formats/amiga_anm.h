// The Amiga version's place pictures, the file `animation.anm` (docs/formats/amiga.md, "The place
// pictures"): DOS's O0-O21.ANM one after another, in DOS's own form, found by the program's table
// of offsets. This cuts one out; formats/anm.h decode_anm reads it.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace chr::formats {

inline constexpr int kAmigaPlacePictures = 22;

// Place picture `n` (0-21, DOS's O<n>.ANM) from `animation.anm` and the program's anm.offsets
// (amiga_program.h): from its offset to the next one's, the last to the file's end. Throws
// FormatError.
std::vector<std::uint8_t> amiga_place_picture(std::span<const std::uint8_t> anm, std::span<const std::uint8_t> offsets,
                                              int n);

}  // namespace chr::formats
