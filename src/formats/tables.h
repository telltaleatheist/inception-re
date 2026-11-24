// The small tables built into BTECH.EXE that the town, the screen and the
// fight read: layouts, offsets, masks and scripts (docs/formats/tables.md).
// The importer reads them from the player's program once and writes them to
// tables.json in the data folder, each under a name with its address; the
// game reads only that file, so a mod can change any of them. The repo has
// the names, addresses and shapes, never the values.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace chr::formats {

class ProgramData;

// A text window, 385b:0000 + 16 n: where its inside starts and how big it is
// in cells, its colours and its cursor.
struct WindowRecord {
    int x = 0, y = 0, width = 0, height = 0;
    int fg = 15, bg = 0;
    int col = 0, row = 0;
    bool operator==(const WindowRecord&) const = default;
};
inline constexpr int kWindowCount = 11;

// A menu, 385b:0092 + 16 m (docs/scripts.md, "Menus"). The record's +6, the
// item last chosen, is game state and starts at 0; it is not imported.
struct MenuRecord {
    int first_row = 0;  // the first item's row in the current window
    int width = 0;      // the highlight bar's width, in cells
    int items = 0;      // how many items (the script's jump table has one word each)
    int colour = 0;     // +8: kept with the record; not read by the menu itself
    int highlight = 0;  // the highlight bar's colour (the bar is XORed in)
    bool operator==(const MenuRecord&) const = default;
};
inline constexpr int kMenuCount = 41;

inline constexpr int kFrameStyleCount = 5;
inline constexpr int kFramePieceTables = 3;
inline constexpr int kFramePieceLength = 12;
inline constexpr std::uint8_t kFramePieceEnd = 0xff;

struct Tables {
    // The screen.
    std::array<int, 16> palette{};                  // 37e8:0000: colour number of each register (EGA)
    std::array<int, 16> endmech_palette{};          // 3858:0000: the same, for ENDMECH.CMP (24d3:15dd)
    std::array<WindowRecord, kWindowCount> windows{};  // 385b:0000
    // 385b:0320/0338/0350: the four corners, then the top, left, right and
    // bottom edges, each a run of frame pieces ended by kFramePieceEnd.
    std::array<std::array<int, kFramePieceLength>, kFramePieceTables> frame_pieces{};
    std::array<int, kFrameStyleCount> frame_style_pieces{};  // DG:4fa4: which piece table each style uses
    std::array<MenuRecord, kMenuCount> menus{};     // 385b:0092

    // The town.
    std::array<int, 8> seen_bits{};                 // DG:029a: the seen map's bits, by x's bits 4-6 (1000:010b)
    std::array<int, 3> money_loss_mask{};           // DG:02a2: per investment, bits that all clear mean a loss
    std::array<int, 3> money_rate{};                // DG:02a8: per investment, the rate
    std::array<int, 10> animated_tiles{};           // DG:04b0: tile numbers that cycle through ANIMATE.ICN
    std::array<int, 0x80> walking_scripts{};        // 37e8:0270: the figures' walking frames (docs/town_loop.md)
    std::array<int, 8> walking_keys{};              // DG:0160: the walking keys, N first, clockwise
    std::array<int, 8> formation_x{};               // DG:0464: the k-th character's half-cells from the party
    std::array<int, 8> formation_y{};               // DG:047c
    std::array<std::array<int, 4>, 8> party_water{};  // DG:0332: water quarter bits by figure k and parity
    std::array<int, 26> place_pictures{};           // DG:141a: by script number, O<n>.ANM or 0
    std::array<int, 3> picture_chance{};            // DG:0ac4: by flag 4f, the mask a random number ANDs
    std::array<int, 8> treatment_cost{};            // DG:4f6e: HOSPITAL's treatment by flag 1a (1-7), C-Bills
    std::array<int, 7> ammo_price{};                // DG:2046 + w: a shot for slot codes w 0x1a-0x20, C-Bills
    std::array<int, 9> upgrade_price{};             // DG:1d8c: the 'Mech upgrade by case (flag 11, 0-8), C-Bills
    std::array<int, 11> upgrade_armour{};           // DG:1e13 + 0x11..0x1b: case 3's armour, full and current
    // The townspeople.
    std::array<int, 16> direction_of{};             // 2c6c:0240: 287f:0971's four tests to a direction
    std::array<int, 8> step_x{};                    // DG:311a: one half-cell in each direction
    std::array<int, 8> step_y{};                    // DG:312a
    std::array<int, 16> turns{};                    // DG:310a: the turns 1e31:0006 tries
    std::array<int, 4> person_water{};              // DG:032e: water quarter bits by parity

    // The fight.
    std::array<int, 4> mech_step_x{};               // DG:045c: the k-th 'Mech's half-cells from the party, for the step check
    std::array<int, 4> mech_step_y{};               // DG:0474
    std::array<int, 8> marked_spot_x{};             // DG:1632: mission 1's eight spots, cells in the area
    std::array<int, 8> marked_spot_y{};             // DG:163a
    std::array<int, 4> mech_formation_x{};          // DG:3a26: the k-th 'Mech's half-cells from the party
    std::array<int, 4> mech_formation_y{};          // DG:3a2a
    std::array<int, 4> mech_cell{};                 // DG:02de: the window cell under the k-th 'Mech
    std::array<int, 4> mech_water{};                // DG:02e6: its water quarter bits
    std::array<int, 4> mech_cell_odd_x{};           // DG:02ee: added when the party's x is odd
    std::array<int, 4> mech_cell_odd_y{};           // DG:02f6: added when the party's y is odd
    std::array<int, 12> enemy_offset_x{};           // DG:1642: 17dc:0d49's enemy places
    std::array<int, 12> enemy_offset_y{};           // DG:164e

    // The round (docs/combat.md 3).
    std::array<int, 8> los_step_x{};                // DG:328a: 1e31:1bfe's step in each direction
    std::array<int, 8> los_step_y{};                // DG:329a
    std::array<int, 8> los_area_x{};                // DG:32aa: added when the step crosses into another area
    std::array<int, 8> los_area_y{};                // DG:32ba
    std::array<int, 8> los_row{};                   // DG:32ca: the cell window's rows a y step moves
    std::array<int, 11> step_direction{};           // DG:2ecc: by (dy + 1) 4 + dx + 1, a path step's direction
    std::array<int, 4> path_quarters{};             // DG:3b12: by parity, the tile bits a path step's cell costs
    // 1e31:11ab's critical hits: by a d6, an actuator's bit and the mask
    // that clears it (legs, arms); by location - 0x1c, its item slots.
    std::array<int, 8> leg_actuator_bit{};          // DG:316a
    std::array<int, 8> leg_actuator_keep{};         // DG:3172
    std::array<int, 8> arm_actuator_bit{};          // DG:317a
    std::array<int, 8> arm_actuator_keep{};         // DG:3182
    std::array<int, 8> location_slot_first{};       // DG:318a: the location's first item byte
    std::array<int, 8> location_slot_count{};       // DG:3192: how many

    // The cache's doors (docs/cache.md).
    std::array<int, 12> door_tiles{};               // DG:246e: three frames of four tiles
    std::array<int, 12> door_x{};                   // DG:247a: door d's half-cells in the area
    std::array<int, 12> door_y{};                   // DG:2486
    std::array<std::array<int, 12>, 3> door_items{};  // DG:2492, 249e, 24aa: by slot, the item it needs, plus 1
    // The cache's items: item i lies on three half-cells from (x, y), and goes in slot item_slot.
    std::array<int, 33> item_x{};                   // DG:21ce
    std::array<int, 33> item_y{};                   // DG:21f0
    std::array<int, 33> item_slot{};                // DG:2212: 0-2, also the kind (DG:2234)
    std::array<int, 18> cache_wall{};               // DG:20bc: six rows of three tiles (1b5d:01e9)
    std::array<int, 7> puzzle_cells{};              // DG:241e: the cells that solve the puzzle (1b5d:0980)

    // The shot with combat graphics (docs/combat.md 3.14).
    static constexpr int kPoseScriptBase = 0x3ef0;
    std::array<int, 0x108> pose_scripts{};   // DG:3ef0: the fire poses', kicks' and missiles' scripts
    std::array<int, 0x50> mech_poses{};      // DG:2d58: far pointers (offset, segment) by direction: fire, fire
                                             // (other sprites), kick, kick (other sprites), missile
    std::array<int, 0xf0> person_poses{};    // DG:3ff8: far pointers, 8 directions per weapon 0-0x0e
    static constexpr int kReactionScript = 0x2e3c, kExplosionScript = 0x41d8;
    std::array<int, 6> reaction_script{};    // DG:2e3c: the target's reaction to a hit
    std::array<int, 4> explosion_script{};   // DG:41d8
    static constexpr int kSpectatorScript = 0x4dd8;
    std::array<int, 3> spectator_script{};   // DG:4dd8: the Arena's spectator stands
    std::array<int, 22> encounter_templates{};  // DG:13e2: far pointers (offset, segment) to 'Mech templates
    std::array<int, 11> encounter_sprite_sets{};  // DG:140e: the sprite set for each
    std::array<int, 0x26> arena_wall{};      // DG:2b8e: map-store cells 0xc00 on, 0 = unchanged (1d43:0c72)
    std::array<int, 4> person_quarters{};    // DG:0372: a person's cell's tile bits, by parity (1000:0e4b)
    std::array<int, 0x30> muzzle{};          // DG:2d28: a line shot's start, x then y by direction (8 each):
                                             // a 'Mech, a 'Mech with the other sprites, a person
    std::array<int, 0x20> line_steps{};      // DG:41dc: by octant, the straight step's x and y, the diagonal's

    // The orders screen (docs/combat.md 2.6).
    std::array<int, 12> path_arrows{};       // DG:3b06: by (dy + 1) 4 + dx + 1, a shown path step's glyph

    // The 'Mech stats screen (15ab:1afe, docs/town_loop.md).
    std::array<int, 11> stats_bar_structure{};  // DG:1306: each bar's structure byte in the record (0: none)
    std::array<int, 11> stats_bar_x{};          // DG:131c: each bar's x
    std::array<int, 11> stats_bar_y{};          // DG:1332: each bar's bottom row
    std::array<int, 16> stats_palette{};        // DG:1348: the screen's colour numbers (EGA)
    std::array<int, 4> stats_pulse{};           // DG:1378: colour 4's number, step by step

    // The terrain outside the towns (docs/wilderness.md).
    std::array<int, 256> world_heights{};        // 2c6c:0b0b: one height per area, 16 x 16
    std::array<int, 256> area_maps{};            // 37e8:0030: each area's map number, 0 for none
    std::array<int, 22> encounter_weapons{};     // DG:2cf4: an encounter soldier's weapon by the sum of 7 (r & 3)
    std::array<int, 32> heal_dice{};             // DG:2602: healing by skill s (0-7) and care c (1-4) at 4s + c - 1: d6 count, x multiplier

    std::array<int, 8> foot_formation_x{};       // DG:3a16: the k-th character on foot's half-cells from the party in a fight
    std::array<int, 7> alley_companion_skills{}; // DG:1e7a: the alley companion's seven skills (19b8:104e)
    std::array<int, 5> alley_offset_x{};         // DG:1e82: the alley soldiers' half-cells from the base x, records 8-12
    std::array<int, 5> alley_offset_y{};         // DG:1e8a: and from the base y
    std::array<int, 8> foot_formation_y{};       // DG:3a1e
    std::array<int, 1024> terrain_templates{};   // 2c6c:0c1d: 16 blocks of 8x8, one per neighbour mask
    std::array<int, 128> store_tail{};           // 2c6c:201d: the block store's blocks 0x50-0x51, after a full map's 64
    std::array<int, 64> plain_block{};           // 2c6c:209d: the block every type-0x10 block is
    std::array<int, 32> mirror_east_west{};      // 2c6c:211d: a template byte with east and west swapped
    std::array<int, 32> mirror_north_south{};    // 2c6c:213d: with north and south swapped

    // The overhead map (docs/overhead_map.md), by map number - 1.
    std::array<std::array<int, 250>, 3> overhead_colours{};  // 2c6c:215d, 2257, 2351: by tile, a small block's
                                                             // cell colour: the land's, the cache levels 0 and 1
    std::array<int, 13> overhead_block_x{};       // DG:0a38: the map's first block column in its grid
    std::array<int, 13> overhead_block_y{};       // DG:0a46: its first block row
    std::array<int, 13> overhead_blocks_across{}; // DG:0a54
    std::array<int, 13> overhead_blocks_down{};   // DG:0a62
    std::array<int, 13> overhead_cell_bytes{};    // DG:0a70: how many bytes of cells it reads from the file
    std::array<int, 314> sound_lists{};           // DG:5008: the sound effects' entries (27c5:0002)
    std::array<int, 12> music_note_hz{};          // 284b:0121: the title music's pitches in its top octave, in Hz
    std::array<int, 8> overhead_pointer_column{}; // DG:0a8a: by direction, the pointer's text column
    std::array<int, 8> overhead_pointer_row{};    // DG:0a92: and its row

    bool operator==(const Tables&) const = default;
};

// Reads the tables from the program (the importer only).
Tables tables_from_original(const ProgramData& exe);
// tables.json's contents, and reading them back. Throws FormatError or
// JsonError for a file that is not of this shape.
std::string write_tables_file(const Tables& tables, int import_version);
Tables read_tables_file(const std::string& text);
Tables read_tables(const std::filesystem::path& data_dir);
std::filesystem::path tables_path(const std::filesystem::path& data_dir);

}  // namespace chr::formats
