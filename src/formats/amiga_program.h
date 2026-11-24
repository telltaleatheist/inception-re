// The Amiga version's program, `battletech` (docs/formats/amiga.md): an AmigaDOS load file whose
// hunks hold the game's code, its tables and its interface text. This reads the hunks as the
// file stores them (relocations are not applied: the spans read here hold no pointers) and
// finds a span by its ID, as formats::OriginalExe does for BTECH.EXE.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "formats/tables.h"

namespace chr::formats {

struct AmigaHunk {
    enum class Kind { Code, Data, Bss } kind = Kind::Code;
    std::uint32_t size = 0;           // in memory, in bytes (from the header)
    std::vector<std::uint8_t> bytes;  // as stored (fewer than `size` when the rest is zero-filled)
};

// A load file's hunks, in order. Throws FormatError on anything that isn't one.
std::vector<AmigaHunk> read_amiga_hunks(std::span<const std::uint8_t> file);

// A piece of the program: `length` bytes at `offset` in hunk `hunk`.
struct AmigaSpan {
    std::string_view id;
    int hunk;
    std::uint32_t offset, length;
};

// The spans the import reads from the Amiga 2.3 program (docs/formats/amiga.md, "The program").
std::span<const AmigaSpan> amiga_spans();

class AmigaProgram {
public:
    // Reads the file and checks it is version 2.3's: its data hunk's hash. Throws FormatError.
    explicit AmigaProgram(std::span<const std::uint8_t> file);
    // A span's bytes, by ID (amiga_spans()). Throws FormatError for an unknown ID.
    std::span<const std::uint8_t> span(std::string_view id) const;
    // Hunk `n`'s bytes as stored. Throws FormatError for a hunk the program doesn't have.
    std::span<const std::uint8_t> hunk(std::size_t n) const;

private:
    std::vector<AmigaHunk> hunks_;
};

// The menu table (span "menus") as DOS's records (formats/tables.h MenuRecord): the same fields
// in big-endian words. The highlight colour is the Amiga's own colour number.
std::array<MenuRecord, kMenuCount> amiga_menus(std::span<const std::uint8_t> menus);

// The SHA-256 of version 2.3's data hunk, as stored: the same in all three known builds.
inline constexpr std::string_view kAmigaDataHunkSha256 =
    "7e3d720c2be3047430d4c6ab12fea1e126613c3e1f030048ad3b170cd06e64ba";

}  // namespace chr::formats
