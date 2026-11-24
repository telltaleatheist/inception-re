#include "formats/rules.h"

#include <algorithm>
#include <cstdio>
#include <type_traits>

#include "formats/cmp.h"  // FormatError
#include "formats/exe_data.h"
#include "formats/game_files.h"
#include "formats/json.h"

namespace chr::formats {

namespace fs = std::filesystem;

namespace {

constexpr std::array<std::string_view, 8> kMechIds = {
    "locust", "wasp", "stinger", "commando", "chameleon", "jenner", "spectator", "urbanmech"};
constexpr std::uint16_t kMechSegment = 0x37e8;
constexpr std::uint16_t kMechOffset = 0x02f0;
constexpr std::uint16_t kWeaponSegment = 0x46db;
constexpr std::uint16_t kWeaponOffset = 0x2ed8;
constexpr int kWeaponCount = 0x21;

// The 'Mech record after its name, field by field (docs/formats/mech.md).
// A count of 1 is written as a number, more as an array.
struct Field {
    std::string_view key;
    int offset;
    int count;
};
constexpr std::array<Field, 21> kMechFields = {{
    {"tonnage", 0x10, 1},
    {"armour", 0x11, 11},  // LA LT LL H CT RA RT RL, rear LT CT RT
    {"structure", 0x1c, 8},  // LA LT LL H CT RA RT RL
    {"actuators", 0x24, 2},  // left, right: high nibble arm, low nibble leg
    {"heat_sinks", 0x26, 1},  // in the engine, not in slots
    {"ammo", 0x27, 10},  // per weapon in slot order; 255 = needs none
    {"walk_mp", 0x31, 1},
    {"jump_mp", 0x32, 1},
    {"slots", 0x33, 35},  // item codes, weapon number + 1; 0x80 = destroyed
    {"max_armour", 0x56, 11},
    {"max_structure", 0x61, 8},
    {"start_actuators", 0x69, 2},
    {"start_ammo", 0x6b, 10},
    {"engine_hits", 0x75, 1},
    {"gyro_hits", 0x76, 1},
    {"sensor_hits", 0x77, 1},
    {"byte_78", 0x78, 1},  // meaning not known
    {"pilot", 0x79, 1},
    {"second_pilot", 0x7a, 1},
    {"model", 0x7b, 1},
    {"byte_7c", 0x7c, 1},
}};

std::string hex_address(std::uint16_t segment, int offset) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04x:%04x", segment, offset);
    return buf;
}

std::string number_list(std::span<const std::uint8_t> bytes) {
    std::string s = "[";
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i) s += ", ";
        s += std::to_string(bytes[i]);
    }
    return s + "]";
}

std::string header(int import_version, std::string_view source, std::string_view what) {
    return "{\n  \"import_version\": " + std::to_string(import_version) + ",\n  \"source\": " +
           json_quote(source) + ",\n  \"what\": " +
           json_quote(what) + ",\n";
}

const Json& member(const Json& object, std::string_view key) {
    const Json* j = object.find(key);
    if (!j) throw FormatError("rules: no \"" + std::string(key) + "\"");
    return *j;
}

std::uint8_t byte_of(const Json& j) {
    const double v = j.number();
    if (v < 0 || v > 255 || v != double(int(v))) throw FormatError("rules: not a byte");
    return std::uint8_t(v);
}

template <std::size_t N>
void fill(std::array<std::uint8_t, N>& out, const Json& j) {
    const auto& a = j.array();
    if (a.size() != N) throw FormatError("rules: a table has the wrong length");
    for (std::size_t i = 0; i < N; ++i) out[i] = byte_of(a[i]);
}

std::span<const std::uint8_t> table(const ProgramData& exe, std::string_view name) { return exe.get(name); }

}  // namespace

std::span<const std::string_view> mech_template_ids() { return kMechIds; }
std::string mech_text_id(std::string_view id) { return "mech." + std::string(id); }
std::string weapon_text_id(int number) { return "weapon." + std::to_string(number); }
fs::path rules_dir(const fs::path& data_dir) { return data_dir / "rules"; }

Rules rules_from_original(const ProgramData& exe) {
    Rules rules;
    const auto mechs = table(exe, "rules.mech_templates");
    for (std::size_t i = 0; i < kMechIds.size(); ++i) {
        MechTemplate t;
        t.id = kMechIds[i];
        std::copy_n(mechs.begin() + std::ptrdiff_t(i * kMechRecordSize), kMechRecordSize, t.record.begin());
        rules.mechs.push_back(t);
    }
    const auto weapons = table(exe, "rules.weapons");
    for (int i = 0; i < kWeaponCount; ++i) {
        WeaponRecord w{};
        std::copy_n(weapons.begin() + i * kWeaponRecordSize + kWeaponNameSize,
                    kWeaponRecordSize - kWeaponNameSize, w.begin() + kWeaponNameSize);
        rules.weapons.push_back(w);
    }
    auto& c = rules.combat;
    const auto hit = table(exe, "rules.hit_locations");
    // The program stores armour byte numbers 0x11-0x1b; the file has 0-10.
    for (int side = 0; side < 4; ++side)
        for (int r = 0; r < 11; ++r) c.hit_locations[side][r] = std::uint8_t(hit[side * 11 + r] - 0x11);
    const auto clusters = table(exe, "rules.clusters");
    for (int r = 0; r < 11; ++r)
        for (int col = 0; col < 7; ++col) c.clusters[r][col] = clusters[r * 7 + col];
    const auto copy = [&](auto& out, std::string_view name) {
        const auto b = table(exe, name);
        std::copy_n(b.begin(), out.size(), out.begin());
    };
    copy(c.facing_sides, "rules.facing_sides");
    copy(c.target_movement, "rules.target_movement");
    copy(c.difficulty_armour, "rules.difficulty_armour");
    copy(c.personal_armour, "rules.personal_armour");
    const auto dword = [&](std::span<const std::uint8_t> b, std::size_t i) {
        return std::uint32_t(b[4 * i]) | std::uint32_t(b[4 * i + 1]) << 8 | std::uint32_t(b[4 * i + 2]) << 16 |
               std::uint32_t(b[4 * i + 3]) << 24;
    };
    const auto armour_prices = table(exe, "rules.armour_prices");
    for (std::size_t i = 0; i < rules.shops.armour.size(); ++i) rules.shops.armour[i] = dword(armour_prices, i);
    copy(rules.shops.armour_point, "rules.armour_point_prices");
    const auto weapon_prices = table(exe, "rules.weapon_prices");
    for (std::size_t i = 0; i < rules.shops.weapon.size(); ++i) rules.shops.weapon[i] = dword(weapon_prices, i);
    // Three tables of ten words: x, y, answer.
    const auto word = [&](std::string_view name, int i) {
        const auto b = table(exe, name);
        return int(std::int16_t(b[std::size_t(2 * i)] | b[std::size_t(2 * i + 1)] << 8));
    };
    for (int i = 0; i < kQuizParts; ++i)
        rules.quiz[std::size_t(i)] = {word("rules.quiz_part_x", i), word("rules.quiz_part_y", i),
                                      word("rules.quiz_answers", i)};
    return rules;
}

std::vector<NamedText> rules_texts_from_original(const ProgramData& exe) {
    std::vector<NamedText> out;
    const auto mechs = table(exe, "rules.mech_templates");
    for (std::size_t i = 0; i < kMechIds.size(); ++i) {
        const auto name = mechs.subspan(i * kMechRecordSize, kMechNameSize);
        out.push_back({mech_text_id(kMechIds[i]), hex_address(kMechSegment, kMechOffset + int(i) * kMechRecordSize),
                       OriginalExe::text_of(name)});
    }
    const auto weapons = table(exe, "rules.weapons");
    for (int i = 0; i < kWeaponCount; ++i) {
        const auto name = weapons.subspan(std::size_t(i * kWeaponRecordSize), kWeaponNameSize);
        out.push_back({weapon_text_id(i), hex_address(kWeaponSegment, kWeaponOffset + i * kWeaponRecordSize),
                       OriginalExe::text_of(name)});
    }
    return out;
}

RulesFiles write_rules_files(const Rules& rules, int import_version, std::string_view source) {
    RulesFiles f;

    f.mechs = header(import_version, source, "the 'Mech templates (docs/formats/mech.md)") + "  \"mechs\": [\n";
    for (std::size_t i = 0; i < rules.mechs.size(); ++i) {
        const auto& t = rules.mechs[i];
        const auto& r = t.record;
        f.mechs += std::string(i ? ",\n" : "") + "    {\"id\": " + json_quote(t.id) + ", \"address\": " +
                   json_quote(hex_address(kMechSegment, kMechOffset + int(i) * kMechRecordSize)) +
                   ", \"name_id\": " + json_quote(mech_text_id(t.id)) + ",\n     \"record_name\": " +
                   json_quote(std::string_view(reinterpret_cast<const char*>(r.data()), kMechNameSize));
        for (const auto& field : kMechFields) {
            f.mechs += ",\n     " + json_quote(field.key) + ": ";
            const auto bytes = std::span(r).subspan(std::size_t(field.offset), std::size_t(field.count));
            f.mechs += field.count == 1 ? std::to_string(bytes[0]) : number_list(bytes);
        }
        f.mechs += "}";
    }
    f.mechs += "\n  ]\n}\n";

    f.weapons = header(import_version, source, "the weapon table (docs/formats/mech.md)") + "  \"weapons\": [\n";
    for (std::size_t i = 0; i < rules.weapons.size(); ++i) {
        const auto& w = rules.weapons[i];
        const bool personal = w[0x0c] & 0x80;
        std::string e = "    {\"number\": " + std::to_string(i) + ", \"address\": " +
                        json_quote(hex_address(kWeaponSegment, kWeaponOffset + int(i) * kWeaponRecordSize)) +
                        ", \"name_id\": " + json_quote(weapon_text_id(int(i))) +
                        ", \"personal\": " + (personal ? "true" : "false");
        // People's weapons roll their damage: a fixed part and a number of d6.
        if (personal)
            e += ", \"damage_fixed\": " + std::to_string(w[0x0b] & 0x0f) +
                 ", \"damage_d6\": " + std::to_string(w[0x0b] >> 4);
        else
            e += ", \"damage\": " + std::to_string(w[0x0b]);
        e += ", \"shots\": " + std::to_string(w[0x0c] & 0x7f) +  // or the cluster column 2-8
             ", \"heat\": " + std::to_string(w[0x0d] & 0x0f) +
             ", \"min_range\": " + std::to_string(w[0x0d] >> 4) +
             ", \"short\": " + std::to_string(w[0x0e] >> 5) +
             ", \"medium\": " + std::to_string(w[0x0e] & 0x1f) +
             ", \"long\": " + std::to_string(w[0x0f]) +
             ", \"skill\": " + std::to_string(w[0x10]) + "}";
        f.weapons += (i ? ",\n" : "") + e;
    }
    f.weapons += "\n  ]\n}\n";

    const auto& c = rules.combat;
    f.combat = header(import_version, source, "the combat tables (docs/combat.md)");
    f.combat += "  \"hit_locations\": {\"address\": \"46db:2e42\", \"by\": \"side 0-3, then 2d6 - 2\", \"rows\": [";
    for (int s = 0; s < 4; ++s) f.combat += std::string(s ? ", " : "") + number_list(c.hit_locations[s]);
    f.combat += "]},\n  \"clusters\": {\"address\": \"46db:2e6e\", \"by\": \"2d6 - 2, then column - 2\", \"rows\": [";
    for (int r = 0; r < 11; ++r) f.combat += std::string(r ? ", " : "") + number_list(c.clusters[r]);
    f.combat += "]},\n  \"facing_sides\": {\"address\": \"46db:2d0a\", \"by\": \"facing difference + 7\", \"values\": " +
                number_list(c.facing_sides) + "},\n";
    f.combat += "  \"target_movement\": {\"address\": \"46db:2d1a\", \"by\": \"steps so far\", \"values\": " +
                number_list(c.target_movement) + "},\n";
    f.combat += "  \"difficulty_armour\": {\"address\": \"46db:165a\", \"by\": \"difficulty\", \"values\": " +
                number_list(c.difficulty_armour) + "},\n";
    f.combat += "  \"personal_armour\": {\"address\": \"46db:4ddb\", \"by\": \"armour type\", \"values\": " +
                number_list(c.personal_armour) + "}\n}\n";

    f.quiz = header(import_version, source, "the training center quiz's parts (docs/combat.md 7)");
    f.quiz += "  \"address\": {\"x\": \"46db:28c4\", \"y\": \"46db:28d8\", \"answer\": \"46db:28ec\"},\n"
              "  \"parts\": [\n";
    for (int i = 0; i < kQuizParts; ++i) {
        const auto& q = rules.quiz[std::size_t(i)];
        f.quiz += std::string(i ? ",\n" : "") + "    {\"x\": " + std::to_string(q.x) + ", \"y\": " + std::to_string(q.y) +
                  ", \"answer\": " + std::to_string(q.answer) + "}";
    }
    f.quiz += "\n  ]\n}\n";

    const auto& sh = rules.shops;
    const auto numbers = [](const auto& values) {
        std::string out = "[";
        for (std::size_t i = 0; i < values.size(); ++i) out += (i ? ", " : "") + std::to_string(values[i]);
        return out + "]";
    };
    f.shops = header(import_version, source, "the shops' prices in C-Bills (docs/scripts.md, \"The shops\")");
    f.shops += "  \"armour\": {\"address\": \"46db:4f26\", \"by\": \"armour type\", \"values\": " + numbers(sh.armour) +
               "},\n  \"armour_point\": {\"address\": \"46db:4f3e\", \"by\": \"armour type - 1\", \"values\": " +
               numbers(sh.armour_point) +
               "},\n  \"weapon\": {\"address\": \"46db:4f44\", \"by\": \"weapon - 1\", \"values\": " + numbers(sh.weapon) +
               "}\n}\n";
    return f;
}

Rules read_rules_files(const RulesFiles& files) {
    Rules rules;

    const Json mechs = parse_json(files.mechs);
    for (const auto& m : member(mechs, "mechs").array()) {
        MechTemplate t;
        t.id = member(m, "id").string();
        const auto& name = member(m, "record_name").string();
        if (name.size() != kMechNameSize) throw FormatError("rules: a record name is not 16 bytes");
        std::copy(name.begin(), name.end(), t.record.begin());
        for (const auto& field : kMechFields) {
            const auto& j = member(m, field.key);
            if (field.count == 1) {
                t.record[std::size_t(field.offset)] = byte_of(j);
                continue;
            }
            const auto& a = j.array();
            if (int(a.size()) != field.count) throw FormatError("rules: " + std::string(field.key) + " has the wrong length");
            for (int k = 0; k < field.count; ++k) t.record[std::size_t(field.offset + k)] = byte_of(a[std::size_t(k)]);
        }
        rules.mechs.push_back(t);
    }

    const Json weapons = parse_json(files.weapons);
    for (const auto& j : member(weapons, "weapons").array()) {
        WeaponRecord w{};
        const auto nibbles = [&](std::string_view hi, std::string_view lo, int lo_bits) {
            const int h = byte_of(member(j, hi)), l = byte_of(member(j, lo));
            if (l >= (1 << lo_bits) || h >= (1 << (8 - lo_bits))) throw FormatError("rules: a weapon field is too big");
            return std::uint8_t(h << lo_bits | l);
        };
        const Json* personal = j.find("personal");
        const bool is_personal = personal && std::get<bool>(personal->value);
        w[0x0b] = is_personal ? nibbles("damage_d6", "damage_fixed", 4) : byte_of(member(j, "damage"));
        const int shots = byte_of(member(j, "shots"));
        if (shots > 0x7f) throw FormatError("rules: shots is too big");
        w[0x0c] = std::uint8_t(shots | (is_personal ? 0x80 : 0));
        w[0x0d] = nibbles("min_range", "heat", 4);
        w[0x0e] = nibbles("short", "medium", 5);
        w[0x0f] = byte_of(member(j, "long"));
        w[0x10] = byte_of(member(j, "skill"));
        rules.weapons.push_back(w);
    }

    const Json combat = parse_json(files.combat);
    auto& c = rules.combat;
    const auto& hit = member(member(combat, "hit_locations"), "rows").array();
    if (hit.size() != 4) throw FormatError("rules: hit_locations needs 4 rows");
    for (int s = 0; s < 4; ++s) fill(c.hit_locations[s], hit[std::size_t(s)]);
    const auto& clu = member(member(combat, "clusters"), "rows").array();
    if (clu.size() != 11) throw FormatError("rules: clusters needs 11 rows");
    for (int r = 0; r < 11; ++r) fill(c.clusters[r], clu[std::size_t(r)]);
    fill(c.facing_sides, member(member(combat, "facing_sides"), "values"));
    fill(c.target_movement, member(member(combat, "target_movement"), "values"));
    fill(c.difficulty_armour, member(member(combat, "difficulty_armour"), "values"));
    fill(c.personal_armour, member(member(combat, "personal_armour"), "values"));

    const Json quiz = parse_json(files.quiz);
    const auto& parts = member(quiz, "parts").array();
    if (parts.size() != std::size_t(kQuizParts)) throw FormatError("rules: the quiz needs 10 parts");
    for (int i = 0; i < kQuizParts; ++i) {
        const auto& j = parts[std::size_t(i)];
        const auto in = [&](std::string_view key, int lo, int hi) {
            const double v = member(j, key).number();
            if (v < lo || v > hi || v != double(int(v))) throw FormatError("rules: a quiz part's " + std::string(key) + " is out of range");
            return int(v);
        };
        rules.quiz[std::size_t(i)] = {in("x", 0, 319), in("y", 0, 199), in("answer", 0, kQuizPartNames - 1)};
    }

    const Json shops = parse_json(files.shops);
    const auto prices = [&](auto& out, std::string_view key, double most) {
        const auto& a = member(member(shops, key), "values").array();
        if (a.size() != out.size()) throw FormatError("rules: shops' " + std::string(key) + " has the wrong count");
        for (std::size_t i = 0; i < out.size(); ++i) {
            const double v = a[i].number();
            if (v < 0 || v > most || v != double(std::uint32_t(v))) throw FormatError("rules: a shop price is out of range");
            out[i] = static_cast<std::remove_reference_t<decltype(out[i])>>(v);
        }
    };
    prices(rules.shops.armour, "armour", 4294967295.0);
    prices(rules.shops.armour_point, "armour_point", 255);
    prices(rules.shops.weapon, "weapon", 4294967295.0);
    return rules;
}

Rules read_rules(const fs::path& data_dir) {
    const auto read = [&](std::string_view name) {
        const auto bytes = read_file(rules_dir(data_dir) / name);
        return std::string(bytes.begin(), bytes.end());
    };
    return read_rules_files(
        {read(kRulesFiles[0]), read(kRulesFiles[1]), read(kRulesFiles[2]), read(kRulesFiles[3]), read(kRulesFiles[4])});
}

}  // namespace chr::formats
