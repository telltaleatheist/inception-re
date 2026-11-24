// The places' scripts: the .BLD files (docs/formats/bld.md, docs/scripts.md).
//
// A .BLD file is a 16-bit length and that many bytes of enciphered script.
// The script is bytecode: an opcode byte (E4-FF) and its operands, with the
// place's text inline after the print opcode. This reads and deciphers a
// file and decodes its instructions; the interpreter is game/place.cpp.
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

#include "formats/tables.h"

namespace chr::formats {

// The scripts by place number: BTECH.EXE DGROUP:4ec2, far pointers to the
// file names (the entrance code turns a map's entrance into a place number).
inline constexpr std::array<std::string_view, 26> kBldFiles = {
    "TRAINING", "CITADEL", "COMSTAR",  "WEAPON",  "ARMOR",    "REPAIR",   "BARRACKS",
    "LOUNGE",   "GARAGE",  "HOSPITAL", "ARENA",   "PARTY",    "CLOTHES",  "JAIL",
    "MAYOR",    "WEAPON2", "THEATER",  "FROB",    "VIEWDISK", "BARRACK2", "ENTRANCE",
    "HUT",      "ENDMECH", "INSTRUCT", "FINDIT",  "WINSCENE",
};

// BTECH.EXE 17dc:1d30 (Ghidra FUN_17dc_1d30) deciphers the loaded script in
// place: each byte becomes (b + 0x29) XOR 0xE9.
std::uint8_t decipher_bld_byte(std::uint8_t b);

// BTECH.EXE 273d:063b (Ghidra FUN_273d_063b) reads the length word, then
// that many bytes. The result is the deciphered script; offsets in it are
// what the script's jumps use. Throws FormatError for a short file.
std::vector<std::uint8_t> parse_bld(const std::vector<std::uint8_t>& file);
std::vector<std::uint8_t> read_bld(const std::filesystem::path& path);

// The opcodes, by what they do (docs/scripts.md has each in full).
enum class Op : std::uint8_t {
    Sound = 0xE4,
    Earn = 0xE5,
    SetPartyPlace = 0xE6,
    IfPartyX = 0xE7,
    IfRandom = 0xE8,
    NewMember = 0xE9,
    Picture = 0xEA,
    IfFlagD451 = 0xEB,
    IfFlagD450 = 0xEC,
    IfSkill = 0xED,
    Pay = 0xEE,
    IfCBills = 0xEF,
    Cursor = 0xF0,
    AddFlag = 0xF1,
    Pause = 0xF2,
    SwitchFlag = 0xF3,
    SetFlag = 0xF4,
    Special = 0xF5,
    IfYes = 0xF6,
    IfFlag = 0xF7,
    Goto = 0xF8,
    Menu = 0xF9,
    Frame = 0xFA,
    WaitKey = 0xFB,
    Print = 0xFC,
    Clear = 0xFD,
    Window = 0xFE,
    End = 0xFF,
};

// One decoded instruction. Bytes below E4 are not opcodes: the interpreter
// steps over them one at a time (`op` is nullopt, `next` = at + 1).
struct Instruction {
    int at = 0;
    std::optional<Op> op;
    std::uint8_t raw = 0;            // the opcode byte
    std::vector<int> bytes;          // byte operands, unsigned, in order
    std::vector<int> words;          // word operands, unsigned 16-bit, in order
    std::string text;                // Print: the text, without its NUL
    std::vector<int> targets;        // where it may jump (Menu, SwitchFlag: the table)
    std::vector<int> target_at;      // where each target's word is in the script, in the same order
    int text_at = -1;                // Print: where its text starts
    int next = 0;                    // the offset after it
    bool falls_through = true;       // false for End, Goto, Menu, SwitchFlag
};

// The name of an opcode, for listings ("print", "if_flag", ...).
std::string_view op_name(Op op);

// Decodes the instruction at `at`. A Menu's table has the menu's item count
// (`menus`, the menu table: formats/tables.h) of words. A SwitchFlag's table has one word per value of the
// flag, a count the file doesn't hold: it is read up to the first entry's
// code (docs/formats/bld.md). Throws FormatError past the end.
Instruction decode(const std::vector<std::uint8_t>& script, int at, std::span<const MenuRecord> menus);

// Every instruction reachable from offset 0, by offset.
std::map<int, Instruction> walk(const std::vector<std::uint8_t>& script, std::span<const MenuRecord> menus);

// The text ID of the text printed at offset `at` (the Print opcode's offset)
// in place file `file` ("CITADEL"): "bld.citadel.00f3".
std::string bld_text_id(std::string_view file, int at);

}  // namespace chr::formats
