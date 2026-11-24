// The Amiga copy's files, on its disk image or in a folder (docs/formats/adf.md,
// docs/formats/amiga.md): which they are, and finding them.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace chr::formats {

// The game's own files, in the disk's Battletech folder; what else is there (saved games, the
// Workbench's files) is not the game's.
inline constexpr std::array<std::string_view, 18> kAmigaFiles = {
    "battletech",   "builds.bld",   "maps",        "animate.icn", "animation.anm", "btborder.cmp",
    "btstats.cmp",  "bttitle.cmp",  "bttltech.icn", "demofile",   "destruct.icn",  "endmech.cmp",
    "infocom.cmp",  "map.icn",      "mechshap.cmp", "snd",        "starleag.icn",  "tinyland.cmp"};

// Without these a disk or a folder isn't taken for the game's.
inline constexpr std::array<std::string_view, 3> kAmigaTellingFiles = {"battletech", "builds.bld", "maps"};

using AmigaFiles = std::map<std::string, std::vector<std::uint8_t>>;  // by lower-case name

// The game's files on a disk image; empty when it is not an Amiga disk with the program, the
// scripts and the maps. Never throws for what the image holds.
AmigaFiles amiga_disk_files(std::span<const std::uint8_t> image);
// The game's files that `files` hasn't got.
std::vector<std::string> amiga_files_missing(const AmigaFiles& files);

// `dir`, or its folder Battletech, when it holds the program, the scripts and the maps.
std::optional<std::filesystem::path> amiga_folder(const std::filesystem::path& dir);

// The game's disk images straight in `dir`, by name.
struct AmigaImage {
    std::filesystem::path path;
    AmigaFiles files;
};
std::vector<AmigaImage> amiga_images_in(const std::filesystem::path& dir);

}  // namespace chr::formats
