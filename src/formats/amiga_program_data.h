// The Amiga 2.3 program's pieces by the DOS import table's IDs (formats/program_data.h): what
// the importer takes from an Amiga copy, in DOS 1.03's forms (docs/formats/amiga.md, "The
// texts" and "The tables").
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "formats/program_data.h"

namespace chr::formats {

// Where one of the DOS import table's texts is in the Amiga program, and how it becomes DOS's.
struct AmigaText {
    std::string_view id;  // the DOS text ID (formats/exe_data.h)
    std::uint8_t hunk = 0;
    std::uint32_t offset = 0;
    // The text's expected hash, as the DOS table's (first 16 hex digits of the SHA-256 of the
    // text and its NUL). Empty: the DOS table's own, because the result is DOS's text.
    std::string_view sha256 = {};
    // Letters whose case the Amiga changed, by position in the text: flipped back.
    std::array<std::uint8_t, 4> flips = {};
    std::uint8_t flip_count = 0;
    // Further texts in the same hunk that the Amiga prints after this one, joined on (0: none).
    std::array<std::uint32_t, 2> joins = {};
};

// One entry for every text in the DOS import table, in its order.
std::span<const AmigaText> amiga_texts();

// Where one of the DOS import table's tables is in the Amiga program, and how it becomes DOS's.
struct AmigaTable {
    enum class Form : std::uint8_t {
        Bytes,             // as stored
        Words,             // big-endian words
        Longs,             // big-endian longs
        Weapons,           // records of 18 bytes; DOS's are their first 17
        TemplatePointers,  // pointers to the 'Mech templates
        PiecePointers,     // pointers to the three frame piece tables
        PosePointers,      // pointers to the pose scripts
        PoseScripts,       // the pose scripts; DOS pads each to an even length
        Menus,             // big-endian words, the colours renumbered
        OverheadColours,   // the land's and the cache's tables, renumbered; DOS's third is the second again
    };
    struct Edit {
        std::uint16_t at = 0;    // in the DOS form
        std::uint8_t value = 0;  // DOS's value there
        bool flip = false;       // `value` is XORed in instead (a letter's case)
    };
    std::string_view id;  // the DOS import table's
    std::uint8_t hunk = 0;
    std::uint32_t offset = 0;
    Form form = Form::Bytes;
    // The DOS form's expected hash. Empty: the DOS table's own, because the result is DOS's table.
    std::string_view sha256 = {};
    std::array<Edit, 2> edits = {};
    std::uint8_t edit_count = 0;
};

// The tables that can be read so far, in the DOS import table's order.
std::span<const AmigaTable> amiga_tables();

// Where one of the palettes (ProgramData::colours) is in the Amiga program.
struct AmigaColours {
    std::string_view id;
    std::uint8_t hunk = 0;
    std::uint32_t offset = 0;
    std::uint8_t words = 16;  // 16: a palette, one for each colour number; fewer: steps of one colour
};

std::span<const AmigaColours> amiga_colour_tables();

class AmigaProgramData final : public ProgramData {
public:
    // Reads and checks the program `battletech` (formats/amiga_program.h), then every text
    // and table against its hash. Throws FormatError naming those that differ. `endmech` is
    // the file ENDMECH.CMP, whose palette the ending is shown with (docs/formats/amiga.md).
    AmigaProgramData(std::span<const std::uint8_t> file, std::span<const std::uint8_t> endmech);

    std::span<const std::uint8_t> get(std::string_view name) const override;
    std::string text(std::string_view name) const override;
    std::vector<Rgb> colours(std::string_view name) const override;
    std::string address(std::string_view name) const override;
    std::string source() const override { return "Amiga battletech 2.3"; }
    std::string sha256() const override { return sha256_; }

private:
    std::map<std::string, std::vector<std::uint8_t>, std::less<>> pieces_;  // DOS form, by ID
    std::map<std::string, std::vector<Rgb>, std::less<>> colours_;
    std::map<std::string, std::string, std::less<>> addresses_;
    std::string sha256_;
};

}  // namespace chr::formats
