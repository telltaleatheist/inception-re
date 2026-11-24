// The Amiga 2.3 program's pieces by the DOS import table's IDs (formats/program_data.h): what
// the importer takes from an Amiga copy, in DOS 1.03's forms (docs/formats/amiga.md, "The
// texts").
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

class AmigaProgramData final : public ProgramData {
public:
    // Reads and checks the program `battletech` (formats/amiga_program.h), then every text
    // against its hash. Throws FormatError naming the texts that differ.
    explicit AmigaProgramData(std::span<const std::uint8_t> file);

    std::span<const std::uint8_t> get(std::string_view name) const override;
    std::string text(std::string_view name) const override;
    std::string address(std::string_view name) const override;
    std::string source() const override { return "Amiga battletech 2.3"; }
    std::string sha256() const override { return sha256_; }

private:
    std::map<std::string, std::vector<std::uint8_t>, std::less<>> pieces_;  // DOS form, by ID
    std::map<std::string, std::string, std::less<>> addresses_;
    std::string sha256_;
};

}  // namespace chr::formats
