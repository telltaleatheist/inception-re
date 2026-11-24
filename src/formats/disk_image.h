// Floppy disk images the other versions of the game come on, read as lists of files: the Amiga's
// .adf (docs/formats/adf.md), the C64's .d64 (docs/formats/d64.md) and the Apple II's RDOS .dsk
// (docs/formats/rdos.md). Each reader checks the image's size and structure and throws
// FormatError (formats/cmp.h) on anything it can't follow; a damaged chain or a loop stops it
// rather than reading past the image.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace chr::formats {

struct ImageFile {
    std::string path;                // '/'-separated, as the disk names it
    std::vector<std::uint8_t> data;  // the file's bytes (a C64 file keeps its two-byte load address)
};

enum class DiskKind { None, Adf, D64, Rdos };

// Which reader an image of this size is for (by size alone; the reader then checks).
DiskKind disk_kind_by_size(std::uint64_t size);

// Amiga DOS floppy, OFS or FFS (880K or 1760K).
std::vector<ImageFile> read_adf(std::span<const std::uint8_t> image);
// The Amiga volume's name (its root block).
std::string adf_volume(std::span<const std::uint8_t> image);

// Commodore 1541 disk, 35 tracks (with or without the error bytes after the sectors).
std::vector<ImageFile> read_d64(std::span<const std::uint8_t> image);

// Apple II 140K disk in DOS 3.3 sector order, under RDOS 3.3 (its catalog on track 1, its own sector order).
std::vector<ImageFile> read_rdos(std::span<const std::uint8_t> image);

// Any of the three, by size; empty for an image of none of their sizes.
std::vector<ImageFile> read_disk_image(std::span<const std::uint8_t> image);

}  // namespace chr::formats
