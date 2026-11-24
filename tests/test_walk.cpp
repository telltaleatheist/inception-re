#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "formats/import.h"
#include "formats/game_files.h"
#include "formats/mtp.h"
#include "game/figures.h"
#include "game/map_view.h"
#include "game/money.h"
#include "game/random.h"
#include "game/movement.h"
#include "game/town.h"
#include "game/townspeople.h"
#include "formats/rules.h"
#include "formats/tables.h"
#include "test.h"

using namespace chr;
using namespace chr::formats;
using namespace chr::game;

namespace {

// A 16x16 map of open ground (tile 0) with a wall (tile 0x55) along cell
// row 3, and entrance 0 at half-cell (10, 20).
MtpMap open_map() {
    MtpMap map;
    map.width = 16;
    map.height = 16;
    map.blocks.assign(256, 0);
    for (int x = 0; x < 8; ++x) {
        map.blocks[3 * 8 + x] = 0x55;        // block 0, row 3
        map.blocks[64 + 3 * 8 + x] = 0x55;   // block 1, row 3
    }
    map.places[0] = "Somewhere";
    map.entrances[0] = {10, 20};
    for (int i = 1; i < 16; ++i) map.entrances[i] = {0xffff, 0xffff};
    return map;
}

struct Answer : EntranceQuestion {
    bool yes = false;
    int asked = -1;
    bool ask(int place, bool mech) override {
        asked = place;
        asked_mech = mech;
        return yes;
    }
    bool asked_mech = false;
};

// The terrain around `map`, placed as the town places it.
Terrain terrain_for(const MtpMap& map, const Tables& tables) {
    static const Fixes fixes;
    Terrain t(tables, fixes);
    t.place_map(map, 4);
    t.link_blocks();
    return t;
}

}  // namespace

TEST(keys_give_directions) {
    CHECK(step_for_key(key::kUp)->dy == -1 && step_for_key(key::kUp)->dx == 0);
    CHECK(step_for_key(key::kPageDown)->dx == 1 && step_for_key(key::kPageDown)->dy == 1);
    CHECK(step_for_key(key::kHome)->dx == -1 && step_for_key(key::kHome)->dy == -1);
    CHECK(!step_for_key('x'));
}

TEST(step_moves_a_half_cell) {
    const auto* tables = chrtest::tables();
    if (!tables) return chrtest::skip("no imported tables (set CHR_GAME_DIR)");
    const auto map = open_map();
    Answer no;
    const auto r = take_step(map, 1, MapPosition{10, 12}, Party{}, {1, 1}, no, *tables, terrain_for(map, *tables), MechParty{}, MapPosition{10, 12});
    CHECK(r.outcome == StepOutcome::Moved);
    CHECK(r.position == (MapPosition{11, 13}));
}

TEST(wall_blocks) {
    const auto* tables = chrtest::tables();
    if (!tables) return chrtest::skip("no imported tables (set CHR_GAME_DIR)");
    const auto map = open_map();
    Answer no;
    // Half-cell row 8 is cell row 4; one up is half-cell 7, cell row 3: the wall.
    const auto r = take_step(map, 1, MapPosition{10, 8}, Party{}, {0, -1}, no, *tables, terrain_for(map, *tables), MechParty{}, MapPosition{10, 8});
    CHECK(r.outcome == StepOutcome::Blocked);
    CHECK(r.position == (MapPosition{10, 8}));
}

TEST(entrance_asks_and_then_checks_the_step) {
    const auto* tables = chrtest::tables();
    if (!tables) return chrtest::skip("no imported tables (set CHR_GAME_DIR)");
    const auto map = open_map();
    Answer yes;
    yes.yes = true;
    const auto r = take_step(map, 1, MapPosition{10, 21}, Party{}, {0, -1}, yes, *tables, terrain_for(map, *tables), MechParty{}, MapPosition{10, 21});
    CHECK(yes.asked == 0 && r.entered);
    CHECK(r.outcome == StepOutcome::Moved);
}

TEST(mech_checks_two_cells_and_entrances_beside_it) {
    const auto* tables = chrtest::tables();
    if (!tables) return chrtest::skip("no imported tables (set CHR_GAME_DIR)");
    const auto map = open_map();
    const auto terrain = terrain_for(map, *tables);
    Answer no;
    // No character walks; one 'Mech stands two half-cells above the party.
    Party nobody;
    nobody.present.fill(false);
    MechParty mech;
    mech.present[0] = true;
    // Its figure is one half-cell left of an entrance: the 'Mech is asked.
    mech.at[0] = {std::uint16_t(map.entrances[0].x - 1), map.entrances[0].y};
    take_step(map, 1, MapPosition{10, 21}, nobody, {0, 0}, no, *tables, terrain, mech, MapPosition{10, 21});
    CHECK(no.asked == 0 && no.asked_mech);
    // From half-cell row 9 the 'Mech's foot, two half-cells up, is on
    // half-cell row 7, cell row 3: the wall. From row 12 it is clear.
    mech.at[0] = {0, 0};
    CHECK(take_step(map, 1, MapPosition{10, 9}, nobody, {0, 0}, no, *tables, terrain, mech, MapPosition{10, 9}).outcome == StepOutcome::Blocked);
    CHECK(take_step(map, 1, MapPosition{10, 12}, nobody, {0, 0}, no, *tables, terrain, mech, MapPosition{10, 12}).outcome == StepOutcome::Moved);
}

TEST(area_step_matches_a_fresh_area) {
    // Stepping into the next area moves the grids over and makes the new
    // row or column: the same grids as making all nine around the new area.
    const auto* tables = chrtest::tables();
    if (!tables) return chrtest::skip("no imported tables (set CHR_GAME_DIR)");
    static const Fixes fixes;
    struct Move { int dx, dy, area; };
    for (const Move m : {Move{0, -1, 0xbc}, Move{0, 1, 0xdc}, Move{-1, 0, 0xcb}, Move{1, 0, 0xcd}}) {
        Terrain stepped(*tables, fixes), fresh(*tables, fixes);
        stepped.generate(0xcc);
        stepped.step_area(m.dx, m.dy, m.area);
        fresh.generate(m.area);
        CHECK(stepped.grids() == fresh.grids());
        CHECK(stepped.masks() == fresh.masks());
    }
}

TEST(key_script_parses) {
    const auto keys = parse_key_script("up*3, pgdn idle:40 y");
    CHECK(keys.size() == 6);
    CHECK(keys[0].key == key::kUp && keys[2].key == key::kUp && keys[3].key == key::kPageDown);
    CHECK(keys[4].idle == 40 && keys[5].key == 'y' && keys[5].idle == 0);
    // A question's blocking read skips idle stretches.
    ScriptKeys source(parse_key_script("idle:5 y"));
    CHECK(source.next() == std::optional<int>('y'));
    bool threw = false;
    try {
        parse_key_script("jump");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

// The test aid set:NN=VV: parsed as a flag setting, flag 14 refused, and a
// question's read refuses one (only the town's loop applies it).
TEST(key_script_set_flag) {
    const auto keys = parse_key_script("set:00=02 up");
    CHECK(keys.size() == 2 && keys[0].is_set() && keys[0].set_flag == 0 && keys[0].set_value == 2);
    CHECK(!keys[1].is_set());
    const auto refused = [](const char* script) {
        try {
            parse_key_script(script);
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    CHECK(refused("set:14=01"));
    CHECK(refused("set:64=01"));
    CHECK(refused("set:00=100"));
    CHECK(refused("set:00"));
    ScriptKeys source(parse_key_script("set:05=01 y"));
    bool threw = false;
    try {
        source.next();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

// A question's read after idle passes, a poke and the poke's own idle passes
// gets the key after them all, the poke applied.
TEST(key_script_poke_then_idle) {
    ScriptKeys source(parse_key_script("idle:2 cbills:4e20 idle:2 y"));
    int cbills = -1;
    source.set_poke([&](bool, int record, int, int value) {
        if (record == -1) cbills = value;
    });
    CHECK(source.next() == std::optional<int>('y'));
    CHECK(cbills == 0x4e20);
    CHECK(!source.next());
}

// The test aid pos:XXXX:YYYY: the party's place, handed over as record -4
// with x and y; a malformed one is refused.
TEST(key_script_place_poke) {
    ScriptKeys source(parse_key_script("pos:0208:c021 y"));
    int x = -1, y = -1;
    source.set_poke([&](bool, int record, int byte, int value) {
        if (record == -4) x = byte, y = value;
    });
    CHECK(source.next() == std::optional<int>('y'));
    CHECK(x == 0x0208 && y == 0xc021);
    const auto refused = [](const char* script) {
        try {
            parse_key_script(script);
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    CHECK(refused("pos:0208"));
    CHECK(refused("pos:208:c021"));
    CHECK(refused("pos:0208:c021x"));
}

namespace {

// Plays `script` on map 1 from the new-game start. False if the game files
// are not here (the test is skipped).
struct Map1 {
    MtpMap map;
    std::unique_ptr<TextTable> imported;
    std::unique_ptr<MapText> map_text;
    std::unique_ptr<LayeredText> texts;
    std::unique_ptr<formats::Tables> tables;
    std::optional<formats::Rules> rules;
};

bool play_map1(const std::string& script, std::unique_ptr<Town>& town, Map1& m,
               const TextProvider* overrides = nullptr, bool start_up = false, bool game_files = false,
               bool with_rules = false) {
    const auto dir = chrtest::game_dir();
    if (dir.empty()) {
        chrtest::skip("no game folder (set CHR_GAME_DIR)");
        return false;
    }
    m.map = decode_mtp(read_file(*find_game_file(dir, "MAP1.MTP")));
    // The text as the importer writes it, imported once into the build tree.
    const std::filesystem::path data = std::filesystem::path(CHR_TEST_SCRATCH) / "walk-data";
    ensure_imported(dir, data);
    m.tables = std::make_unique<formats::Tables>(formats::read_tables(data));
    m.imported = std::make_unique<TextTable>(TextTable::load(imported_text_path(data)));
    m.map_text = std::make_unique<MapText>(m.map, 1);
    std::vector<const TextProvider*> layers{m.imported.get(), m.map_text.get()};
    if (overrides) layers.insert(layers.begin(), overrides);
    m.texts = std::make_unique<LayeredText>(layers);
    town = std::make_unique<Town>(m.map, 1, new_game_position(), *m.texts, *m.tables);
    if (game_files) town->set_source(std::make_shared<const formats::DosGameSource>(dir));
    if (with_rules) {
        m.rules = formats::read_rules(data);
        town->set_rules(&*m.rules);
    }
    if (start_up) town->start_up();
    else town->draw_panel();
    ScriptKeys keys(parse_key_script(script));
    town->run(keys);
    return true;
}

}  // namespace

TEST(start_up_screens_and_questions) {
    auto play = [](const std::string& script, std::unique_ptr<Town>& town, Map1& m) {
        return play_map1(script, town, m, nullptr, true);
    };
    std::unique_ptr<Town> town;
    Map1 m;
    // The Infocom screen goes after 700 frames, or at a key.
    if (!play("idle:699", town, m)) return;
    CHECK(town->full_screen() == assets::kInfocomLogo && town->start_up_open());
    Map1 m2;
    play("idle:300 idle:400", town, m2);
    CHECK(town->full_screen() == assets::kTitleScreen);
    Map1 m3;
    play("enter", town, m3);
    CHECK(town->full_screen() == assets::kTitleScreen);
    // The title waits for a key, however long; then the first-time question.
    Map1 m4;
    play("enter idle:5000 enter", town, m4);
    CHECK(!town->full_screen() && town->start_up_open());
    // Yes: the town.
    Map1 m5;
    play("enter enter enter", town, m5);
    CHECK(!town->start_up_open() && town->log().back() == "new game");
    // No, then no again: a new game too. No, then yes: the load's slot menu
    // over the new game (docs/startup.md).
    Map1 m6;
    play("enter enter n", town, m6);
    CHECK(town->start_up_open());
    Map1 m7;
    play("enter enter n n", town, m7);
    CHECK(!town->start_up_open() && town->log().back() == "new game");
    Map1 m8;
    play("enter enter n y", town, m8);
    CHECK(town->start_up_open() && town->log().back() == "new game");
}

TEST(map1_wall_south_of_the_start) {
    std::unique_ptr<Town> town;
    Map1 m;
    if (!play_map1("down*4", town, m)) return;
    // Two steps south, then the fence.
    CHECK(town->position() == (MapPosition{0x0c45, 0xc01b}));
    CHECK(town->log()[1] == "moved at 0c45,c01b");
    CHECK(town->log()[2] == "blocked at 0c45,c01b");
}

TEST(map1_citadel_question) {
    std::unique_ptr<Town> town;
    Map1 m;
    if (!play_map1("up*5", town, m)) return;
    CHECK(town->question_open());
    CHECK(town->position() == (MapPosition{0x0c45, 0xc015}));
    const auto& t = town->text();
    CHECK(t.row_text(10).substr(14, 18) == "Will you enter the");
    CHECK(t.row_text(11).substr(14, 8) == "Citadel?");
    CHECK(t.row_text(12).substr(14, 6) == "Yes No");
    CHECK(t.cell(14, 12).bg == chr::game::TextColour::data(14) && t.cell(14, 12).fg == chr::game::TextColour::data(0));  // Yes picked: black on yellow
    CHECK(t.cell(18, 12).bg == chr::game::TextColour::data(0) && t.cell(18, 12).fg == chr::game::TextColour::data(15));
}

TEST(map1_enter_the_citadel) {
    std::unique_ptr<Town> town;
    Map1 m;
    if (!play_map1("up*5 right y", town, m)) return;
    CHECK(town->entered_place() == 1);
    // The right arrow picked No, then y answered yes: the line shows Yes picked.
    CHECK(town->text().cell(14, 12).bg == chr::game::TextColour::data(14));
    CHECK(town->text().cell(18, 12).bg == chr::game::TextColour::data(0));
}

TEST(map1_say_no_and_walk_on) {
    std::unique_ptr<Town> town;
    Map1 m;
    if (!play_map1("up*5 n down", town, m)) return;
    CHECK(!town->entered_place());
    CHECK(town->position() == (MapPosition{0x0c45, 0xc015}));
    // The next turn draws the map over the question.
    CHECK(town->text().cell(14, 10).kind == TextCell::Kind::Empty);
}

TEST(map1_view_scrolls_with_the_party) {
    std::unique_ptr<Town> town;
    Map1 m;
    if (!play_map1("left*10", town, m)) return;
    CHECK(town->position() == (MapPosition{0x0c3b, 0xc019}));
    const auto view = town->view();
    // The view's corner is 12 half-cells up and left of the party.
    CHECK(view.origin_half_x == 0x3b - 12 && view.origin_half_y == 0x19 - 12);
}

TEST(map1_panel_lists_jason) {
    std::unique_ptr<Town> town;
    Map1 m;
    if (!play_map1("", town, m)) return;
    CHECK(town->text().row_text(13).substr(9, 3) == "BDC");
    CHECK(town->text().row_text(14).substr(1, 5) == "Jason");
    CHECK(town->text().cell(0, 0).kind == TextCell::Kind::Frame);
}

TEST(map1_question_in_another_language) {
    std::unique_ptr<Town> town;
    Map1 m;
    const auto german = TextTable::parse(R"({"texts": [
        {"id": "walk.enter_question", "text": "Wollt ihr hinein in die\r"},
        {"id": "map1.place.1", "text": "Zitadelle"}]})");
    if (!play_map1("up*5", town, m, &german)) return;
    CHECK(town->text().row_text(10).substr(14, 23) == "Wollt ihr hinein in die");
    CHECK(town->text().row_text(11).substr(14, 10) == "Zitadelle?");
}

TEST(map1_move_log_in_the_upper_box) {
    std::unique_ptr<Town> town;
    Map1 m;
    // Five steps north: the fifth asks about the Citadel, so it is not logged.
    if (!play_map1("up*5", town, m)) return;
    const auto& t = town->text();
    CHECK(t.row_text(1).substr(1, 11) == "           ");  // each name starts with a new line
    for (int row = 2; row <= 5; ++row) CHECK(t.row_text(row).substr(1, 6) == "North ");
    CHECK(t.row_text(6).substr(1, 5) == "     ");

    // A blocked step is logged too.
    std::unique_ptr<Town> town2;
    Map1 m2;
    if (!play_map1("down*3 pgup", town2, m2)) return;
    CHECK(town2->text().row_text(4).substr(1, 5) == "South");
    CHECK(town2->text().row_text(5).substr(1, 9) == "Northeast");
}

TEST(map1_game_ticks_and_money) {
    std::unique_ptr<Town> town;
    Map1 m;
    // The first idle pass is a game tick: the money step runs at once (20 is
    // below the floor of 50, so 15 are added), then every 256th tick.
    if (!play_map1("idle:3", town, m)) return;
    CHECK(town->ticks() == 1);
    CHECK(town->animation_phase() == 1);
    CHECK(town->money().cbills == 35);
    CHECK(town->text().row_text(22).substr(1, 8) == "C-Bills:");
    CHECK(town->text().row_text(23).substr(1, 3) == "35 ");
    CHECK(town->text().cell(1, 23).fg == chr::game::TextColour(chr::game::TextRole::Amount));

    // Tick n comes at pass 1 + 11(n - 1): 257 ticks after 2817 passes.
    std::unique_ptr<Town> town2;
    Map1 m2;
    if (!play_map1("idle:2816", town2, m2)) return;
    CHECK(town2->ticks() == 256);
    CHECK(town2->money().cbills == 35);
    std::unique_ptr<Town> town3;
    Map1 m3;
    if (!play_map1("idle:2817", town3, m3)) return;
    CHECK(town3->ticks() == 257);
    CHECK(town3->money().cbills == 50);
    CHECK(town3->animation_phase() == 2);
    CHECK(town3->text().row_text(23).substr(1, 3) == "50 ");
}

TEST(map1_keys_are_game_ticks) {
    std::unique_ptr<Town> town;
    Map1 m;
    // Each key is followed by a tick; the fifth step asks about the Citadel
    // and waits, before its tick.
    if (!play_map1("up*5", town, m)) return;
    CHECK(town->ticks() == 4);
    CHECK(town->animation_phase() == 1);
}

TEST(map1_party_figure) {
    std::unique_ptr<Town> town;
    Map1 m;
    if (!play_map1("", town, m)) return;
    auto figures = town->figures();
    CHECK(figures.size() == 1);
    // Jason: sprite 0x96 + 0x10, one half-cell right of the position.
    CHECK(figures[0].sprite == sprite(0xa6));
    CHECK(figures[0].half_x == (0x45 + 1) && figures[0].half_y == 0x19);
    CHECK(figures[0].sink == 0);

    // Walking north runs frames 0x18-0x1b; the step that asks a question
    // does not move the animation on.
    std::unique_ptr<Town> town2;
    Map1 m2;
    if (!play_map1("up*5", town2, m2)) return;
    CHECK(town2->figures()[0].sprite == sprite(0x96 + 0x1b));
    std::unique_ptr<Town> town3;
    Map1 m3;
    if (!play_map1("up down", town3, m3)) return;
    CHECK(town3->figures()[0].sprite == sprite(0x96 + 0x10));
}

TEST(map1_panel_bars) {
    std::unique_ptr<Town> town;
    Map1 m;
    if (!play_map1("", town, m)) return;
    const auto bars = town->bars();
    CHECK(bars.size() == 3);
    CHECK((bars[0] == StatBar{0, StatBar::Stat::B, 8, 0}));
    CHECK((bars[1] == StatBar{0, StatBar::Stat::D, 9, 0}));
    CHECK((bars[2] == StatBar{0, StatBar::Stat::C, 7, 0}));
}

TEST(random_numbers) {
    // From the state 25 13 90.
    Random r({0x25, 0x13, 0x90});
    const int expected[] = {0xb5, 0x87, 0xf8, 0x8b, 0xab, 0x88, 0x7e, 0x4b, 0xcd, 0xb7, 0xf9, 0xb9};
    for (const int e : expected) CHECK(r.next() == e);
}

TEST(money_step) {
    const auto* tables = chrtest::tables();
    if (!tables) return chrtest::skip("no imported tables (set CHR_GAME_DIR)");
    Random r({0x25, 0x13, 0x90});
    Money money;
    Flags flags = new_game_flags();
    CHECK(money_tick(money, flags, false, r, *tables));  // runs on the first tick
    CHECK(money.cbills == 35 && flags[flag::kMoneyCountdown] == 0xff);
    for (int i = 0; i < 255; ++i) CHECK(!money_tick(money, flags, false, r, *tables));
    CHECK(money_tick(money, flags, false, r, *tables));
    CHECK(money.cbills == 50);
    for (int i = 0; i < 256; ++i) money_tick(money, flags, false, r, *tables);
    CHECK(money.cbills == 50);  // at the floor: no more
    // Investments: x * 100 / rate on a gain, x * rate / 110 on a loss.
    Money m2;
    m2.cbills = 0;
    m2.invested = {9600, 9000, 20000};
    Random r2({0x25, 0x13, 0x90});  // draws 0xb5, 0x87, 0xf8: gains for the first two, a loss for the third
    Flags f2 = new_game_flags();
    money_tick(m2, f2, true, r2, *tables);
    CHECK(m2.invested[0] == 10000 && m2.invested[1] == 10000);
    CHECK(m2.invested[2] == 20000 * 48 / 110);
}

TEST(map1_move_log_scrolls_a_line_at_a_time) {
    // Window 4 holds 11 lines; the 12th step scrolls it by one, with no
    // blank lines between (each entry is "\rSouth": break, then the word).
    std::unique_ptr<Town> town;
    Map1 m;
    if (!play_map1("left*13", town, m)) return;
    for (int row = 1; row <= 11; ++row) CHECK(town->text().row_text(row).substr(1, 4) == "West");
}

TEST(window_tile_wraps_to_the_next_row) {
    // A 32x32-cell map of open ground. The party stands in cell (11, 11),
    // so the original's window holds cells 0-23 both ways. A step to cell
    // (24, 11), just past the window's right edge, is checked against the
    // cell at the start of the next row, (0, 12).
    MtpMap map;
    map.width = 32;
    map.height = 32;
    map.blocks.assign(32 * 32, 0);
    map.blocks[(1 * 4 + 0) * 64 + 4 * 8 + 0] = 0x55;  // cell (0, 12): block (0, 1), row 4
    const Tables tables;
    const Fixes fixes;
    Terrain terrain(tables, fixes);
    terrain.place_map(map, 4);
    terrain.link_blocks();
    bool outside = true;
    CHECK(window_tile(map, terrain, {0x0c16, 0xc016}, {0x0c2f, 0xc016}, outside) == 0x55);
    CHECK(!outside);
    CHECK(map.cell(24, 11) == 0);
    // Inside the window it is the cell itself.
    CHECK(window_tile(map, terrain, {0x0c16, 0xc016}, {0x0c17, 0xc019}, outside) == map.cell(12, 12));
    CHECK(!outside);
}

TEST(direction_toward_target) {
    const auto* tables = chrtest::tables();
    if (!tables) return chrtest::skip("no imported tables (set CHR_GAME_DIR)");
    using formats::MapPosition;
    const MapPosition at{0x0c20, 0xc020};
    CHECK(direction_toward(at, at, *tables) == -1);
    CHECK(direction_toward(at, {0x0c20, 0xc010}, *tables) == 0);  // north
    CHECK(direction_toward(at, {0x0c30, 0xc010}, *tables) == 1);  // north-east
    CHECK(direction_toward(at, {0x0c30, 0xc021}, *tables) == 2);  // east (nearer east than south-east)
    CHECK(direction_toward(at, {0x0c22, 0xc030}, *tables) == 4);  // south
    CHECK(direction_toward(at, {0x0c10, 0xc030}, *tables) == 5);  // south-west
    CHECK(direction_toward(at, {0x0c10, 0xc010}, *tables) == 7);  // north-west
}

TEST(map1_townspeople_placed_as_the_original) {
    // From the original's memory at the town start (tools/reference/state.py).
    std::unique_ptr<Town> town;
    Map1 m;
    if (!play_map1("", town, m)) return;
    const auto& w = town->townspeople().walkers();
    CHECK((w[0].at == formats::MapPosition{0x0c20, 0xc02c}) && w[0].route == 0x63 && w[0].wait == 1);
    CHECK((w[1].target == formats::MapPosition{0x0c45, 0xc014}) && w[1].route == 0x51);
    CHECK((w[7].at == formats::MapPosition{0x0c11, 0xc00d}) && w[7].route == 0x46);
    // After 135 passes (12 rounds) only the fifth, near the party, has moved
    // (to 0c47,c016, heading south-east), as in the original.
    std::unique_ptr<Town> town2;
    Map1 m2;
    if (!play_map1("idle:135", town2, m2)) return;
    const auto& w2 = town2->townspeople().walkers();
    CHECK((w2[4].at == formats::MapPosition{0x0c47, 0xc016}) && w2[4].heading == 3);
    CHECK((w2[5].at == formats::MapPosition{0x0c2e, 0xc072}) && w2[5].wait == 0);
    const auto figures = town2->figures();
    CHECK(figures.size() == 2);  // Jason and the fifth person
    CHECK(figures[1].sprite.index >= 0xfe + 0x10 && figures[1].sprite.index <= 0xfe + 0x1f);
}

// The command menu's overhead map (docs/overhead_map.md), with the game
// folder for the map files: item 7 is six rows down without 'Mechs.
TEST(overhead_map_shows_and_waits) {
    std::unique_ptr<Town> town;
    Map1 m;
    if (!play_map1("space down*6 enter", town, m, nullptr, false, true)) return;
    CHECK(town->overhead().has_value());
    CHECK(town->place_open());
    const auto& shown = *town->overhead();
    int blocks = 0;
    for (const auto& t : shown.tiles) blocks += t.kind == OverheadMap::Tile::Kind::Block;
    CHECK(blocks > 0);
    CHECK(int(shown.blocks.size()) == blocks);
    CHECK(shown.dot.has_value());
    CHECK(shown.colours == OverheadMap::Colours::Land);
    // The caption on row 24, nothing else written above it (no pointer:
    // flag 2f is clear at a new game).
    CHECK(!town->text().row_text(24).empty() && town->text().cell(0, 24).kind == TextCell::Kind::Glyph);
    for (int r = 0; r < 24; ++r)
        for (int c = 0; c < kTextColumns; ++c) CHECK(town->text().cell(c, r).kind == TextCell::Kind::Empty);
}

TEST(overhead_map_draws_a_number_each_pass) {
    std::unique_ptr<Town> a, b;
    Map1 ma, mb;
    if (!play_map1("space down*6 enter", a, ma, nullptr, false, true)) return;
    play_map1("space down*6 enter idle:5", b, mb, nullptr, false, true);
    Random r = a->random();
    for (int i = 0; i < 5; ++i) r.next();
    CHECK(r.state() == b->random().state());
}

TEST(overhead_map_closes_in_town_after_one_key) {
    std::unique_ptr<Town> town;
    Map1 m;
    // In the town (flag 04 clear) one key ends it, whatever the key.
    if (!play_map1("space down*6 enter right", town, m, nullptr, false, true)) return;
    CHECK(!town->overhead().has_value());
    CHECK(!town->place_open());
    bool closed = false, skipped = false;
    for (const auto& line : town->log()) {
        closed = closed || line == "overhead map closed";
        skipped = skipped || line.find("no townspeople") != std::string::npos;
    }
    CHECK(closed && skipped);
}

TEST(overhead_map_moves_two_areas_outdoors) {
    std::unique_ptr<Town> a, b;
    Map1 ma, mb;
    // Outdoors (flag 04 set by the test aid) it stays up; right moves the
    // view: the party's area column is 12, the view's first column 10, then
    // 11 (held to 0..11), so the dot moves 64 points left.
    if (!play_map1("set:04=01 space down*6 enter", a, ma, nullptr, false, true)) return;
    play_map1("set:04=01 space down*6 enter right", b, mb, nullptr, false, true);
    CHECK(a->overhead() && b->overhead() && a->overhead()->dot && b->overhead()->dot);
    CHECK(b->overhead()->dot->x == a->overhead()->dot->x - 64);
    CHECK(b->overhead()->dot->y == a->overhead()->dot->y);
    Map1 mc;
    play_map1("set:04=01 space down*6 enter right space", a, mc, nullptr, false, true);
    CHECK(!a->overhead().has_value());
}

TEST(demo_keys_from_made_up_bytes) {
    // Made-up bytes, not the player's DEMOFILE: a key with its top bit set
    // (an arrow, read as minus its scan code), two pauses, Enter, the end
    // mark, and one more.
    const std::vector<std::uint8_t> bytes{0xb8, DemoKeys::kPause, DemoKeys::kPause, 0x0d, DemoKeys::kEndMark, 0x0d};
    ScriptKeys time(parse_key_script("idle:1 idle:60"));
    DemoKeys demo(bytes, time);
    CHECK(demo.next() == -0x48);
    CHECK(demo.position() == 1 && !demo.ended());
    // The next key waits two pauses and a frame: 61 frames, and only 60 are given.
    CHECK(!demo.next());
    CHECK(demo.position() == 1 && !demo.ended());

    ScriptKeys time2(parse_key_script("idle:62 idle:1 idle:1"));
    DemoKeys demo2(bytes, time2);
    CHECK(demo2.next() == -0x48);
    CHECK(demo2.next() == 0x0d && demo2.position() == 4 && !demo2.ended());
    CHECK(demo2.next() == DemoKeys::kEndMark && demo2.ended());
    CHECK(demo2.next() == 0x0d);
    CHECK(!demo2.next() && demo2.ended());  // past the data
    // The poll finds a key and takes nothing.
    ScriptKeys none({});
    DemoKeys demo3(bytes, none);
    CHECK(demo3.poll() == true && demo3.position() == 0);
}

TEST(demo_ends_at_the_players_key) {
    const std::vector<std::uint8_t> bytes{0x0d, 0x0d, 0x0d, 0x0d};
    // A key in the script is the player's: the read after it ends the demo,
    // and still returns the demo's key.
    ScriptKeys time(parse_key_script("idle:1 x idle:1"));
    DemoKeys demo(bytes, time);
    CHECK(demo.next() == 0x0d && !demo.ended());
    CHECK(demo.next() == 0x0d && demo.ended());
    // A key during a wait is caught by the next read.
    ScriptKeys time2(parse_key_script("idle:3 x idle:1"));
    DemoKeys demo2(bytes, time2);
    CHECK(demo2.wait(3) && !demo2.ended());
    CHECK(demo2.wait(1));
    CHECK(demo2.next() == 0x0d && demo2.ended());
}

TEST(tune_end_only_at_the_title) {
    const auto items = parse_key_script("tune-end");
    CHECK(items.size() == 1 && items.front().tune_end);
    bool threw = false;
    try {
        ScriptKeys keys(parse_key_script("tune-end"));
        keys.next();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(demo_plays_and_the_title_comes_back) {
    auto play = [](const std::string& script, std::unique_ptr<Town>& town, Map1& m) {
        return play_map1(script, town, m, nullptr, true, true, true);
    };
    std::unique_ptr<Town> town;
    Map1 m;
    // The tune runs out: the demo, which waits for its time.
    if (!play("enter tune-end idle:100", town, m)) return;
    CHECK(town->demo_playing());
    // Given all its time it plays to its end mark, and the title is back.
    Map1 m2;
    play("enter tune-end idle:100000", town, m2);
    const auto& log = town->log();
    CHECK(std::find(log.begin(), log.end(), "demo end") != log.end());
    CHECK(town->title_tune_playing());
    // A key the player presses ends it after that pass.
    Map1 m3;
    play("enter tune-end idle:100 x idle:10", town, m3);
    CHECK(town->title_tune_playing());
    // A key at the title after a demo: a new game again, then the question.
    Map1 m4;
    play("enter tune-end idle:100000 enter enter", town, m4);
    CHECK(!town->start_up_open() && town->log().back() == "new game");
    CHECK(std::count(town->log().begin(), town->log().end(), std::string("new game")) == 3);
}
