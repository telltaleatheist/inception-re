#include "formats/bld.h"

#include <cstdio>

#include "formats/cmp.h"  // FormatError
#include "formats/game_files.h"

namespace chr::formats {

namespace {

// Operand layouts: b a byte, w a word, s NUL-ended text, M a menu's jump
// table, T a switch's jump table.
struct OpInfo {
    std::string_view name;
    std::string_view operands;
};

const OpInfo& info(Op op) {
    static const std::array<OpInfo, 28> kInfo = {{
        {"sound", "b"},            // E4
        {"earn", "w"},             // E5
        {"set_party_place", "ww"}, // E6
        {"if_party_x", "ww"},      // E7
        {"if_random", "bw"},       // E8
        {"new_member", "b"},       // E9
        {"picture", "bb"},         // EA
        {"if_d451", "w"},          // EB
        {"if_d450", "w"},          // EC
        {"if_skill", "bbw"},       // ED
        {"pay", "w"},              // EE
        {"if_cbills", "ww"},       // EF
        {"cursor", "bb"},          // F0
        {"add_flag", "bb"},        // F1
        {"pause", ""},             // F2
        {"switch_flag", "bT"},     // F3
        {"set_flag", "bb"},        // F4
        {"special", "b"},          // F5
        {"if_yes", "w"},           // F6
        {"if_flag", "bw"},         // F7
        {"goto", "w"},             // F8
        {"menu", "bM"},            // F9
        {"frame", "b"},            // FA
        {"wait_key", ""},          // FB
        {"print", "s"},            // FC
        {"clear", ""},             // FD
        {"window", "b"},           // FE
        {"end", ""},               // FF
    }};
    return kInfo[static_cast<int>(op) - 0xE4];
}

int byte_at(const std::vector<std::uint8_t>& s, int at) {
    if (at < 0 || at >= static_cast<int>(s.size())) throw FormatError("script ends inside an instruction");
    return s[at];
}

int word_at(const std::vector<std::uint8_t>& s, int at) { return byte_at(s, at) | byte_at(s, at + 1) << 8; }

}  // namespace

std::uint8_t decipher_bld_byte(std::uint8_t b) { return static_cast<std::uint8_t>((b + 0x29) ^ 0xE9); }

std::vector<std::uint8_t> parse_bld(const std::vector<std::uint8_t>& file) {
    if (file.size() < 2) throw FormatError("a .BLD file shorter than its length word");
    const std::size_t length = file[0] | file[1] << 8;
    if (file.size() < 2 + length) throw FormatError("a .BLD file shorter than its length word says");
    std::vector<std::uint8_t> script(file.begin() + 2, file.begin() + 2 + static_cast<std::ptrdiff_t>(length));
    for (auto& b : script) b = decipher_bld_byte(b);
    return script;
}

std::vector<std::uint8_t> read_bld(const std::filesystem::path& path) { return parse_bld(read_file(path)); }

std::string_view op_name(Op op) { return info(op).name; }

Instruction decode(const std::vector<std::uint8_t>& script, int at, std::span<const MenuRecord> menus) {
    Instruction in;
    in.at = at;
    in.raw = static_cast<std::uint8_t>(byte_at(script, at));
    in.next = at + 1;
    if (in.raw < 0xE4) return in;
    in.op = static_cast<Op>(in.raw);
    int p = at + 1;
    int last_word_at = -1;
    for (const char k : info(*in.op).operands) {
        switch (k) {
        case 'b':
            in.bytes.push_back(byte_at(script, p++));
            break;
        case 'w':
            in.words.push_back(word_at(script, p));
            last_word_at = p;
            p += 2;
            break;
        case 's': {
            const int start = p;
            in.text_at = start;
            while (byte_at(script, p) != 0) ++p;
            in.text.assign(script.begin() + start, script.begin() + p);
            ++p;
            break;
        }
        case 'M': {
            const int menu = static_cast<std::int8_t>(in.bytes.back());
            if (menu < 0 || menu >= static_cast<int>(menus.size()))
                throw FormatError("a script asks for menu " + std::to_string(menu));
            for (int i = 0; i < menus[std::size_t(menu)].items; ++i, p += 2) {
                in.targets.push_back(word_at(script, p));
                in.target_at.push_back(p);
            }
            break;
        }
        case 'T': {
            // One entry per value of the flag. The file doesn't say how many;
            // in every original script the code for the first value that
            // jumps forward starts right after the table.
            int end = -1;
            do {
                const int target = word_at(script, p);
                in.target_at.push_back(p);
                p += 2;
                in.targets.push_back(target);
                if (target >= p && (end < 0 || target < end)) end = target;
            } while (end < 0 || p < end);
            break;
        }
        }
    }
    in.next = p;
    switch (*in.op) {
    case Op::End:
    case Op::Menu:
    case Op::SwitchFlag:
        in.falls_through = false;
        break;
    case Op::Goto:
        in.targets.push_back(in.words.back());
        in.target_at.push_back(last_word_at);
        in.falls_through = false;
        break;
    case Op::IfPartyX:
    case Op::IfRandom:
    case Op::IfFlagD451:
    case Op::IfFlagD450:
    case Op::IfSkill:
    case Op::IfCBills:
    case Op::IfYes:
    case Op::IfFlag:
        in.targets.push_back(in.words.back());
        in.target_at.push_back(last_word_at);
        break;
    default:
        break;
    }
    return in;
}

std::map<int, Instruction> walk(const std::vector<std::uint8_t>& script, std::span<const MenuRecord> menus) {
    std::map<int, Instruction> seen;
    std::vector<int> todo{0};
    const int size = static_cast<int>(script.size());
    while (!todo.empty()) {
        int at = todo.back();
        todo.pop_back();
        while (at < size && !seen.count(at)) {
            Instruction in = decode(script, at, menus);
            for (const int t : in.targets)
                if (t < size) todo.push_back(t);
            const bool on = in.falls_through;
            at = in.next;
            seen.emplace(in.at, std::move(in));
            if (!on) break;
        }
    }
    return seen;
}

std::string bld_text_id(std::string_view file, int at) {
    std::string id = "bld.";
    for (const char c : file) id += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    char offset[8];
    std::snprintf(offset, sizeof offset, ".%04x", at);
    return id + offset;
}

}  // namespace chr::formats
