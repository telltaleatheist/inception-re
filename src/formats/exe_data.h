// The import table: the data the rebuild takes from the player's own
// BTECH.EXE (the font, and the game's text and tables that live in the
// program rather than in a data file), each piece by name with its place in
// the unpacked program. Only the importer (formats/import.h) reads the
// program; the game reads what it wrote. We never ship any of it
// (docs/rules.md, rule 1).
//
// How a copy is accepted: first its layout (the EXEPACK unpack, the size,
// the header and relocation table, the entry point), so the addresses mean
// what the table says; then every piece the rebuild imports, each against
// its own hash. Bytes the rebuild does not import are not checked, so copies
// that differ only there are accepted. A copy that fails names the pieces
// that differ instead of handing back the wrong bytes. Other versions are
// added on purpose, when someone has one, with their own table.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "formats/program_data.h"

namespace chr::formats {

// One named piece of the program: segment:offset as in docs/functions.md
// (load segment 0x1000) and its length in bytes.
struct ExeSpan {
    enum class Kind { Font, Text, Table };
    Kind kind;
    std::string_view name;  // for text, the text ID (docs/text.md)
    std::uint16_t segment;
    std::uint16_t offset;
    std::uint16_t size;
    std::string_view sha256;  // the first 16 hex digits of the piece's SHA-256
};

// The version we support: DOS 1.03, unpacked SHA-256 below.
inline constexpr std::string_view kSupportedVersion = "DOS 1.03";

// The unpacked DOS 1.03 program's layout. The header (with the relocation
// table) is hashed whole: it has no game logic, only where things go.
struct ExeLayout {
    std::size_t size;
    std::uint16_t relocations, header_paragraphs;
    std::uint16_t cs, ip, ss, sp;
    std::string_view header_sha256;  // first 16 hex digits
};
inline constexpr ExeLayout kDos103Layout = {260528, 3292, 832, 0x187f, 0x2d82, 0x3c5e, 0x0800, "0054271f61ff947d"};

class OriginalExe final : public ProgramData {
public:
    // Unpacks and checks `packed` (the BTECH.EXE file). Throws FormatError
    // naming the supported version if it is not that program.
    explicit OriginalExe(std::span<const std::uint8_t> packed);
    // The same checks on a program already unpacked (for tests).
    static OriginalExe from_unpacked(std::vector<std::uint8_t> unpacked);
    static OriginalExe load(const std::filesystem::path& game_dir);

    // Where a piece is, "ssss:oooo", for the imported files.
    static std::string address(const ExeSpan& span);
    std::string address(std::string_view name) const override;

    // The bytes of the named piece. Throws FormatError for an unknown name.
    std::span<const std::uint8_t> get(std::string_view name) const override;
    // A named piece as text, read the way the printer (2656:03f5) reads it:
    // up to the NUL that ends it, where the codes 02, 06, 09 and 13 take the
    // next byte as their argument (so `06 00`, black, is not the end), and a
    // byte with the top bit set is the last one.
    std::string text(std::string_view name) const override;
    // The same reading of any bytes (for texts inside a table).
    static std::string text_of(std::span<const std::uint8_t> bytes);

    std::vector<Rgb> colours(std::string_view name) const override;

    static std::span<const ExeSpan> table();
    // The SHA-256 of the unpacked program (for the import's manifest).
    std::string sha256() const override;
    std::string source() const override;

private:
    OriginalExe() = default;
    void check();
    std::vector<std::uint8_t> unpacked_;
    std::size_t image_start_ = 0;  // where the load image starts in unpacked_
};

}  // namespace chr::formats
