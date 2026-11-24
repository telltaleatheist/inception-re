// The Amiga place scripts (docs/formats/amiga.md, "The place scripts").

#include "formats/amiga_bld.h"

#include <map>

#include "formats/bld.h"
#include "formats/cmp.h"  // FormatError

namespace chr::formats {

namespace {

constexpr int kPlaces = 26;

// The printer's codes that take one argument byte (docs/text.md).
bool takes_argument(std::uint8_t c) { return c == 0x02 || c == 0x06 || c == 0x09 || c == 0x13; }

}  // namespace

// Amiga battletech 0:5fba: seeks to the place's offset, reads a big-endian length and that many
// bytes, and deciphers them as DOS does (formats/bld.h decipher_bld_byte).
std::vector<std::uint8_t> amiga_place_script(std::span<const std::uint8_t> builds,
                                             std::span<const std::uint8_t> offsets, int place) {
    if (place < 0 || place >= kPlaces || offsets.size() < std::size_t(kPlaces) * 4)
        throw FormatError("no Amiga script for place " + std::to_string(place));
    const std::size_t at = std::size_t(place) * 4;
    const std::size_t start = std::size_t(offsets[at]) << 24 | std::size_t(offsets[at + 1]) << 16 |
                              std::size_t(offsets[at + 2]) << 8 | offsets[at + 3];
    if (start + 2 > builds.size()) throw FormatError("builds.bld is shorter than its offsets");
    const std::size_t length = std::size_t(builds[start]) << 8 | builds[start + 1];
    if (start + 2 + length > builds.size()) throw FormatError("a script runs past builds.bld's end");
    std::vector<std::uint8_t> script;
    script.reserve(length);
    for (std::size_t i = 0; i < length; ++i) script.push_back(decipher_bld_byte(builds[start + 2 + i]));
    return script;
}

std::string expand_amiga_text(std::string_view packed, const AmigaTextPairs& pairs) {
    if (pairs.first.size() < 16 || pairs.second.size() < 128) throw FormatError("the Amiga letter-pair tables are short");
    std::string out;
    out.reserve(packed.size() * 2);
    for (std::size_t i = 0; i < packed.size(); ++i) {
        const auto c = static_cast<std::uint8_t>(packed[i]);
        if (takes_argument(c) && i + 1 < packed.size()) {
            out += char(c);
            out += packed[++i];
        } else if (c & 0x80) {
            out += char(pairs.first[(c & 0x7F) >> 3]);
            out += char(pairs.second[c & 0x7F]);
        } else {
            out += char(c);
        }
    }
    return out;
}

std::vector<std::uint8_t> relay_amiga_script(const std::vector<std::uint8_t>& packed, const AmigaTextPairs& pairs,
                                             std::span<const MenuRecord> menus, std::span<const ScriptEdit> edits) {
    std::map<int, Instruction> code;  // every instruction, start to end
    for (int p = 0; p < static_cast<int>(packed.size());) {
        Instruction in = decode(packed, p, menus);
        p = in.next;
        code.emplace(in.at, std::move(in));
    }
    std::map<int, const ScriptEdit*> by_offset;
    for (const ScriptEdit& e : edits) {
        const auto it = code.find(e.at);
        if (it == code.end() || it->second.raw != e.op) throw FormatError("a script edit for an instruction the script doesn't have");
        by_offset[e.at] = &e;
    }
    const int size = static_cast<int>(packed.size());
    std::vector<int> moved(std::size_t(size) + 1, 0);  // each old offset's new one
    std::vector<std::pair<int, int>> jumps;            // (where the word is now, its old target)
    std::vector<std::uint8_t> out;
    out.reserve(packed.size() * 2);
    for (const auto& [p, in] : code) {
        const auto edit = by_offset.find(p);
        const int start = static_cast<int>(out.size());
        if (edit != by_offset.end() && edit->second->kind == ScriptEdit::Kind::Drop) {
            for (int q = p; q < in.next; ++q) moved[std::size_t(q)] = start;
            continue;
        }
        const int copy_end = in.text_at >= 0 ? in.text_at : in.next;
        for (int q = p; q < copy_end; ++q) {
            moved[std::size_t(q)] = start + (q - p);
            out.push_back(packed[std::size_t(q)]);
        }
        if (edit != by_offset.end()) {  // SetByte: the first byte operand follows the opcode
            if (in.bytes.empty()) throw FormatError("a script edit sets a byte an instruction doesn't have");
            out[std::size_t(start) + 1] = static_cast<std::uint8_t>(edit->second->value);
        }
        for (std::size_t k = 0; k < in.target_at.size(); ++k)
            jumps.emplace_back(start + (in.target_at[k] - p), in.targets[k]);
        if (in.text_at >= 0) {
            const std::string text = expand_amiga_text(in.text, pairs);
            for (int q = in.text_at; q < in.next; ++q) moved[std::size_t(q)] = static_cast<int>(out.size());
            out.insert(out.end(), text.begin(), text.end());
            out.push_back(0);
        }
    }
    moved[std::size_t(size)] = static_cast<int>(out.size());
    for (const auto& [word, target] : jumps) {
        if (target < 0 || target > size) throw FormatError("a script jumps outside itself");
        const int to = moved[std::size_t(target)];
        out[std::size_t(word)] = static_cast<std::uint8_t>(to);
        out[std::size_t(word) + 1] = static_cast<std::uint8_t>(to >> 8);
    }
    return out;
}

}  // namespace chr::formats
