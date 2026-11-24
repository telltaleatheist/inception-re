// The Amiga program's pieces by the DOS import table's IDs (formats/amiga_program_data.h).

#include "formats/amiga_program_data.h"

#include <cstdio>

#include "formats/amiga_program.h"
#include "formats/cmp.h"  // FormatError
#include "formats/exe_data.h"
#include "formats/sha256.h"

namespace chr::formats {

namespace {

std::string_view dos_hash(std::string_view id) {
    for (const auto& span : OriginalExe::table())
        if (span.kind == ExeSpan::Kind::Text && span.name == id) return span.sha256;
    throw FormatError("the DOS import table has no text " + std::string(id));
}

// A text from `offset` in `hunk`, read as DOS's printer reads it, its bytes as stored.
std::string text_at(std::span<const std::uint8_t> hunk, std::uint32_t offset) {
    if (offset >= hunk.size()) throw FormatError("an Amiga text lies past its hunk's data");
    return OriginalExe::text_of(hunk.subspan(offset));
}

}  // namespace

AmigaProgramData::AmigaProgramData(std::span<const std::uint8_t> file) : sha256_(sha256_hex(file)) {
    const AmigaProgram program(file);
    std::string differ;
    int differing = 0;
    for (const AmigaText& t : amiga_texts()) {
        const auto hunk = program.hunk(t.hunk);
        std::string text = text_at(hunk, t.offset);
        for (const std::uint32_t join : t.joins)
            if (join) text += text_at(hunk, join);
        for (std::size_t i = 0; i < t.flip_count; ++i) {
            const std::size_t at = t.flips[i];
            if (at >= text.size()) throw FormatError("an Amiga text is shorter than its edits");
            text[at] = char(text[at] ^ 0x20);  // a letter's other case
        }
        std::vector<std::uint8_t> bytes(text.begin(), text.end());
        bytes.push_back(0);
        const std::string_view expected = t.sha256.empty() ? dos_hash(t.id) : t.sha256;
        if (sha256_hex(bytes).substr(0, 16) != expected) {
            if (differing++ < 5) differ += (differ.empty() ? "" : ", ") + std::string(t.id);
            continue;
        }
        char address[16];
        std::snprintf(address, sizeof address, "%u:%04x", unsigned(t.hunk), unsigned(t.offset));
        addresses_.emplace(t.id, address);
        pieces_.emplace(t.id, std::move(bytes));
    }
    if (differing)
        throw FormatError("the Amiga program's texts differ from version 2.3's (" + std::to_string(differing) +
                          ": " + differ + (differing > 5 ? ", ..." : "") + ")");
}

std::span<const std::uint8_t> AmigaProgramData::get(std::string_view name) const {
    const auto it = pieces_.find(name);
    if (it == pieces_.end()) throw FormatError("the Amiga copy has no piece " + std::string(name));
    return it->second;
}

std::string AmigaProgramData::text(std::string_view name) const { return OriginalExe::text_of(get(name)); }

std::string AmigaProgramData::address(std::string_view name) const {
    const auto it = addresses_.find(name);
    if (it == addresses_.end()) throw FormatError("the Amiga copy has no piece " + std::string(name));
    return it->second;
}

}  // namespace chr::formats
