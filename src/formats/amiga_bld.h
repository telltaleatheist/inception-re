// The Amiga version's place scripts, `builds.bld` (docs/formats/amiga.md, "The place scripts"):
// the 26 DOS scripts one after another, each enciphered as in DOS, with its text packed in letter
// pairs. This finds a place's script and lays it out again in DOS form, text expanded and every
// jump moved to match, so the game and its text IDs (bld.<file>.<offset>) see DOS's script.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "formats/tables.h"

namespace chr::formats {

// The printer's letter-pair tables (amiga_program.h: text.pair_first, text.pair_second).
struct AmigaTextPairs {
    std::span<const std::uint8_t> first;   // 16 letters
    std::span<const std::uint8_t> second;  // 128 letters
};

// Place `place`'s script (0-25, the DOS place numbers: formats/bld.h kBldFiles), from builds.bld
// and the program's bld.offsets: deciphered, in the Amiga's own form. Throws FormatError.
std::vector<std::uint8_t> amiga_place_script(std::span<const std::uint8_t> builds,
                                             std::span<const std::uint8_t> offsets, int place);

// A text with its letter pairs written out. Control codes 2, 6, 9 and 0x13 keep their argument
// byte as it is (the Amiga printer, 0:a81e, reads it raw).
std::string expand_amiga_text(std::string_view packed, const AmigaTextPairs& pairs);

// A change at one instruction while laying a script out: the instruction at offset `at` of the
// Amiga script, when it has opcode `op`. Drop leaves it out; SetByte gives its first byte operand
// `value`; InsertSound puts a sound instruction playing effect `value` just before it (several at
// one instruction go in the order given), and jumps to the instruction land on the first.
struct ScriptEdit {
    int at;
    std::uint8_t op;
    enum class Kind { Drop, SetByte, InsertSound } kind = Kind::Drop;
    int value = 0;
};

// The edits that make the Amiga 2.3 copy's script for `place` play as DOS 1.03's (docs/versions.md,
// "Amiga rules"), always applied. Empty for most places.
std::span<const ScriptEdit> amiga_dos_edits(int place);

// The script in DOS form: decoded from its start to its end, one instruction after another (as
// every script decodes, its unreached parts too), each copied with its text expanded, and every
// jump moved to where its target went. An edit whose instruction isn't there throws.
std::vector<std::uint8_t> relay_amiga_script(const std::vector<std::uint8_t>& packed, const AmigaTextPairs& pairs,
                                             std::span<const MenuRecord> menus,
                                             std::span<const ScriptEdit> edits = {});

}  // namespace chr::formats
