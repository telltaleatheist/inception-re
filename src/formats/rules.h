// The rule tables built into BTECH.EXE: the 'Mech templates, the weapon
// table and the small combat tables (docs/formats/mech.md). The importer
// reads them from the player's program once and writes them as plain JSON
// files with named fields in the data folder:
//
//   rules/mechs.json     the eight 'Mech templates
//   rules/weapons.json   the 33 weapons (people's, 'Mechs', the Kick)
//   rules/combat.json    hit locations, clusters, facing sides, the target
//                        movement modifier, difficulty, personal armour
//   rules/quiz.json      the training center quiz's ten parts: where each
//                        is on BTSTATS.CMP and its menu item
//   rules/shops.json     the shops' prices
//
// The repo has the layouts and addresses only, never the values. What the
// player reads as a 'Mech's or a weapon's name is a text (mech.<id>,
// weapon.<n> in text/english.json), so a translation can rename them. A
// template's own name bytes are kept in mechs.json as well, as stored: they
// are part of the record (the game state), not what is shown.
//
// In memory a template or a weapon stays the original's bytes: the game
// state is those records (a fight copies a template into a 'Mech record,
// and some of the original's bugs read records as bytes), and a byte-exact
// record is what the reference rig compares.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace chr::formats {

class OriginalExe;

inline constexpr int kMechRecordSize = 0x7d;
inline constexpr int kWeaponRecordSize = 0x11;
inline constexpr int kMechNameSize = 0x10;    // record bytes 00-0f
inline constexpr int kWeaponNameSize = 0x0b;  // record bytes 00-0a

using MechRecord = std::array<std::uint8_t, kMechRecordSize>;
using WeaponRecord = std::array<std::uint8_t, kWeaponRecordSize>;

struct MechTemplate {
    std::string id;       // "locust", ...; the shown name is the text mech.<id>
    MechRecord record{};  // the template as stored, name bytes included
};

struct CombatTables {
    // By side (0 front, 1 and 3 the sides, 2 rear) and 2d6 - 2: a location
    // index 0-10 (armour bytes 0x11 + index: LA LT LL H CT RA RT RL, then the
    // rear LT CT RT).
    std::array<std::array<std::uint8_t, 11>, 4> hit_locations{};
    // By 2d6 - 2 and cluster column - 2 (weapon byte +0x0c, 2-8): how many
    // missiles hit.
    std::array<std::array<std::uint8_t, 7>, 11> clusters{};
    // By facing difference + 7 (-7..7): the side that is hit.
    std::array<std::uint8_t, 15> facing_sides{};
    // By the target's steps so far this round (0-12): added to the to-hit.
    std::array<std::uint8_t, 13> target_movement{};
    // By difficulty (flag 03, clamped to 6): armour taken off each location.
    std::array<std::uint8_t, 7> difficulty_armour{};
    // By personal armour type 0-5: its points when new.
    std::array<std::uint8_t, 6> personal_armour{};
};

// The training center's quiz (1c67:08a8, docs/combat.md 7): ten parts
// pictured on BTSTATS.CMP. Each has a point on the 320x200 screen, which
// the quiz boxes, and its answer, an item of menu 0x25 (the part names
// quiz.part.<n>).
struct QuizPart {
    int x = 0, y = 0;
    int answer = 0;
};
inline constexpr int kQuizParts = 10;
inline constexpr int kQuizPartNames = 20;
using QuizTables = std::array<QuizPart, kQuizParts>;

// The shops' prices (docs/scripts.md, "The shops"), in C-Bills.
inline constexpr int kShopWeapons = 11;
struct ShopTables {
    std::array<std::uint32_t, 6> armour{};        // by armour type; entry 0 is never read
    std::array<std::uint8_t, 6> armour_point{};   // by armour type - 1
    std::array<std::uint32_t, kShopWeapons> weapon{};  // by weapon number - 1
};

struct Rules {
    std::vector<MechTemplate> mechs;
    // By weapon number. The name bytes (00-0a) are left 0: the name is the
    // text weapon.<n>.
    std::vector<WeaponRecord> weapons;
    CombatTables combat;
    QuizTables quiz{};
    ShopTables shops{};
};

// The template ids in the program's order (37e8:02f0 + 0x7d * i).
std::span<const std::string_view> mech_template_ids();
std::string mech_text_id(std::string_view id);
std::string weapon_text_id(int number);

// Reads the tables from the program (the importer only).
Rules rules_from_original(const OriginalExe& exe);
// The names of the templates and weapons, as texts: (id, address, bytes).
struct NamedText {
    std::string id, address, bytes;
};
std::vector<NamedText> rules_texts_from_original(const OriginalExe& exe);

// The files' contents, and reading them back. Throws FormatError
// (formats/cmp.h) or JsonError for a file that is not of this shape.
struct RulesFiles {
    std::string mechs, weapons, combat, quiz, shops;
};
RulesFiles write_rules_files(const Rules& rules, int import_version);
Rules read_rules_files(const RulesFiles& files);
Rules read_rules(const std::filesystem::path& data_dir);

std::filesystem::path rules_dir(const std::filesystem::path& data_dir);
inline constexpr std::array<std::string_view, 5> kRulesFiles = {"mechs.json", "weapons.json", "combat.json",
                                                                "quiz.json", "shops.json"};

}  // namespace chr::formats
