#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

#include "formats/cmp.h"
#include "formats/font_file.h"
#include "formats/import.h"
#include "formats/json.h"
#include "formats/exe_data.h"
#include "formats/exepack.h"
#include "formats/game_files.h"
#include "formats/game_source.h"
#include "formats/sha256.h"
#include "game/text.h"
#include "game/text_provider.h"
#include "formats/tables.h"
#include "test.h"

using namespace chr;
using namespace chr::game;
using chr::game::lookup;

TEST(sha256_known_values) {
    CHECK(formats::sha256_hex({}) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string abc = "abc";
    CHECK(formats::sha256_hex(std::span(reinterpret_cast<const std::uint8_t*>(abc.data()), 3)) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // Two blocks of padding.
    const std::string long_text = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(formats::sha256_hex(std::span(reinterpret_cast<const std::uint8_t*>(long_text.data()),
                                        long_text.size())) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(window_frame_pieces) {
    const auto* tables = chrtest::tables();
    if (!tables) return chrtest::skip("no imported tables (set CHR_GAME_DIR)");
    TextScreen t(*tables);
    t.select_window(3);  // (1, 13) 11x11
    t.frame(3);
    CHECK(t.cell(0, 12).kind == TextCell::Kind::Frame && t.cell(0, 12).code == 2);   // top-left
    CHECK(t.cell(12, 12).code == 3);                                                 // top-right
    CHECK(t.cell(0, 24).code == 6 && t.cell(12, 24).code == 7);                      // bottom corners
    CHECK(t.cell(5, 12).code == 4 && t.cell(5, 24).code == 4);                       // top, bottom
    CHECK(t.cell(0, 18).code == 5 && t.cell(12, 18).code == 5);                      // sides
    CHECK(t.cell(5, 18).kind == TextCell::Kind::Empty);                              // inside untouched
}

TEST(printer_wraps_words) {
    const auto* tables = chrtest::tables();
    if (!tables) return chrtest::skip("no imported tables (set CHR_GAME_DIR)");
    TextScreen t(*tables);
    t.select_window(7);  // (14, 10) 25x5
    t.clear_window();
    t.print("The water is too deep that way.");
    CHECK(t.row_text(10).substr(14, 25) == "The water is too deep    ");
    CHECK(t.row_text(11).substr(14, 25) == "that way.                ");
}

TEST(printer_newline_and_colours) {
    const auto* tables = chrtest::tables();
    if (!tables) return chrtest::skip("no imported tables (set CHR_GAME_DIR)");
    TextScreen t(*tables);
    t.select_window(7);
    t.clear_window();
    const std::array<std::uint8_t, 8> s = {'A', 0x0d, 0x06, 4, 0x02, 14, 'B', 0};
    t.print(std::span<const std::uint8_t>(s));
    CHECK(t.row_text(10).substr(14, 2) == "A ");
    CHECK(t.cell(14, 11).code == 'B' && t.cell(14, 11).fg == chr::game::TextColour::data(4) && t.cell(14, 11).bg == chr::game::TextColour::data(14));
}

// A menu's bar: on, off with the same call, gone when the cell is painted;
// two menus' bars on one cell are both kept.
TEST(menu_bar_toggles) {
    const auto* tables = chrtest::tables();
    if (!tables) return chrtest::skip("no imported tables (set CHR_GAME_DIR)");
    TextScreen t(*tables);
    t.put_text("Yes", 2, 3, chr::game::TextRole::Text, chr::game::TextRole::Background);
    t.menu_bar(2, 3, 3, 5);
    CHECK(t.cell(2, 3).bars[0] == 6 && t.cell(4, 3).bars[0] == 6 && t.cell(5, 3).bars[0] == 0);
    t.menu_bar(2, 3, 3, 0x28);
    CHECK(t.cell(3, 3).bars[0] == 6 && t.cell(3, 3).bars[1] == 0x29);
    t.menu_bar(2, 3, 3, 5);
    CHECK(t.cell(3, 3).bars[0] == 0 && t.cell(3, 3).bars[1] == 0x29);
    t.put_text("No", 2, 3, chr::game::TextRole::Text, chr::game::TextRole::Background);
    CHECK(t.cell(2, 3).bars[1] == 0 && t.cell(4, 3).bars[1] == 0x29);
}

TEST(printer_scrolls_at_the_bottom) {
    const auto* tables = chrtest::tables();
    if (!tables) return chrtest::skip("no imported tables (set CHR_GAME_DIR)");
    TextScreen t(*tables);
    t.select_window(7);
    t.clear_window();
    t.print("1\r2\r3\r4\r5\r6");
    CHECK(t.row_text(10).substr(14, 1) == "2");
    CHECK(t.row_text(14).substr(14, 1) == "6");
}

TEST(exe_rejects_other_programs) {
    std::vector<std::uint8_t> junk(4096, 0);
    junk[0] = 'M';
    junk[1] = 'Z';
    bool threw = false;
    try {
        formats::OriginalExe exe(junk);
    } catch (const formats::FormatError& e) {
        threw = std::string(e.what()).find("DOS 1.03") != std::string::npos;
    }
    CHECK(threw);
}

TEST(exe_unpacks_like_the_tool_and_has_the_font) {
    const auto dir = chrtest::game_dir();
    if (dir.empty()) {
        chrtest::skip("no game folder (set CHR_GAME_DIR)");
        return;
    }
    const auto path = formats::find_game_file(dir, "BTECH.EXE");
    CHECK(path.has_value());
    if (!path) return;
    const auto unpacked = formats::unexepack(formats::read_file(*path));
    CHECK(unpacked.size() == formats::kDos103Layout.size);
    const auto exe = formats::OriginalExe::load(dir);
    CHECK(exe.get("font").size() == 1024);
    CHECK(exe.text("walk.too_deep") == "The water is too deep that way.");
}

namespace {

// A fresh folder under the build tree's test scratch.
std::filesystem::path scratch(const std::string& name) {
    const std::filesystem::path dir = std::filesystem::path(CHR_TEST_SCRATCH) / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

TEST(import_writes_text_and_font) {
    const auto dir = chrtest::game_dir();
    if (dir.empty()) {
        chrtest::skip("no game folder (set CHR_GAME_DIR)");
        return;
    }
    const auto data = scratch("import");
    CHECK(formats::import_state(data) == formats::ImportState::Missing);
    CHECK(formats::ensure_imported(dir, data));
    CHECK(formats::import_state(data) == formats::ImportState::Current);
    CHECK(!formats::ensure_imported(dir, data));  // nothing to do the second time

    const auto texts = TextTable::load(formats::imported_text_path(data));
    CHECK(lookup(texts, "walk.too_deep") == "The water is too deep that way.");
    CHECK(lookup(texts, "character.0") == "Jason");
    CHECK(lookup(texts, "panel.party_heading") == "BDC");
    CHECK(lookup(texts, "ui.answer_yes_picked").substr(0, 5) == std::string("\r\x06\x00\x02\x0e", 5));
    // Each entry says where in the program it came from.
    const auto bytes = formats::read_file(formats::imported_text_path(data));
    const std::string json(bytes.begin(), bytes.end());
    CHECK(json.find("\"address\": \"46db:0410\"") != std::string::npos);

    const auto font_bytes = formats::read_file(formats::imported_font_path(data));
    const auto font = formats::parse_font_file(std::string(font_bytes.begin(), font_bytes.end()));
    CHECK(font.width == 8 && font.height == 8 && font.glyphs.size() == 128);
    int lit_a = 0, lit_space = 0;
    for (const auto bit : font.glyphs['A']) lit_a += bit;
    for (const auto bit : font.glyphs[' ']) lit_space += bit;
    CHECK(lit_a > 8 && lit_space == 0);

    // A different import version is imported again.
    std::filesystem::remove(data / "import.json");
    {
        std::ofstream(data / "import.json") << "{\"import_version\": 0}";
    }
    CHECK(formats::import_state(data) == formats::ImportState::Outdated);
    CHECK(formats::ensure_imported(dir, data));
    CHECK(formats::import_state(data) == formats::ImportState::Current);

    // So is one from another version's copy.
    CHECK(formats::import_state(data, formats::dos_program_name()) == formats::ImportState::Current);
    CHECK(formats::import_state(data, "another program") == formats::ImportState::Outdated);

    // So is one made with another import table, at the same version.
    const auto manifest_bytes = formats::read_file(data / "import.json");
    std::string manifest(manifest_bytes.begin(), manifest_bytes.end());
    const auto at = manifest.find(formats::import_table_hash());
    CHECK(at != std::string::npos);
    manifest.replace(at, 4, "0000");
    std::filesystem::remove(data / "import.json");
    {
        std::ofstream(data / "import.json") << manifest;
    }
    CHECK(formats::import_state(data) == formats::ImportState::Outdated);
}

namespace {

// The DOS copy as one with no font or speaker tables, but with recorded effects.
class RecordedProgram final : public formats::ProgramData {
public:
    explicit RecordedProgram(std::unique_ptr<const formats::ProgramData> dos) : dos_(std::move(dos)) {}
    std::span<const std::uint8_t> get(std::string_view name) const override {
        if (!has(name)) throw formats::FormatError("not in this copy");
        return dos_->get(name);
    }
    bool has(std::string_view name) const override {
        return name != "font" && name != "tables.sound_lists" && name != "tables.music_note_hz" && dos_->has(name);
    }
    std::string text(std::string_view name) const override { return dos_->text(name); }
    std::vector<formats::Rgb> colours(std::string_view name) const override { return dos_->colours(name); }
    std::string address(std::string_view name) const override { return dos_->address(name); }
    std::string source() const override { return "a recorded copy"; }
    std::string sha256() const override { return dos_->sha256(); }

private:
    std::unique_ptr<const formats::ProgramData> dos_;
};

class RecordedSource final : public formats::GameSource {
public:
    explicit RecordedSource(std::filesystem::path dir) : dos_(std::move(dir)) {}
    std::string describe() const override { return dos_.describe(); }
    std::string program_name() const override { return "a recorded copy"; }
    std::optional<std::vector<std::uint8_t>> script(int file) const override { return dos_.script(file); }
    std::optional<std::vector<std::uint8_t>> map(int number) const override { return dos_.map(number); }
    std::optional<formats::AnmPicture> place_picture(int number) const override { return dos_.place_picture(number); }
    std::optional<formats::CmpPicture> picture(const std::string& name) const override { return dos_.picture(name); }
    std::optional<formats::TileSheet> tile_sheet(const std::string& name) const override { return dos_.tile_sheet(name); }
    std::optional<std::vector<std::uint8_t>> demo() const override { return dos_.demo(); }
    std::optional<std::vector<std::uint8_t>> title_tune() const override { return std::nullopt; }
    std::optional<formats::SoundSample> effect_sound(int n) const override {
        if (n != 2) return std::nullopt;
        return formats::SoundSample{8000, {-128, 0, 127}};
    }
    std::unique_ptr<const formats::ProgramData> program() const override {
        return std::make_unique<const RecordedProgram>(dos_.program());
    }

private:
    formats::DosGameSource dos_;
};

}  // namespace

// A copy with no font and no speaker tables imports without them, its recorded effects as WAV
// files; a DOS import over it brings the font and tables and takes the recordings away.
TEST(import_of_a_copy_with_recorded_sounds) {
    const auto dir = chrtest::game_dir();
    if (dir.empty()) return chrtest::skip("no game folder (set CHR_GAME_DIR)");
    const auto data = scratch("import-recorded");
    const RecordedSource recorded(dir);
    CHECK(formats::ensure_imported(recorded, data));
    CHECK(formats::import_state(data, "a recorded copy") == formats::ImportState::Current);
    CHECK(!std::filesystem::exists(formats::imported_font_path(data)));
    const auto tables = formats::read_tables(data);
    CHECK(tables.sound_lists.front() == 0 && tables.music_note_hz.front() == 0 && tables.walking_keys.front() != 0);
    const auto sounds = formats::imported_sounds_dir(data);
    CHECK(!std::filesystem::exists(sounds / "1.wav"));
    // 44 bytes of header, three samples stored unsigned, one byte of padding.
    const auto wav = formats::read_file(sounds / "2.wav");
    CHECK(wav.size() == 48 && wav[24] == (8000 & 0xff) && wav[25] == (8000 >> 8) && wav[40] == 3);
    CHECK(wav.size() == 48 && wav[44] == 0 && wav[45] == 128 && wav[46] == 255);

    CHECK(formats::ensure_imported(dir, data));
    CHECK(formats::import_state(data, formats::dos_program_name()) == formats::ImportState::Current);
    CHECK(std::filesystem::exists(formats::imported_font_path(data)) && !std::filesystem::exists(sounds));
    CHECK(formats::read_tables(data).sound_lists.front() != 0);
    // A DOS import whose font has gone is missing.
    std::filesystem::remove(formats::imported_font_path(data));
    CHECK(formats::import_state(data, formats::dos_program_name()) == formats::ImportState::Missing);
}

// Our own font has every character the game prints, in the original's 8x8 cells: 32-126, and
// the arrows 1-8.
TEST(own_font_has_every_printed_character) {
    const auto bytes = formats::read_file(CHR_OWN_FONT);
    const auto font = formats::parse_font_file(std::string(bytes.begin(), bytes.end()));
    CHECK(font.width == 8 && font.height == 8);
    for (std::size_t c = 0; c < font.glyphs.size(); ++c) {
        const bool lit = std::find(font.glyphs[c].begin(), font.glyphs[c].end(), 1) != font.glyphs[c].end();
        CHECK(lit == ((c > 32 && c < 127) || (c >= 1 && c <= 8)));
    }
}

TEST(import_table_hash_follows_the_table) {
    const auto table = formats::OriginalExe::table();
    std::vector<formats::ExeSpan> more(table.begin(), table.end());
    const auto before = formats::import_table_hash(more);
    CHECK(before == formats::import_table_hash());
    more.push_back({formats::ExeSpan::Kind::Text, "test.added", 0x46db, 0x0100, 4, "0000000000000000"});
    CHECK(formats::import_table_hash(more) != before);
    // Any field of a piece counts.
    more.pop_back();
    more.front().sha256 = "ffffffffffffffff";
    CHECK(formats::import_table_hash(more) != before);
}

TEST(missing_import_says_how_to_import) {
    const auto data = scratch("no-import");
    const auto no_game = scratch("no-game");
    bool told = false;
    try {
        formats::ensure_imported(no_game, data);
    } catch (const formats::FormatError& e) {
        told = std::string(e.what()).find("chr --import") != std::string::npos;
    }
    CHECK(told);
    CHECK(formats::import_state(data) == formats::ImportState::Missing);
}

TEST(font_file_round_trip) {
    std::vector<std::uint8_t> bits(1024, 0);
    bits[65 * 8] = 0x81;  // 'A', top row: the two outer pixels
    const auto font = formats::font_from_original(bits);
    CHECK(font.glyphs[65][0] == 1 && font.glyphs[65][7] == 1 && font.glyphs[65][1] == 0);
    const auto again = formats::parse_font_file(formats::write_font_file(font));
    CHECK(again.glyphs == font.glyphs);
    // A mod's font can be any size.
    const auto big = formats::parse_font_file("chr-font 1\nsize 3 2\nglyph 33\n#.#\n.#.\n");
    CHECK(big.width == 3 && big.height == 2 && big.glyphs[33][1] == 0 && big.glyphs[33][4] == 1);
}

TEST(text_overrides_replace_ids) {
    const auto table = TextTable::parse(R"({"language": "de", "texts": [
        {"id": "walk.too_deep", "text": "Das Wasser ist dort zu tief."},
        {"id": "walk.enter_question", "text": "Wollt ihr hinein:\r"},
        {"id": "ui.answer_yes_picked", "text": "\r\u0006\u0000\u0002\u000eJa\u0006\u000f\u0002\u0000 Nein"}
    ]})");
    CHECK(table.size() == 3);
    CHECK(lookup(table, "walk.too_deep") == "Das Wasser ist dort zu tief.");
    CHECK(lookup(table, "walk.enter_question") == "Wollt ihr hinein:\r");
    CHECK(lookup(table, "ui.answer_yes_picked") == std::string("\r\x06\x00\x02\x0eJa\x06\x0f\x02\x00 Nein", 16));
    // The first layer that has an ID wins.
    TextTable base;
    base.set("walk.too_deep", "original");
    base.set("panel.party_heading", "BDC");
    const LayeredText layered({&table, &base});
    CHECK(lookup(layered, "walk.too_deep") == "Das Wasser ist dort zu tief.");
    CHECK(lookup(layered, "panel.party_heading") == "BDC");
    bool threw = false;
    try {
        TextTable::parse(R"({"texts": [{"id": "x"}]})");
    } catch (const formats::JsonError&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(formats::json_quote("a\r\x06\"") == R"("a\r\u0006\"")");
}

// The copy check: a change inside a piece the rebuild imports is refused and
// named; a change anywhere else (here the first code byte, which no piece
// covers) is accepted, so a copy that differs only outside them imports.
TEST(exe_check_names_changed_pieces_and_ignores_the_rest) {
    const auto dir = chrtest::game_dir();
    if (dir.empty()) {
        chrtest::skip("no game folder (set CHR_GAME_DIR)");
        return;
    }
    const auto path = formats::find_game_file(dir, "BTECH.EXE");
    const auto unpacked = formats::unexepack(formats::read_file(*path));
    const std::size_t image = std::size_t(formats::kDos103Layout.header_paragraphs) * 16;

    auto outside = unpacked;
    outside[image] ^= 0xff;  // 1000:0000
    bool accepted = true;
    try {
        formats::OriginalExe::from_unpacked(outside);
    } catch (const formats::FormatError&) {
        accepted = false;
    }
    CHECK(accepted);

    const auto& span = formats::OriginalExe::table()[1];  // the first table after the font
    auto inside = unpacked;
    inside[image + std::size_t(span.segment - 0x1000) * 16 + span.offset] ^= 0xff;
    std::string message;
    try {
        formats::OriginalExe::from_unpacked(inside);
    } catch (const formats::FormatError& e) {
        message = e.what();
    }
    CHECK(message.find(std::string(span.name)) != std::string::npos);
    CHECK(message.find("1 of the pieces") != std::string::npos);

    auto layout = unpacked;
    layout[6] ^= 1;  // the relocation count
    bool refused = false;
    try {
        formats::OriginalExe::from_unpacked(layout);
    } catch (const formats::FormatError& e) {
        refused = std::string(e.what()).find("layout") != std::string::npos;
    }
    CHECK(refused);
}
