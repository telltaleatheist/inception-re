// The rule tables: imported from the player's BTECH.EXE, written as JSON and
// read back, they must give the program's bytes exactly. No values here:
// the repo never holds them (docs/rules.md).
#include <algorithm>
#include <filesystem>

#include "formats/exe_data.h"
#include "formats/game_files.h"
#include "formats/import.h"
#include "formats/rules.h"
#include "test.h"

using namespace chr;

TEST(rules_round_trip_to_the_programs_bytes) {
    const auto dir = chrtest::game_dir();
    if (dir.empty()) {
        chrtest::skip("no game folder (set CHR_GAME_DIR)");
        return;
    }
    const auto exe = formats::OriginalExe::load(dir);
    const auto rules = formats::rules_from_original(exe);
    const auto back = formats::read_rules_files(formats::write_rules_files(rules, formats::kImportVersion));

    const auto mechs = exe.get("rules.mech_templates");
    CHECK(back.mechs.size() == 8);
    for (std::size_t i = 0; i < back.mechs.size(); ++i) {
        CHECK(back.mechs[i].id == formats::mech_template_ids()[i]);
        CHECK(std::equal(back.mechs[i].record.begin(), back.mechs[i].record.end(),
                         mechs.begin() + std::ptrdiff_t(i * formats::kMechRecordSize)));
    }
    const auto weapons = exe.get("rules.weapons");
    CHECK(back.weapons.size() == 0x21);
    for (std::size_t i = 0; i < back.weapons.size(); ++i)
        for (int b = formats::kWeaponNameSize; b < formats::kWeaponRecordSize; ++b)
            CHECK(back.weapons[i][std::size_t(b)] == weapons[i * formats::kWeaponRecordSize + std::size_t(b)]);

    // The combat tables, against the bytes as the program indexes them.
    const auto& c = back.combat;
    const auto hit = exe.get("rules.hit_locations");
    for (int s = 0; s < 4; ++s)
        for (int r = 0; r < 11; ++r) {
            CHECK(c.hit_locations[s][r] + 0x11 == hit[std::size_t(s * 11 + r)]);
            CHECK(c.hit_locations[s][r] <= 10);
        }
    const auto clusters = exe.get("rules.clusters");
    for (int r = 0; r < 11; ++r)
        for (int col = 0; col < 7; ++col) CHECK(c.clusters[r][col] == clusters[std::size_t(r * 7 + col)]);
    const auto same = [&](const auto& got, const char* name) {
        const auto b = exe.get(name);
        CHECK(std::equal(got.begin(), got.end(), b.begin()));
    };
    same(c.facing_sides, "rules.facing_sides");
    same(c.target_movement, "rules.target_movement");
    same(c.difficulty_armour, "rules.difficulty_armour");
    same(c.personal_armour, "rules.personal_armour");

    // The shops' prices: 32-bit entries little-endian, the point prices bytes.
    const auto dwords = [&](const auto& got, const char* name) {
        const auto b = exe.get(name);
        CHECK(b.size() == got.size() * 4);
        for (std::size_t i = 0; i < got.size(); ++i)
            CHECK(got[i] == (std::uint32_t(b[4 * i]) | std::uint32_t(b[4 * i + 1]) << 8 |
                             std::uint32_t(b[4 * i + 2]) << 16 | std::uint32_t(b[4 * i + 3]) << 24));
    };
    dwords(back.shops.armour, "rules.armour_prices");
    dwords(back.shops.weapon, "rules.weapon_prices");
    same(back.shops.armour_point, "rules.armour_point_prices");

    // The quiz: ten parts, each answer one of menu 0x25's 20 names.
    const auto word = [&](const char* name, int i) {
        const auto b = exe.get(name);
        return int(std::int16_t(b[std::size_t(2 * i)] | b[std::size_t(2 * i + 1)] << 8));
    };
    for (int i = 0; i < formats::kQuizParts; ++i) {
        const auto& q = back.quiz[std::size_t(i)];
        CHECK(q.x == word("rules.quiz_part_x", i) && q.y == word("rules.quiz_part_y", i));
        CHECK(q.answer == word("rules.quiz_answers", i));
        CHECK(q.answer >= 0 && q.answer < formats::kQuizPartNames);
    }
}

TEST(import_writes_the_rules_and_names) {
    const auto dir = chrtest::game_dir();
    if (dir.empty()) {
        chrtest::skip("no game folder (set CHR_GAME_DIR)");
        return;
    }
    const auto data = std::filesystem::path(CHR_TEST_SCRATCH) / "import-rules";
    std::filesystem::remove_all(data);
    formats::import_original(dir, data);
    const auto rules = formats::read_rules(data);
    CHECK(rules.mechs.size() == 8 && rules.weapons.size() == 0x21);
    const auto bytes = formats::read_file(formats::imported_text_path(data));
    const std::string json(bytes.begin(), bytes.end());
    for (const auto id : formats::mech_template_ids())
        CHECK(json.find("\"id\": \"" + formats::mech_text_id(id) + "\"") != std::string::npos);
    CHECK(json.find("\"id\": \"weapon.32\", \"address\": \"46db:30f8\"") != std::string::npos);
}

// Every text in the import table ends inside its span: the span holds the
// whole text and the byte that ends it, so none is cut short.
TEST(every_text_span_holds_its_text) {
    const auto dir = chrtest::game_dir();
    if (dir.empty()) {
        chrtest::skip("no game folder (set CHR_GAME_DIR)");
        return;
    }
    const auto exe = formats::OriginalExe::load(dir);
    for (const auto& span : formats::OriginalExe::table()) {
        if (span.kind != formats::ExeSpan::Kind::Text) continue;
        const auto text = exe.text(span.name);
        const auto bytes = exe.get(span.name);
        const bool ends = text.size() < bytes.size() ? bytes[text.size()] == 0 : (bytes.back() & 0x80) != 0;
        if (!ends) chrtest::fail(__FILE__, __LINE__, std::string(span.name) + " is cut short");
    }
}
