#include "formats/tables.h"

#include <cstdio>
#include <functional>
#include <span>
#include <vector>

#include "formats/cmp.h"  // FormatError
#include "formats/exe_data.h"
#include "formats/game_files.h"
#include "formats/json.h"

namespace chr::formats {

namespace fs = std::filesystem;

namespace {

enum class Element { U8, S8, U16, S16 };

int element_size(Element e) { return e == Element::U8 || e == Element::S8 ? 1 : 2; }

// A table of numbers: its import-table name (tables.<name>), where it is,
// how each number is stored, and where it goes in Tables.
struct Flat {
    std::string_view name;
    std::uint16_t segment, offset;
    Element element;
    std::function<std::span<int>(Tables&)> values;
};

template <std::size_t N>
std::span<int> all(std::array<int, N>& a) { return a; }
template <std::size_t N, std::size_t M>
std::span<int> all(std::array<std::array<int, M>, N>& a) { return {a.front().data(), N * M}; }

const std::vector<Flat>& flat_tables() {
    static const std::vector<Flat> kFlat = {
        {"palette", 0x37e8, 0x0000, Element::U8, [](Tables& t) { return all(t.palette); }},
        {"endmech_palette", 0x3858, 0x0000, Element::U8, [](Tables& t) { return all(t.endmech_palette); }},
        {"frame_pieces_0", 0x385b, 0x0320, Element::U16, [](Tables& t) { return all(t.frame_pieces[0]); }},
        {"frame_pieces_1", 0x385b, 0x0338, Element::U16, [](Tables& t) { return all(t.frame_pieces[1]); }},
        {"frame_pieces_2", 0x385b, 0x0350, Element::U16, [](Tables& t) { return all(t.frame_pieces[2]); }},
        {"seen_bits", 0x46db, 0x029a, Element::U8, [](Tables& t) { return all(t.seen_bits); }},
        {"money_loss_mask", 0x46db, 0x02a2, Element::U16, [](Tables& t) { return all(t.money_loss_mask); }},
        {"money_rate", 0x46db, 0x02a8, Element::U16, [](Tables& t) { return all(t.money_rate); }},
        {"animated_tiles", 0x46db, 0x04b0, Element::U8, [](Tables& t) { return all(t.animated_tiles); }},
        {"walking_scripts", 0x37e8, 0x0270, Element::U8, [](Tables& t) { return all(t.walking_scripts); }},
        {"walking_keys", 0x46db, 0x0160, Element::S16, [](Tables& t) { return all(t.walking_keys); }},
        {"formation_x", 0x46db, 0x0464, Element::S16, [](Tables& t) { return all(t.formation_x); }},
        {"formation_y", 0x46db, 0x047c, Element::S16, [](Tables& t) { return all(t.formation_y); }},
        {"party_water", 0x46db, 0x0332, Element::U16, [](Tables& t) { return all(t.party_water); }},
        {"place_pictures", 0x46db, 0x141a, Element::U8, [](Tables& t) { return all(t.place_pictures); }},
        {"picture_chance", 0x46db, 0x0ac4, Element::U16, [](Tables& t) { return all(t.picture_chance); }},
        {"direction_of", 0x2c6c, 0x0240, Element::S8, [](Tables& t) { return all(t.direction_of); }},
        {"step_x", 0x46db, 0x311a, Element::S16, [](Tables& t) { return all(t.step_x); }},
        {"step_y", 0x46db, 0x312a, Element::S16, [](Tables& t) { return all(t.step_y); }},
        {"turns", 0x46db, 0x310a, Element::S8, [](Tables& t) { return all(t.turns); }},
        {"person_water", 0x46db, 0x032e, Element::U8, [](Tables& t) { return all(t.person_water); }},
        {"mech_step_x", 0x46db, 0x045c, Element::S16, [](Tables& t) { return all(t.mech_step_x); }},
        {"mech_step_y", 0x46db, 0x0474, Element::S16, [](Tables& t) { return all(t.mech_step_y); }},
        {"marked_spot_x", 0x46db, 0x1632, Element::S8, [](Tables& t) { return all(t.marked_spot_x); }},
        {"marked_spot_y", 0x46db, 0x163a, Element::S8, [](Tables& t) { return all(t.marked_spot_y); }},
        {"mech_formation_x", 0x46db, 0x3a26, Element::S8, [](Tables& t) { return all(t.mech_formation_x); }},
        {"mech_formation_y", 0x46db, 0x3a2a, Element::S8, [](Tables& t) { return all(t.mech_formation_y); }},
        {"mech_cell", 0x46db, 0x02de, Element::S16, [](Tables& t) { return all(t.mech_cell); }},
        {"mech_water", 0x46db, 0x02e6, Element::U16, [](Tables& t) { return all(t.mech_water); }},
        {"mech_cell_odd_x", 0x46db, 0x02ee, Element::S16, [](Tables& t) { return all(t.mech_cell_odd_x); }},
        {"mech_cell_odd_y", 0x46db, 0x02f6, Element::S16, [](Tables& t) { return all(t.mech_cell_odd_y); }},
        {"enemy_offset_x", 0x46db, 0x1642, Element::S8, [](Tables& t) { return all(t.enemy_offset_x); }},
        {"enemy_offset_y", 0x46db, 0x164e, Element::S8, [](Tables& t) { return all(t.enemy_offset_y); }},
        {"los_step_x", 0x46db, 0x328a, Element::S16, [](Tables& t) { return all(t.los_step_x); }},
        {"los_step_y", 0x46db, 0x329a, Element::S16, [](Tables& t) { return all(t.los_step_y); }},
        {"los_area_x", 0x46db, 0x32aa, Element::S16, [](Tables& t) { return all(t.los_area_x); }},
        {"los_area_y", 0x46db, 0x32ba, Element::S16, [](Tables& t) { return all(t.los_area_y); }},
        {"los_row", 0x46db, 0x32ca, Element::S16, [](Tables& t) { return all(t.los_row); }},
        {"step_direction", 0x46db, 0x2ecc, Element::S8, [](Tables& t) { return all(t.step_direction); }},
        {"path_quarters", 0x46db, 0x3b12, Element::U8, [](Tables& t) { return all(t.path_quarters); }},
        {"leg_actuator_bit", 0x46db, 0x316a, Element::U8, [](Tables& t) { return all(t.leg_actuator_bit); }},
        {"leg_actuator_keep", 0x46db, 0x3172, Element::U8, [](Tables& t) { return all(t.leg_actuator_keep); }},
        {"arm_actuator_bit", 0x46db, 0x317a, Element::U8, [](Tables& t) { return all(t.arm_actuator_bit); }},
        {"arm_actuator_keep", 0x46db, 0x3182, Element::U8, [](Tables& t) { return all(t.arm_actuator_keep); }},
        {"door_tiles", 0x46db, 0x246e, Element::U8, [](Tables& t) { return all(t.door_tiles); }},
        {"door_x", 0x46db, 0x247a, Element::S8, [](Tables& t) { return all(t.door_x); }},
        {"door_y", 0x46db, 0x2486, Element::S8, [](Tables& t) { return all(t.door_y); }},
        {"door_item_0", 0x46db, 0x2492, Element::S8, [](Tables& t) { return all(t.door_items[0]); }},
        {"door_item_1", 0x46db, 0x249e, Element::S8, [](Tables& t) { return all(t.door_items[1]); }},
        {"door_item_2", 0x46db, 0x24aa, Element::S8, [](Tables& t) { return all(t.door_items[2]); }},
        {"item_x", 0x46db, 0x21ce, Element::S8, [](Tables& t) { return all(t.item_x); }},
        {"item_y", 0x46db, 0x21f0, Element::S8, [](Tables& t) { return all(t.item_y); }},
        {"item_slot", 0x46db, 0x2212, Element::S8, [](Tables& t) { return all(t.item_slot); }},
        {"cache_wall", 0x46db, 0x20bc, Element::U8, [](Tables& t) { return all(t.cache_wall); }},
        {"puzzle_cells", 0x46db, 0x241e, Element::U16, [](Tables& t) { return all(t.puzzle_cells); }},
        {"location_slot_first", 0x46db, 0x318a, Element::U8, [](Tables& t) { return all(t.location_slot_first); }},
        {"location_slot_count", 0x46db, 0x3192, Element::U8, [](Tables& t) { return all(t.location_slot_count); }},
        {"pose_scripts", 0x46db, 0x3ef0, Element::U8, [](Tables& t) { return all(t.pose_scripts); }},
        {"mech_poses", 0x46db, 0x2d58, Element::U16, [](Tables& t) { return all(t.mech_poses); }},
        {"person_poses", 0x46db, 0x3ff8, Element::U16, [](Tables& t) { return all(t.person_poses); }},
        {"reaction_script", 0x46db, 0x2e3c, Element::U8, [](Tables& t) { return all(t.reaction_script); }},
        {"explosion_script", 0x46db, 0x41d8, Element::U8, [](Tables& t) { return all(t.explosion_script); }},
        {"person_quarters", 0x46db, 0x0372, Element::U16, [](Tables& t) { return all(t.person_quarters); }},
        {"muzzle", 0x46db, 0x2d28, Element::S8, [](Tables& t) { return all(t.muzzle); }},
        {"line_steps", 0x46db, 0x41dc, Element::S8, [](Tables& t) { return all(t.line_steps); }},
        {"encounter_templates", 0x46db, 0x13e2, Element::U16, [](Tables& t) { return all(t.encounter_templates); }},
        {"encounter_sprite_sets", 0x46db, 0x140e, Element::U8, [](Tables& t) { return all(t.encounter_sprite_sets); }},
        {"spectator_script", 0x46db, 0x4dd8, Element::U8, [](Tables& t) { return all(t.spectator_script); }},
        {"arena_wall", 0x46db, 0x2b8e, Element::U8, [](Tables& t) { return all(t.arena_wall); }},
        {"path_arrows", 0x46db, 0x3b06, Element::U8, [](Tables& t) { return all(t.path_arrows); }},
        {"stats_bar_structure", 0x46db, 0x1306, Element::U16, [](Tables& t) { return all(t.stats_bar_structure); }},
        {"stats_bar_x", 0x46db, 0x131c, Element::U16, [](Tables& t) { return all(t.stats_bar_x); }},
        {"stats_bar_y", 0x46db, 0x1332, Element::U16, [](Tables& t) { return all(t.stats_bar_y); }},
        {"stats_palette", 0x46db, 0x1348, Element::U8, [](Tables& t) { return all(t.stats_palette); }},
        {"stats_pulse", 0x46db, 0x1378, Element::U8, [](Tables& t) { return all(t.stats_pulse); }},
        {"world_heights", 0x2c6c, 0x0b0b, Element::U8, [](Tables& t) { return all(t.world_heights); }},
        {"area_maps", 0x37e8, 0x0030, Element::U8, [](Tables& t) { return all(t.area_maps); }},
        {"encounter_weapons", 0x46db, 0x2cf4, Element::U8, [](Tables& t) { return all(t.encounter_weapons); }},
        {"heal_dice", 0x46db, 0x2602, Element::U8, [](Tables& t) { return all(t.heal_dice); }},
        {"foot_formation_x", 0x46db, 0x3a16, Element::S8, [](Tables& t) { return all(t.foot_formation_x); }},
        {"alley_companion_skills", 0x46db, 0x1e7a, Element::U8, [](Tables& t) { return all(t.alley_companion_skills); }},
        {"alley_offset_x", 0x46db, 0x1e82, Element::S8, [](Tables& t) { return all(t.alley_offset_x); }},
        {"alley_offset_y", 0x46db, 0x1e8a, Element::S8, [](Tables& t) { return all(t.alley_offset_y); }},
        {"foot_formation_y", 0x46db, 0x3a1e, Element::S8, [](Tables& t) { return all(t.foot_formation_y); }},
        {"terrain_templates", 0x2c6c, 0x0c1d, Element::U8, [](Tables& t) { return all(t.terrain_templates); }},
        {"plain_block", 0x2c6c, 0x209d, Element::U8, [](Tables& t) { return all(t.plain_block); }},
        {"store_tail", 0x2c6c, 0x201d, Element::U8, [](Tables& t) { return all(t.store_tail); }},
        {"mirror_east_west", 0x2c6c, 0x211d, Element::U8, [](Tables& t) { return all(t.mirror_east_west); }},
        {"mirror_north_south", 0x2c6c, 0x213d, Element::U8, [](Tables& t) { return all(t.mirror_north_south); }},
        {"overhead_colours", 0x2c6c, 0x215d, Element::U8, [](Tables& t) { return all(t.overhead_colours); }},
        {"overhead_block_x", 0x46db, 0x0a38, Element::S8, [](Tables& t) { return all(t.overhead_block_x); }},
        {"overhead_block_y", 0x46db, 0x0a46, Element::S8, [](Tables& t) { return all(t.overhead_block_y); }},
        {"overhead_blocks_across", 0x46db, 0x0a54, Element::S8, [](Tables& t) { return all(t.overhead_blocks_across); }},
        {"overhead_blocks_down", 0x46db, 0x0a62, Element::S8, [](Tables& t) { return all(t.overhead_blocks_down); }},
        {"overhead_cell_bytes", 0x46db, 0x0a70, Element::U16, [](Tables& t) { return all(t.overhead_cell_bytes); }},
        {"overhead_pointer_column", 0x46db, 0x0a8a, Element::S8, [](Tables& t) { return all(t.overhead_pointer_column); }},
        {"overhead_pointer_row", 0x46db, 0x0a92, Element::S8, [](Tables& t) { return all(t.overhead_pointer_row); }},
        {"sound_lists", 0x46db, 0x5008, Element::U16, [](Tables& t) { return all(t.sound_lists); }},
    };
    return kFlat;
}

int read_element(std::span<const std::uint8_t> b, std::size_t i, Element e) {
    switch (e) {
    case Element::U8: return b[i];
    case Element::S8: return std::int8_t(b[i]);
    case Element::U16: return b[2 * i] | b[2 * i + 1] << 8;
    case Element::S16: return std::int16_t(b[2 * i] | b[2 * i + 1] << 8);
    }
    return 0;
}

bool fits(int v, Element e) {
    switch (e) {
    case Element::U8: return v >= 0 && v <= 0xff;
    case Element::S8: return v >= -0x80 && v <= 0x7f;
    case Element::U16: return v >= 0 && v <= 0xffff;
    case Element::S16: return v >= -0x8000 && v <= 0x7fff;
    }
    return false;
}

std::string address(std::uint16_t segment, std::uint16_t offset) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04x:%04x", segment, offset);
    return buf;
}

std::string number_list(std::span<const int> v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + std::to_string(v[i]);
    return s + "]";
}

const Json& member(const Json& object, std::string_view key) {
    const Json* j = object.find(key);
    if (!j) throw FormatError("tables: no \"" + std::string(key) + "\"");
    return *j;
}

int whole(const Json& j, int lo, int hi, std::string_view what) {
    const double v = j.number();
    if (v < lo || v > hi || v != double(int(v))) throw FormatError("tables: " + std::string(what) + " is out of range");
    return int(v);
}

std::string span_name(std::string_view name) { return "tables." + std::string(name); }

// The record fields, in the order of their words.
constexpr std::array<std::string_view, 8> kWindowFields = {"x", "y", "width", "height", "fg", "bg", "col", "row"};
std::array<int*, 8> window_fields(WindowRecord& w) {
    return {&w.x, &w.y, &w.width, &w.height, &w.fg, &w.bg, &w.col, &w.row};
}
// The menu record's words 0, 1, 2, 4 and 5 (+6 is state, +0c/+0e unused).
constexpr std::array<std::string_view, 5> kMenuFields = {"first_row", "width", "items", "colour", "highlight"};
constexpr std::array<int, 5> kMenuWords = {0, 1, 2, 4, 5};
std::array<int*, 5> menu_fields(MenuRecord& m) { return {&m.first_row, &m.width, &m.items, &m.colour, &m.highlight}; }

constexpr std::uint16_t kScreenSegment = 0x385b;
constexpr std::uint16_t kWindowsAt = 0x0000, kMenusAt = 0x0092, kStylesAt = 0x4fa4;
constexpr std::array<std::uint16_t, kFramePieceTables> kPieceTablesAt = {0x0320, 0x0338, 0x0350};

}  // namespace

fs::path tables_path(const fs::path& data_dir) { return data_dir / "tables.json"; }

Tables tables_from_original(const OriginalExe& exe) {
    Tables t;
    for (const auto& f : flat_tables()) {
        const auto bytes = exe.get(span_name(f.name));
        auto out = f.values(t);
        if (bytes.size() != out.size() * std::size_t(element_size(f.element)))
            throw FormatError("tables: " + std::string(f.name) + " has the wrong size");
        for (std::size_t i = 0; i < out.size(); ++i) out[i] = read_element(bytes, i, f.element);
    }
    const auto windows = exe.get("tables.windows");
    for (int n = 0; n < kWindowCount; ++n) {
        const auto fields = window_fields(t.windows[std::size_t(n)]);
        for (std::size_t k = 0; k < fields.size(); ++k)
            *fields[k] = read_element(windows, std::size_t(8 * n) + k, Element::S16);
    }
    const auto menus = exe.get("tables.menus");
    for (int m = 0; m < kMenuCount; ++m) {
        const auto fields = menu_fields(t.menus[std::size_t(m)]);
        for (std::size_t k = 0; k < fields.size(); ++k)
            *fields[k] = read_element(menus, std::size_t(8 * m + kMenuWords[k]), Element::S16);
    }
    // DG:4fa4: a far pointer per style, to one of the three piece tables.
    const auto styles = exe.get("tables.frame_styles");
    for (int s = 0; s < kFrameStyleCount; ++s) {
        const int offset = read_element(styles, std::size_t(2 * s), Element::U16);
        const int segment = read_element(styles, std::size_t(2 * s + 1), Element::U16);
        int which = -1;
        for (int p = 0; p < kFramePieceTables; ++p)
            if (offset == kPieceTablesAt[std::size_t(p)] && segment == kScreenSegment - 0x1000) which = p;
        if (which < 0) throw FormatError("tables: frame style " + std::to_string(s) + " points at no piece table");
        t.frame_style_pieces[std::size_t(s)] = which;
    }
    return t;
}

std::string write_tables_file(const Tables& tables, int import_version) {
    Tables t = tables;
    std::string s = "{\n  \"import_version\": " + std::to_string(import_version) + ",\n  \"source\": " +
                    json_quote("BTECH.EXE " + std::string(kSupportedVersion)) +
                    ",\n  \"what\": \"the program's small tables (docs/formats/tables.md)\",\n  \"tables\": {\n";
    bool first = true;
    for (const auto& f : flat_tables()) {
        s += std::string(first ? "" : ",\n") + "    " + json_quote(f.name) + ": {\"address\": " +
             json_quote(address(f.segment, f.offset)) + ", \"values\": " + number_list(f.values(t)) + "}";
        first = false;
    }
    s += "\n  },\n  \"windows\": {\"address\": " + json_quote(address(kScreenSegment, kWindowsAt)) + ", \"records\": [\n";
    for (int n = 0; n < kWindowCount; ++n) {
        const auto fields = window_fields(t.windows[std::size_t(n)]);
        s += std::string(n ? ",\n" : "") + "    {";
        for (std::size_t k = 0; k < fields.size(); ++k)
            s += std::string(k ? ", " : "") + json_quote(kWindowFields[k]) + ": " + std::to_string(*fields[k]);
        s += "}";
    }
    s += "\n  ]},\n  \"menus\": {\"address\": " + json_quote(address(kScreenSegment, kMenusAt)) + ", \"records\": [\n";
    for (int m = 0; m < kMenuCount; ++m) {
        const auto fields = menu_fields(t.menus[std::size_t(m)]);
        s += std::string(m ? ",\n" : "") + "    {";
        for (std::size_t k = 0; k < fields.size(); ++k)
            s += std::string(k ? ", " : "") + json_quote(kMenuFields[k]) + ": " + std::to_string(*fields[k]);
        s += "}";
    }
    s += "\n  ]},\n  \"frame_styles\": {\"address\": " + json_quote(address(0x46db, kStylesAt)) +
         ", \"by\": \"frame style: frame_pieces_<n>\", \"values\": " + number_list(t.frame_style_pieces) + "}\n}\n";
    return s;
}

Tables read_tables_file(const std::string& text) {
    Tables t;
    const Json root = parse_json(text);
    const Json& flat = member(root, "tables");
    for (const auto& f : flat_tables()) {
        const auto& a = member(member(flat, f.name), "values").array();
        auto out = f.values(t);
        if (a.size() != out.size()) throw FormatError("tables: " + std::string(f.name) + " has the wrong length");
        for (std::size_t i = 0; i < out.size(); ++i) {
            const double v = a[i].number();
            if (v != double(int(v)) || !fits(int(v), f.element))
                throw FormatError("tables: a value of " + std::string(f.name) + " is out of range");
            out[i] = int(v);
        }
    }
    const auto& windows = member(member(root, "windows"), "records").array();
    if (windows.size() != std::size_t(kWindowCount)) throw FormatError("tables: windows needs 11 records");
    for (int n = 0; n < kWindowCount; ++n) {
        const auto fields = window_fields(t.windows[std::size_t(n)]);
        for (std::size_t k = 0; k < fields.size(); ++k)
            *fields[k] = whole(member(windows[std::size_t(n)], kWindowFields[k]), -0x8000, 0x7fff, kWindowFields[k]);
    }
    const auto& menus = member(member(root, "menus"), "records").array();
    if (menus.size() != std::size_t(kMenuCount)) throw FormatError("tables: menus needs 41 records");
    for (int m = 0; m < kMenuCount; ++m) {
        const auto fields = menu_fields(t.menus[std::size_t(m)]);
        for (std::size_t k = 0; k < fields.size(); ++k)
            *fields[k] = whole(member(menus[std::size_t(m)], kMenuFields[k]), -0x8000, 0x7fff, kMenuFields[k]);
    }
    const auto& styles = member(member(root, "frame_styles"), "values").array();
    if (styles.size() != std::size_t(kFrameStyleCount)) throw FormatError("tables: frame_styles needs 5 values");
    for (int s = 0; s < kFrameStyleCount; ++s)
        t.frame_style_pieces[std::size_t(s)] = whole(styles[std::size_t(s)], 0, kFramePieceTables - 1, "a frame style");
    return t;
}

Tables read_tables(const fs::path& data_dir) {
    const auto bytes = read_file(tables_path(data_dir));
    return read_tables_file(std::string(bytes.begin(), bytes.end()));
}

}  // namespace chr::formats
