// The player's copy as a GameSource, whichever version it is (docs/versions.md, "One source by
// ID"): told apart by what is there, and checked by the source's own readers.
#pragma once

#include <filesystem>
#include <memory>

#include "formats/game_source.h"

namespace chr::formats {

// `copy` is a folder, or the Amiga's disk image (.adf):
//   a folder with BTECH.EXE            the DOS copy
//   a folder with the Amiga's battletech, builds.bld and maps, or with a folder Battletech
//   holding them, or a disk image with them    the Amiga copy
// A folder that is none of these is taken as a DOS copy, so what is missing is said by name when
// it is read. Throws FormatError for a file that is not such a disk, or an Amiga program that
// isn't version 2.3's.
std::shared_ptr<const GameSource> open_game_source(const std::filesystem::path& copy);

}  // namespace chr::formats
