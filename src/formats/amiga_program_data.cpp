// The Amiga program's pieces by the DOS import table's IDs (formats/amiga_program_data.h).

#include "formats/amiga_program_data.h"

#include <algorithm>
#include <cstdio>
#include <set>

#include "formats/amiga_colours.h"
#include "formats/amiga_program.h"
#include "formats/cmp.h"  // FormatError
#include "formats/exe_data.h"
#include "formats/sha256.h"

namespace chr::formats {

namespace {

const ExeSpan& dos_span(std::string_view id) {
    for (const auto& span : OriginalExe::table())
        if (span.name == id) return span;
    throw FormatError("the DOS import table has no piece " + std::string(id));
}

std::uint32_t long_at(std::span<const std::uint8_t> b, std::size_t at) {
    if (b.size() < 4 || at > b.size() - 4) throw FormatError("an Amiga table runs past its hunk's data");
    return std::uint32_t(b[at]) << 24 | std::uint32_t(b[at + 1]) << 16 | std::uint32_t(b[at + 2]) << 8 | b[at + 3];
}

std::span<const std::uint8_t> bytes_at(std::span<const std::uint8_t> b, std::size_t at, std::size_t n) {
    if (at > b.size() || n > b.size() - at) throw FormatError("an Amiga table runs past its hunk's data");
    return b.subspan(at, n);
}

// DOS's far pointer as the unpacked program stores it: the segment before the load segment
// 0x1000 is added.
void put_far(std::vector<std::uint8_t>& out, std::uint16_t segment, std::uint32_t offset) {
    const std::uint16_t stored = std::uint16_t(segment - 0x1000);
    for (const unsigned v : {unsigned(offset), unsigned(stored)}) {
        out.push_back(std::uint8_t(v));
        out.push_back(std::uint8_t(v >> 8));
    }
}

const AmigaTable& amiga_table(std::string_view id) {
    for (const AmigaTable& t : amiga_tables())
        if (t.id == id) return t;
    throw FormatError("the Amiga table list has no " + std::string(id));
}

std::size_t pointer_count(const AmigaTable& t) { return dos_span(t.id).size / 4; }

// The pose scripts, by their Amiga addresses: each one's bytes run to the next script's start,
// and DOS lays the same scripts out in the same order, each padded with a 0 to an even length.
struct PoseScripts {
    std::map<std::uint32_t, std::uint32_t> dos_offset;  // Amiga address -> DG offset
    std::vector<std::uint8_t> dos;
};

PoseScripts pose_scripts(std::span<const std::uint8_t> hunk) {
    std::set<std::uint32_t> starts;
    for (const std::string_view id : {"tables.mech_poses", "tables.person_poses"}) {
        const AmigaTable& t = amiga_table(id);
        for (std::size_t i = 0; i < pointer_count(t); ++i) starts.insert(long_at(hunk, t.offset + 4 * i));
    }
    const ExeSpan& span = dos_span("tables.pose_scripts");
    PoseScripts out;
    for (auto it = starts.begin(); it != starts.end(); ++it) {
        const auto next = std::next(it);
        out.dos_offset[*it] = std::uint32_t(span.offset + out.dos.size());
        // The last one fills what is left of DOS's table.
        const std::size_t n = next != starts.end() ? *next - *it : span.size - std::min<std::size_t>(span.size, out.dos.size());
        const auto script = bytes_at(hunk, *it, n);
        out.dos.insert(out.dos.end(), script.begin(), script.end());
        if (out.dos.size() % 2) out.dos.push_back(0);
        if (out.dos.size() > span.size) throw FormatError("the Amiga pose scripts are longer than DOS's");
    }
    return out;
}

// The table in DOS's form, before its edits.
std::vector<std::uint8_t> dos_form(const AmigaTable& t, std::span<const std::uint8_t> hunk) {
    const ExeSpan& span = dos_span(t.id);
    std::vector<std::uint8_t> out;
    const auto reversed = [&](std::size_t n) {
        const auto b = bytes_at(hunk, t.offset, span.size);
        for (std::size_t i = 0; i + n <= b.size(); i += n)
            for (std::size_t k = n; k-- > 0;) out.push_back(b[i + k]);
    };
    switch (t.form) {
    case AmigaTable::Form::Bytes: {
        const auto b = bytes_at(hunk, t.offset, span.size);
        out.assign(b.begin(), b.end());
        break;
    }
    case AmigaTable::Form::Words: reversed(2); break;
    case AmigaTable::Form::Longs: reversed(4); break;
    case AmigaTable::Form::Weapons:
        for (std::size_t at = t.offset; out.size() < span.size; at += 18) {
            const auto record = bytes_at(hunk, at, 17);
            out.insert(out.end(), record.begin(), record.end());
        }
        break;
    case AmigaTable::Form::TemplatePointers: {
        const ExeSpan& templates = dos_span("rules.mech_templates");
        const std::uint32_t first = amiga_table("rules.mech_templates").offset;
        for (std::size_t i = 0; i < pointer_count(t); ++i)
            put_far(out, templates.segment, templates.offset + long_at(hunk, t.offset + 4 * i) - first);
        break;
    }
    case AmigaTable::Form::PiecePointers:
        for (std::size_t i = 0; i < pointer_count(t); ++i) {
            const std::uint32_t to = long_at(hunk, t.offset + 4 * i);
            const ExeSpan* piece = nullptr;
            for (const std::string_view id : {"tables.frame_pieces_0", "tables.frame_pieces_1", "tables.frame_pieces_2"})
                if (amiga_table(id).offset == to) piece = &dos_span(id);
            if (!piece) throw FormatError("an Amiga frame style points at no piece table");
            put_far(out, piece->segment, piece->offset);
        }
        break;
    case AmigaTable::Form::PosePointers: {
        const PoseScripts scripts = pose_scripts(hunk);
        for (std::size_t i = 0; i < pointer_count(t); ++i)
            put_far(out, dos_span("tables.pose_scripts").segment, scripts.dos_offset.at(long_at(hunk, t.offset + 4 * i)));
        break;
    }
    case AmigaTable::Form::Menus:
        reversed(2);
        for (std::size_t m = 0; m + 16 <= out.size(); m += 16)
            for (const std::size_t word : {std::size_t(4), std::size_t(5)})  // colour, highlight
                out[m + 2 * word] = dos_colour(out[m + 2 * word]);
        break;
    case AmigaTable::Form::OverheadColours: {
        const std::size_t third = span.size / 3;
        const auto b = bytes_at(hunk, t.offset, 2 * third);
        for (const std::uint8_t c : b) out.push_back(dos_colour(c));
        // The Amiga has one table for both cache levels (0:bac0 swaps it in on either).
        for (std::size_t i = third; i < 2 * third; ++i) out.push_back(dos_colour(b[i]));
        break;
    }
    case AmigaTable::Form::PoseScripts: out = pose_scripts(hunk).dos; break;
    }
    return out;
}

std::string hunk_address(unsigned hunk, unsigned offset) {
    char address[16];
    std::snprintf(address, sizeof address, "%u:%04x", hunk, offset);
    return address;
}

// A text from `offset` in `hunk`, read as DOS's printer reads it, its bytes as stored.
std::string text_at(std::span<const std::uint8_t> hunk, std::uint32_t offset) {
    if (offset >= hunk.size()) throw FormatError("an Amiga text lies past its hunk's data");
    return OriginalExe::text_of(hunk.subspan(offset));
}

}  // namespace

AmigaProgramData::AmigaProgramData(std::span<const std::uint8_t> file, std::span<const std::uint8_t> endmech)
    : sha256_(sha256_hex(file)) {
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
        renumber_text_colours(bytes);
        const std::string_view expected = t.sha256.empty() ? dos_span(t.id).sha256 : t.sha256;
        if (sha256_hex(bytes).substr(0, 16) != expected) {
            if (differing++ < 5) differ += (differ.empty() ? "" : ", ") + std::string(t.id);
            continue;
        }
        addresses_.emplace(t.id, hunk_address(t.hunk, t.offset));
        pieces_.emplace(t.id, std::move(bytes));
    }
    for (const AmigaTable& t : amiga_tables()) {
        std::vector<std::uint8_t> bytes = dos_form(t, program.hunk(t.hunk));
        for (std::size_t i = 0; i < t.edit_count; ++i) {
            std::uint8_t& b = bytes.at(t.edits[i].at);
            b = t.edits[i].flip ? std::uint8_t(b ^ t.edits[i].value) : t.edits[i].value;
        }
        const std::string_view expected = t.sha256.empty() ? dos_span(t.id).sha256 : t.sha256;
        if (sha256_hex(bytes).substr(0, 16) != expected) {
            if (differing++ < 5) differ += (differ.empty() ? "" : ", ") + std::string(t.id);
            continue;
        }
        addresses_.emplace(t.id, hunk_address(t.hunk, t.offset));
        pieces_.emplace(t.id, std::move(bytes));
    }
    for (const AmigaColours& c : amiga_colour_tables()) {
        std::vector<std::uint8_t> words;
        const auto b = bytes_at(program.hunk(c.hunk), c.offset, std::size_t(c.words) * 2);
        words.assign(b.begin(), b.end());
        std::vector<Rgb>& out = colours_[std::string(c.id)];
        if (c.words == 16) {
            const Palette16 palette = amiga_palette(words);
            out.assign(palette.begin(), palette.end());
        } else {
            // DOS's four steps; the Amiga alternates between two.
            for (std::size_t step = 0; step < 4; ++step) {
                const std::size_t at = step % c.words * 2;
                out.push_back(amiga_colour(std::uint16_t(words[at] << 8 | words[at + 1])));
            }
        }
        addresses_.emplace(c.id, hunk_address(c.hunk, c.offset));
    }
    const Palette16 ending = amiga_palette(endmech);
    colours_["tables.endmech_palette"].assign(ending.begin(), ending.end());
    addresses_.emplace("tables.endmech_palette", "ENDMECH.CMP");
    if (differing)
        throw FormatError("the Amiga program's texts or tables differ from version 2.3's (" + std::to_string(differing) +
                          ": " + differ + (differing > 5 ? ", ..." : "") + ")");
}

std::span<const std::uint8_t> AmigaProgramData::get(std::string_view name) const {
    const auto it = pieces_.find(name);
    if (it == pieces_.end()) throw FormatError("the Amiga copy has no piece " + std::string(name));
    return it->second;
}

std::string AmigaProgramData::text(std::string_view name) const { return OriginalExe::text_of(get(name)); }

std::vector<Rgb> AmigaProgramData::colours(std::string_view name) const {
    const auto it = colours_.find(name);
    if (it == colours_.end()) throw FormatError("the Amiga copy has no colours " + std::string(name));
    return it->second;
}

std::string AmigaProgramData::address(std::string_view name) const {
    const auto it = addresses_.find(name);
    if (it == addresses_.end()) throw FormatError("the Amiga copy has no piece " + std::string(name));
    return it->second;
}

}  // namespace chr::formats
