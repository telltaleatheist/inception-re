#include "formats/exe_data.h"

#include <algorithm>
#include <array>
#include <cstdio>

#include "formats/cmp.h"  // FormatError
#include "formats/exepack.h"
#include "formats/game_files.h"
#include "formats/sha256.h"

namespace chr::formats {

namespace {

// DOS 1.03. DGROUP (the program's data segment) is 46db; the asm display
// library's data is 2c6c. Text pieces keep the program's control bytes
// (docs/text.md); several of them are not NUL-free, so each has its length.
constexpr std::array kDos103 = {
    // 2c6c:a661, 1,024 bytes: the 8x8 font, one bit per pixel, 8 bytes per
    // character, characters 0..127 (273d:00d5 masks with 0x7f).
    ExeSpan{ExeSpan::Kind::Font, "font", 0x2c6c, 0xa661, 0x400, "7578b312775492b4"},
    // The rule tables (formats/rules.h, docs/formats/mech.md), written to
    // rules/*.json. The eight 'Mech templates, 0x7d bytes each, that
    // 17dc:0d49 and the new game (24d3) copy into the 'Mech records:
    ExeSpan{ExeSpan::Kind::Table, "rules.mech_templates", 0x37e8, 0x02f0, 8 * 0x7d, "c47a46cb2699b7c5"},
    // The weapon table, 0x21 records of 0x11 bytes (22e8:000c, 1e31:0f24):
    ExeSpan{ExeSpan::Kind::Table, "rules.weapons", 0x46db, 0x2ed8, 0x21 * 0x11, "9be8135807109015"},
    // 22e8:000c's hit location table, DG:2e40 + 11 * side + 2d6 (4 x 11):
    ExeSpan{ExeSpan::Kind::Table, "rules.hit_locations", 0x46db, 0x2e42, 44, "08f02dbef3cd8116"},
    // 22e8:000c's cluster table, DG:2e5e + 7 * 2d6 + column (11 x 7):
    ExeSpan{ExeSpan::Kind::Table, "rules.clusters", 0x46db, 0x2e6e, 77, "404f8411018c76a3"},
    // The side hit by facing difference, DG:2d11 + (-7..7):
    ExeSpan{ExeSpan::Kind::Table, "rules.facing_sides", 0x46db, 0x2d0a, 15, "6cff70c3be8eb123"},
    // The to-hit modifier by the target's steps so far, DG:2d1a + 0..12:
    ExeSpan{ExeSpan::Kind::Table, "rules.target_movement", 0x46db, 0x2d1a, 13, "27f09a3f6b92627e"},
    // 17dc:0d49's armour cut by difficulty, DG:165a + 0..6:
    ExeSpan{ExeSpan::Kind::Table, "rules.difficulty_armour", 0x46db, 0x165a, 7, "a8d217a43d0fbae8"},
    // The personal armour types' points, DG:4ddb + 0..5 (1000:3a49, 15ab:1a8f, 24d3:0a3c):
    ExeSpan{ExeSpan::Kind::Table, "rules.personal_armour", 0x46db, 0x4ddb, 6, "50b413dca9156173"},
    // The shops' prices (24d3:09c7, 0a4b, 0ef3): armour by type (32-bit), an armour point by
    // type - 1, weapons by number - 1 (32-bit).
    ExeSpan{ExeSpan::Kind::Table, "rules.armour_prices", 0x46db, 0x4f26, 24, "27f58a769d8131df"},
    ExeSpan{ExeSpan::Kind::Table, "rules.armour_point_prices", 0x46db, 0x4f3e, 6, "11abfb70db858fa0"},
    ExeSpan{ExeSpan::Kind::Table, "rules.weapon_prices", 0x46db, 0x4f44, 44, "4e53bb99e5bd32d8"},
    // 1c67:08a8's ten pictured parts on BTSTATS.CMP: each one's point (x at
    // DG:28c4, y at DG:28d8, words) and its answer, a menu 0x25 item (DG:28ec):
    ExeSpan{ExeSpan::Kind::Table, "rules.quiz_part_x", 0x46db, 0x28c4, 20, "6ae32790932d22a7"},
    ExeSpan{ExeSpan::Kind::Table, "rules.quiz_part_y", 0x46db, 0x28d8, 20, "0568d0aff7d516d2"},
    // The small tables (formats/tables.h, docs/formats/tables.md), written
    // to tables.json:
    ExeSpan{ExeSpan::Kind::Table, "tables.palette", 0x37e8, 0x0000, 16, "52a1e16100dab1df"},
    ExeSpan{ExeSpan::Kind::Table, "tables.endmech_palette", 0x3858, 0x0000, 16, "53d8d7a73919f94d"},
    ExeSpan{ExeSpan::Kind::Table, "tables.windows", 0x385b, 0x0000, 176, "03e2e9d477f0f6cb"},
    ExeSpan{ExeSpan::Kind::Table, "tables.frame_pieces_0", 0x385b, 0x0320, 24, "78e19eea404b9cfc"},
    ExeSpan{ExeSpan::Kind::Table, "tables.frame_pieces_1", 0x385b, 0x0338, 24, "8c88f1d788589394"},
    ExeSpan{ExeSpan::Kind::Table, "tables.frame_pieces_2", 0x385b, 0x0350, 24, "e95da86b759effb4"},
    ExeSpan{ExeSpan::Kind::Table, "tables.frame_styles", 0x46db, 0x4fa4, 20, "fb772bc97755dadb"},
    ExeSpan{ExeSpan::Kind::Table, "tables.menus", 0x385b, 0x0092, 656, "573492a0ca90ef61"},
    ExeSpan{ExeSpan::Kind::Table, "tables.seen_bits", 0x46db, 0x029a, 8, "f2aeecc5b331bd1a"},
    ExeSpan{ExeSpan::Kind::Table, "tables.money_loss_mask", 0x46db, 0x02a2, 6, "3a85db76e54174a3"},
    ExeSpan{ExeSpan::Kind::Table, "tables.money_rate", 0x46db, 0x02a8, 6, "5e8fab143a15add9"},
    ExeSpan{ExeSpan::Kind::Table, "tables.animated_tiles", 0x46db, 0x04b0, 10, "0492a26176d515bf"},
    ExeSpan{ExeSpan::Kind::Table, "tables.walking_scripts", 0x37e8, 0x0270, 128, "92ce3a05c1bcbe5b"},
    ExeSpan{ExeSpan::Kind::Table, "tables.walking_keys", 0x46db, 0x0160, 16, "ec4160a4db744e94"},
    ExeSpan{ExeSpan::Kind::Table, "tables.formation_x", 0x46db, 0x0464, 16, "18c44063f315ce33"},
    ExeSpan{ExeSpan::Kind::Table, "tables.formation_y", 0x46db, 0x047c, 16, "3158edbd95873ac6"},
    ExeSpan{ExeSpan::Kind::Table, "tables.party_water", 0x46db, 0x0332, 64, "e4ff8c9892227501"},
    ExeSpan{ExeSpan::Kind::Table, "tables.place_pictures", 0x46db, 0x141a, 26, "710b0d54e3302449"},
    ExeSpan{ExeSpan::Kind::Table, "tables.picture_chance", 0x46db, 0x0ac4, 6, "52552f039304ed1d"},
    ExeSpan{ExeSpan::Kind::Table, "tables.direction_of", 0x2c6c, 0x0240, 16, "6bfee35147b42526"},
    ExeSpan{ExeSpan::Kind::Table, "tables.step_x", 0x46db, 0x311a, 16, "b829d92687b7ab07"},
    ExeSpan{ExeSpan::Kind::Table, "tables.step_y", 0x46db, 0x312a, 16, "969207e215e3b0aa"},
    ExeSpan{ExeSpan::Kind::Table, "tables.turns", 0x46db, 0x310a, 16, "42da2f1ae609f2ab"},
    ExeSpan{ExeSpan::Kind::Table, "tables.person_water", 0x46db, 0x032e, 4, "6a03e86ab54be375"},
    ExeSpan{ExeSpan::Kind::Table, "tables.mech_step_x", 0x46db, 0x045c, 8, "5b76c41e8baa8005"},
    ExeSpan{ExeSpan::Kind::Table, "tables.mech_step_y", 0x46db, 0x0474, 8, "8a4459acf78eabb8"},
    ExeSpan{ExeSpan::Kind::Table, "tables.marked_spot_x", 0x46db, 0x1632, 8, "0feee0909cfb969d"},
    ExeSpan{ExeSpan::Kind::Table, "tables.marked_spot_y", 0x46db, 0x163a, 8, "dc3398dd9d76b22e"},
    ExeSpan{ExeSpan::Kind::Table, "tables.mech_formation_x", 0x46db, 0x3a26, 4, "9565215f32d0de3b"},
    ExeSpan{ExeSpan::Kind::Table, "tables.mech_formation_y", 0x46db, 0x3a2a, 4, "e26174938af57d98"},
    ExeSpan{ExeSpan::Kind::Table, "tables.mech_cell", 0x46db, 0x02de, 8, "c8e0ac891029dadd"},
    ExeSpan{ExeSpan::Kind::Table, "tables.mech_water", 0x46db, 0x02e6, 8, "5de6f2a1d339d170"},
    ExeSpan{ExeSpan::Kind::Table, "tables.mech_cell_odd_x", 0x46db, 0x02ee, 8, "578ce22e73619f9d"},
    ExeSpan{ExeSpan::Kind::Table, "tables.mech_cell_odd_y", 0x46db, 0x02f6, 8, "3504323d21b3b66d"},
    ExeSpan{ExeSpan::Kind::Table, "tables.enemy_offset_x", 0x46db, 0x1642, 12, "a5ddfab990b465b7"},
    ExeSpan{ExeSpan::Kind::Table, "tables.enemy_offset_y", 0x46db, 0x164e, 12, "d83ed98840f7cb42"},
    ExeSpan{ExeSpan::Kind::Table, "tables.los_step_x", 0x46db, 0x328a, 16, "b829d92687b7ab07"},
    ExeSpan{ExeSpan::Kind::Table, "tables.los_step_y", 0x46db, 0x329a, 16, "969207e215e3b0aa"},
    ExeSpan{ExeSpan::Kind::Table, "tables.los_area_x", 0x46db, 0x32aa, 16, "076c889bc05cfd4e"},
    ExeSpan{ExeSpan::Kind::Table, "tables.los_area_y", 0x46db, 0x32ba, 16, "8ac265f2824ba3d6"},
    ExeSpan{ExeSpan::Kind::Table, "tables.los_row", 0x46db, 0x32ca, 16, "7671f41812f6a168"},
    ExeSpan{ExeSpan::Kind::Table, "tables.step_direction", 0x46db, 0x2ecc, 11, "1d25344c26c3f072"},
    ExeSpan{ExeSpan::Kind::Table, "tables.path_quarters", 0x46db, 0x3b12, 4, "6a03e86ab54be375"},
    ExeSpan{ExeSpan::Kind::Table, "tables.leg_actuator_bit", 0x46db, 0x316a, 8, "3fc60db5dc27bf82"},
    ExeSpan{ExeSpan::Kind::Table, "tables.leg_actuator_keep", 0x46db, 0x3172, 8, "2fa2afa16b05680e"},
    ExeSpan{ExeSpan::Kind::Table, "tables.arm_actuator_bit", 0x46db, 0x317a, 8, "f2da1742dfa01895"},
    ExeSpan{ExeSpan::Kind::Table, "tables.arm_actuator_keep", 0x46db, 0x3182, 8, "25a134f2663c725d"},
    ExeSpan{ExeSpan::Kind::Table, "tables.door_tiles", 0x46db, 0x246e, 12, "c0b1dd497d669ea0"},
    ExeSpan{ExeSpan::Kind::Table, "tables.door_x", 0x46db, 0x247a, 12, "1c5e450adcfa8c68"},
    ExeSpan{ExeSpan::Kind::Table, "tables.door_y", 0x46db, 0x2486, 12, "48ef9e1f1d1ffea0"},
    ExeSpan{ExeSpan::Kind::Table, "tables.door_item_0", 0x46db, 0x2492, 12, "0da498ea1ea95b7a"},
    ExeSpan{ExeSpan::Kind::Table, "tables.door_item_1", 0x46db, 0x249e, 12, "ef793f3443b3e683"},
    ExeSpan{ExeSpan::Kind::Table, "tables.door_item_2", 0x46db, 0x24aa, 12, "3ded72c68ec8857e"},
    ExeSpan{ExeSpan::Kind::Table, "tables.item_x", 0x46db, 0x21ce, 33, "e14fbe5eb92b5bd3"},
    ExeSpan{ExeSpan::Kind::Table, "tables.item_y", 0x46db, 0x21f0, 33, "5e90f840ae6e32af"},
    ExeSpan{ExeSpan::Kind::Table, "tables.item_slot", 0x46db, 0x2212, 33, "d9d6c2d044187b62"},
    ExeSpan{ExeSpan::Kind::Table, "tables.cache_wall", 0x46db, 0x20bc, 18, "05201a56b0dad16b"},
    ExeSpan{ExeSpan::Kind::Table, "tables.puzzle_cells", 0x46db, 0x241e, 14, "ac9deaddb7a7d2e8"},
    ExeSpan{ExeSpan::Kind::Table, "tables.location_slot_first", 0x46db, 0x318a, 8, "9fbd883337f64bcc"},
    ExeSpan{ExeSpan::Kind::Table, "tables.location_slot_count", 0x46db, 0x3192, 8, "79569a0bc1d7d49c"},
    // 22e8:12c7, the shot with combat graphics: the poses' scripts and their
    // far pointers by direction, the hit reaction, the explosion; 1000:0e4b's
    // quarter bits for a person's cell:
    ExeSpan{ExeSpan::Kind::Table, "tables.pose_scripts", 0x46db, 0x3ef0, 0x108, "e3468f75cda6a54c"},
    ExeSpan{ExeSpan::Kind::Table, "tables.mech_poses", 0x46db, 0x2d58, 0xa0, "f103ff439e3bcf16"},
    ExeSpan{ExeSpan::Kind::Table, "tables.person_poses", 0x46db, 0x3ff8, 0x1e0, "d98f3f8f3c91bd01"},
    ExeSpan{ExeSpan::Kind::Table, "tables.reaction_script", 0x46db, 0x2e3c, 6, "4bced400051d4aa7"},
    ExeSpan{ExeSpan::Kind::Table, "tables.explosion_script", 0x46db, 0x41d8, 4, "c9772bef4d4be38f"},
    ExeSpan{ExeSpan::Kind::Table, "tables.person_quarters", 0x46db, 0x0372, 8, "7a1284cf4c84ce54"},
    ExeSpan{ExeSpan::Kind::Table, "tables.muzzle", 0x46db, 0x2d28, 0x30, "e5f4d337ac08ca39"},
    ExeSpan{ExeSpan::Kind::Table, "tables.line_steps", 0x46db, 0x41dc, 0x20, "b5d668490f6227c0"},
    // The Arena (17dc:0d49's bit-0x80 branch, 22e8:12c7, 1d43:0c72):
    ExeSpan{ExeSpan::Kind::Table, "tables.encounter_templates", 0x46db, 0x13e2, 44, "1aebfdba712bd0c0"},
    ExeSpan{ExeSpan::Kind::Table, "tables.encounter_sprite_sets", 0x46db, 0x140e, 11, "653680474c132640"},
    ExeSpan{ExeSpan::Kind::Table, "tables.spectator_script", 0x46db, 0x4dd8, 3, "e204de4f40ba3515"},
    ExeSpan{ExeSpan::Kind::Table, "tables.arena_wall", 0x46db, 0x2b8e, 0x26, "410a59f69b3795fe"},
    ExeSpan{ExeSpan::Kind::Table, "tables.path_arrows", 0x46db, 0x3b06, 12, "a9eff4005e9a70e1"},
    // The terrain outside the towns (docs/wilderness.md):
    ExeSpan{ExeSpan::Kind::Table, "tables.world_heights", 0x2c6c, 0x0b0b, 256, "1ace38e0c5828f12"},
    ExeSpan{ExeSpan::Kind::Table, "tables.area_maps", 0x37e8, 0x0030, 256, "c4fa392e7eb70166"},
    // The random encounter (docs/combat.md 2.2, 2.3):
    ExeSpan{ExeSpan::Kind::Table, "tables.encounter_weapons", 0x46db, 0x2cf4, 22, "e715fbca48ead8b7"},
    ExeSpan{ExeSpan::Kind::Table, "tables.heal_dice", 0x46db, 0x2602, 32, "cab6c5404e723ff5"},
    ExeSpan{ExeSpan::Kind::Table, "tables.foot_formation_x", 0x46db, 0x3a16, 8, "7edd3039cea710f9"},
    // 19b8:104e's tables (docs/combat.md 1.6): the companion's skills, then
    // the alley soldiers' places (read from DG:1e7a + i and DG:1e82 + i, i 8-12).
    ExeSpan{ExeSpan::Kind::Table, "tables.alley_companion_skills", 0x46db, 0x1e7a, 7, "8e861ba75d1288db"},
    ExeSpan{ExeSpan::Kind::Table, "tables.alley_offset_x", 0x46db, 0x1e82, 5, "9c8526f0819fb2ba"},
    ExeSpan{ExeSpan::Kind::Table, "tables.alley_offset_y", 0x46db, 0x1e8a, 5, "769b52acf79c621b"},
    ExeSpan{ExeSpan::Kind::Table, "tables.foot_formation_y", 0x46db, 0x3a1e, 8, "54a0cc86c63b8864"},
    ExeSpan{ExeSpan::Kind::Table, "tables.terrain_templates", 0x2c6c, 0x0c1d, 1024, "c5cddf08c0924d94"},
    ExeSpan{ExeSpan::Kind::Table, "tables.plain_block", 0x2c6c, 0x209d, 64, "e3a878f25dc4ae8e"},
    ExeSpan{ExeSpan::Kind::Table, "tables.store_tail", 0x2c6c, 0x201d, 128, "5f55b86a6b86f94a"},
    ExeSpan{ExeSpan::Kind::Table, "tables.mirror_east_west", 0x2c6c, 0x211d, 32, "513e0371b0a58371"},
    ExeSpan{ExeSpan::Kind::Table, "tables.mirror_north_south", 0x2c6c, 0x213d, 32, "d81bed15e8cec54a"},
    // The overhead map (1000:3fae, docs/overhead_map.md).
    ExeSpan{ExeSpan::Kind::Table, "tables.overhead_colours", 0x2c6c, 0x215d, 3 * 250, "f7439b9b84149840"},
    ExeSpan{ExeSpan::Kind::Table, "tables.overhead_block_x", 0x46db, 0x0a38, 13, "dd46c3eebb1884ff"},
    ExeSpan{ExeSpan::Kind::Table, "tables.overhead_block_y", 0x46db, 0x0a46, 13, "dd46c3eebb1884ff"},
    ExeSpan{ExeSpan::Kind::Table, "tables.overhead_blocks_across", 0x46db, 0x0a54, 13, "981ae324fe741102"},
    ExeSpan{ExeSpan::Kind::Table, "tables.overhead_blocks_down", 0x46db, 0x0a62, 13, "981ae324fe741102"},
    ExeSpan{ExeSpan::Kind::Table, "tables.overhead_cell_bytes", 0x46db, 0x0a70, 26, "8cd7c10ecfb54713"},
    ExeSpan{ExeSpan::Kind::Table, "tables.overhead_pointer_column", 0x46db, 0x0a8a, 8, "55db8edf1181cf86"},
    ExeSpan{ExeSpan::Kind::Table, "tables.overhead_pointer_row", 0x46db, 0x0a92, 8, "39512a0507aaf0cf"},
    ExeSpan{ExeSpan::Kind::Table, "tables.sound_lists", 0x46db, 0x5008, 628, "547c0c4ed891a257"},
    ExeSpan{ExeSpan::Kind::Table, "rules.quiz_answers", 0x46db, 0x28ec, 20, "3fe5abf0257c084c"},
    // Text the game shows. The names are the text IDs (docs/text.md): a
    // text provider hands them out, and an override file can replace them.
    // 1000:1c12, the step check:
    ExeSpan{ExeSpan::Kind::Text, "walk.too_deep", 0x46db, 0x0410, 32, "703f071516842f90"},
    ExeSpan{ExeSpan::Kind::Text, "walk.enter_question", 0x46db, 0x0430, 20, "0b636a56d39eba45"},  // then the place's name
    ExeSpan{ExeSpan::Kind::Text, "walk.enter_question_end", 0x46db, 0x0444, 2, "b813d21bdb2ae744"},
    // ... and its copies for a party in its 'Mechs:
    ExeSpan{ExeSpan::Kind::Text, "walk.mech_enter_question", 0x46db, 0x0446, 20, "0b636a56d39eba45"},
    ExeSpan{ExeSpan::Kind::Text, "walk.mech_enter_question_end", 0x46db, 0x045a, 2, "b813d21bdb2ae744"},
    // 24d3:0187, special 2, after the course:
    ExeSpan{ExeSpan::Kind::Text, "course.result_locust", 0x46db, 0x48b3, 44, "b005d2c9405c5065"},
    ExeSpan{ExeSpan::Kind::Text, "course.result_wasp", 0x46db, 0x48df, 5, "5703a9c91b94e70f"},
    ExeSpan{ExeSpan::Kind::Text, "course.result_chameleon", 0x46db, 0x48e4, 10, "0210015c1105d3dd"},
    ExeSpan{ExeSpan::Kind::Text, "course.result_other", 0x46db, 0x48ee, 44, "24053ec7ddbb0f41"},
    // 1000:00bb, the main loop: after a walking key it writes the direction
    // in the upper panel box (pointers at DGROUP:01aa, in the order of the
    // keys at DGROUP:0160). Each starts with a new line.
    ExeSpan{ExeSpan::Kind::Text, "walk.direction.north", 0x46db, 0x006c, 7, "d7531083f7293f66"},
    ExeSpan{ExeSpan::Kind::Text, "walk.direction.northeast", 0x46db, 0x0073, 11, "45956e606cdd9f7b"},
    ExeSpan{ExeSpan::Kind::Text, "walk.direction.east", 0x46db, 0x007e, 6, "69b0782392a9fd66"},
    ExeSpan{ExeSpan::Kind::Text, "walk.direction.southeast", 0x46db, 0x0084, 11, "3f5775c2672a1219"},
    ExeSpan{ExeSpan::Kind::Text, "walk.direction.south", 0x46db, 0x008f, 7, "1c50306e6aea7e0b"},
    ExeSpan{ExeSpan::Kind::Text, "walk.direction.southwest", 0x46db, 0x0096, 11, "9fb7d4c4637426a5"},
    ExeSpan{ExeSpan::Kind::Text, "walk.direction.west", 0x46db, 0x00a1, 6, "7e0d8ba62a5a8baa"},
    ExeSpan{ExeSpan::Kind::Text, "walk.direction.northwest", 0x46db, 0x00a7, 11, "f7ec95a44795bfc9"},
    // 1000:1a13, the yes/no answer line, with Yes or with No picked:
    ExeSpan{ExeSpan::Kind::Text, "ui.answer_yes_picked", 0x46db, 0x03ee, 16, "ab37bd17e5401171"},
    ExeSpan{ExeSpan::Kind::Text, "ui.press_key", 0x46db, 0x050d, 14, "787e88186eb252b3"},  // 1000:2a4f
    ExeSpan{ExeSpan::Kind::Text, "over.leader.0", 0x46db, 0x00f1, 6, "99c40d29b9ee0f3b"},  // 1000:04bc
    ExeSpan{ExeSpan::Kind::Text, "over.leader.1", 0x46db, 0x00f7, 4, "fd2ca94a5886fb2b"},
    ExeSpan{ExeSpan::Kind::Text, "over.killed", 0x46db, 0x00fb, 52, "573910d52bc952e9"},
    ExeSpan{ExeSpan::Kind::Text, "over.new_game", 0x46db, 0x012f, 27, "febca21abb92bace"},
    ExeSpan{ExeSpan::Kind::Text, "ui.plural_s", 0x46db, 0x051b, 2, "28a3d6d537e5d509"},    // 1000:2a69
    ExeSpan{ExeSpan::Kind::Text, "ui.full_stop", 0x46db, 0x051d, 2, "075d3ddf5a3a826e"},   // 1000:2a7e
    ExeSpan{ExeSpan::Kind::Text, "ui.answer_no_picked", 0x46db, 0x03fe, 18, "b0ed7f20a54135c7"},
    // 1000:4cac, the heading over the party box's three bar columns:
    ExeSpan{ExeSpan::Kind::Text, "panel.party_heading", 0x46db, 0x0aca, 4, "1b1faf746e0d0bb8"},
    ExeSpan{ExeSpan::Kind::Text, "panel.cbills_label", 0x46db, 0x32da, 12, "e4fc11a2b9c6240f"},
    // The eight characters' names (pointers at DGROUP:01ca):
    ExeSpan{ExeSpan::Kind::Text, "character.0", 0x46db, 0x00b2, 6, "99c40d29b9ee0f3b"},
    ExeSpan{ExeSpan::Kind::Text, "character.1", 0x46db, 0x00b8, 4, "fd2ca94a5886fb2b"},
    ExeSpan{ExeSpan::Kind::Text, "character.2", 0x46db, 0x00bc, 7, "7ac12388c36db677"},
    ExeSpan{ExeSpan::Kind::Text, "character.3", 0x46db, 0x00c3, 5, "b75a3ab3009f854c"},
    ExeSpan{ExeSpan::Kind::Text, "character.4", 0x46db, 0x00c8, 5, "466cc126070bdf0a"},
    ExeSpan{ExeSpan::Kind::Text, "character.5", 0x46db, 0x00cd, 5, "ad5ead78b19a5e68"},
    ExeSpan{ExeSpan::Kind::Text, "character.6", 0x46db, 0x00d2, 7, "18fc56153b22ba4f"},
    ExeSpan{ExeSpan::Kind::Text, "character.7", 0x46db, 0x00d9, 6, "7dad5b3f04a6e708"},
    // The places' special actions (docs/scripts.md). The combat class
    // (24d3:02a5): the transcript, the five levels (pointers at DGROUP:4ea2)
    // and the three subjects (4eb6).
    ExeSpan{ExeSpan::Kind::Text, "course.transcript", 0x46db, 0x491a, 46, "7d754d35e7881ff4"},
    ExeSpan{ExeSpan::Kind::Text, "course.level.0", 0x46db, 0x46dc, 21, "a4284c2077adef7d"},
    ExeSpan{ExeSpan::Kind::Text, "course.level.1", 0x46db, 0x46f1, 11, "507123d5deaf1276"},
    ExeSpan{ExeSpan::Kind::Text, "course.level.2", 0x46db, 0x46fc, 10, "71d83aa7aba43c48"},
    ExeSpan{ExeSpan::Kind::Text, "course.level.3", 0x46db, 0x4706, 7, "17836d59111081d0"},
    ExeSpan{ExeSpan::Kind::Text, "course.level.4", 0x46db, 0x470d, 15, "e27c123f7a363aac"},
    ExeSpan{ExeSpan::Kind::Text, "course.subject.0", 0x46db, 0x471c, 34, "571a9d8e2e572545"},
    ExeSpan{ExeSpan::Kind::Text, "course.subject.1", 0x46db, 0x473e, 42, "ab3b223dad286e9a"},
    ExeSpan{ExeSpan::Kind::Text, "course.subject.2", 0x46db, 0x4768, 34, "49dc40050a7033a2"},
    ExeSpan{ExeSpan::Kind::Text, "course.maxed", 0x46db, 0x4948, 81, "69272b4a699d6c45"},
    ExeSpan{ExeSpan::Kind::Text, "course.no_money", 0x46db, 0x4999, 58, "8f7e9188a67561a3"},
    ExeSpan{ExeSpan::Kind::Text, "course.no_money_end", 0x46db, 0x4f7e, 34, "5f7733440b4932eb"},
    // The start-up (1000:50c8): the copyright notice and the first-time
    // question, then, after "No", the question about a saved game.
    ExeSpan{ExeSpan::Kind::Text, "intro.notice.0", 0x46db, 0x0b0e, 62, "08626be799a5a4da"},
    ExeSpan{ExeSpan::Kind::Text, "intro.notice.1", 0x46db, 0x0b4c, 68, "e8bb6df2d0c4c5d4"},
    ExeSpan{ExeSpan::Kind::Text, "intro.notice.2", 0x46db, 0x0b90, 82, "5172319c36430c8b"},
    ExeSpan{ExeSpan::Kind::Text, "intro.notice.3", 0x46db, 0x0be2, 43, "4b63835bd8295376"},
    ExeSpan{ExeSpan::Kind::Text, "intro.first_time", 0x46db, 0x0c0d, 44, "29640df170975cbc"},
    ExeSpan{ExeSpan::Kind::Text, "intro.load_old_game", 0x46db, 0x0c39, 95, "bb1c956763c88c90"},
    // Before VIEWDISK after the garage (17dc:0008 at 1000:7ec2, 7ecf).
    ExeSpan{ExeSpan::Kind::Text, "place.follow_on.0", 0x46db, 0x1384, 50, "83e880e5ab13237c"},
    ExeSpan{ExeSpan::Kind::Text, "place.follow_on.1", 0x46db, 0x13b6, 44, "50a0a217ac94a2a3"},
    // Talking to the townspeople inside (17dc:17b9); the reasons are by
    // place (pointers at DGROUP:4e32).
    ExeSpan{ExeSpan::Kind::Text, "talk.nobody", 0x46db, 0x16de, 48, "b1478517be074391"},
    ExeSpan{ExeSpan::Kind::Text, "talk.you_see", 0x46db, 0x170e, 26, "df573cb8c9e8451d"},
    ExeSpan{ExeSpan::Kind::Text, "talk.done", 0x46db, 0x1728, 5, "8882688380a5a2bc"},
    ExeSpan{ExeSpan::Kind::Text, "talk.hello", 0x46db, 0x172d, 52, "4880859f54f2aeaf"},
    ExeSpan{ExeSpan::Kind::Text, "talk.going_to", 0x46db, 0x1761, 46, "f1e8b5379d3cdf5b"},
    ExeSpan{ExeSpan::Kind::Text, "talk.end", 0x46db, 0x178f, 2, "075d3ddf5a3a826e"},
    ExeSpan{ExeSpan::Kind::Text, "talk.line_end", 0x46db, 0x4fa0, 2, "f566cc6fccc65736"},
    ExeSpan{ExeSpan::Kind::Text, "talk.reason.0", 0x46db, 0x4375, 89, "5b62eec9561ca397"},
    ExeSpan{ExeSpan::Kind::Text, "talk.reason.1", 0x46db, 0x43ce, 51, "6df0a1fed92177cd"},
    ExeSpan{ExeSpan::Kind::Text, "talk.reason.2", 0x46db, 0x4401, 78, "a67a18a1a059e96c"},
    ExeSpan{ExeSpan::Kind::Text, "talk.reason.3", 0x46db, 0x444f, 18, "a91efcd61de00395"},
    ExeSpan{ExeSpan::Kind::Text, "talk.reason.4", 0x46db, 0x4461, 32, "8ca30ba929f0697f"},
    ExeSpan{ExeSpan::Kind::Text, "talk.reason.5", 0x46db, 0x4481, 56, "18d712307201b474"},
    ExeSpan{ExeSpan::Kind::Text, "talk.reason.6", 0x46db, 0x44b9, 22, "4a9e2320a44e4cad"},
    ExeSpan{ExeSpan::Kind::Text, "talk.reason.7", 0x46db, 0x44cf, 8, "fbc1fcd429c9b81f"},
    ExeSpan{ExeSpan::Kind::Text, "talk.reason.8", 0x46db, 0x44d7, 1, "6e340b9cffb37a98"},
    ExeSpan{ExeSpan::Kind::Text, "talk.reason.9", 0x46db, 0x44d8, 14, "f4fe185aeca04164"},
    ExeSpan{ExeSpan::Kind::Text, "talk.reason.10", 0x46db, 0x44e6, 30, "a7770df18edb0d2e"},
    ExeSpan{ExeSpan::Kind::Text, "talk.reason.11", 0x46db, 0x4504, 29, "804bfd9c1129a64c"},
    // Combat (docs/combat.md), by the function that prints them. Pieces that
    // a function strings together have their own IDs; a translation keeps
    // the pieces' order, or overrides them all.
    // 15ab:0002, repairs from the wrecks and scrap money:
    ExeSpan{ExeSpan::Kind::Text, "after.repair.break", 0x46db, 0x0efb, 3, "2c25ea9939bc6b51"},
    ExeSpan{ExeSpan::Kind::Text, "after.repair.scavenges", 0x46db, 0x0efe, 42, "14775e6991935489"},
    ExeSpan{ExeSpan::Kind::Text, "after.repair.from_wrecks", 0x46db, 0x0f28, 24, "b9938f16e329f0e1"},
    ExeSpan{ExeSpan::Kind::Text, "after.repair.skill.2", 0x46db, 0x0eae, 16, "df69a45a30ba8d2e"},
    ExeSpan{ExeSpan::Kind::Text, "after.repair.skill.3", 0x46db, 0x0ebe, 25, "bdaefe058c814e8a"},
    ExeSpan{ExeSpan::Kind::Text, "after.repair.skill.4", 0x46db, 0x0ed7, 36, "4b8d54e90e2f69dc"},
    ExeSpan{ExeSpan::Kind::Text, "after.scrap.found", 0x46db, 0x0f40, 37, "1e8072f777cfeee1"},
    ExeSpan{ExeSpan::Kind::Text, "after.scrap.cbills", 0x46db, 0x0f65, 57, "a172456cce291662"},
    ExeSpan{ExeSpan::Kind::Text, "after.scrap.plural", 0x46db, 0x0f9e, 2, "28a3d6d537e5d509"},
    ExeSpan{ExeSpan::Kind::Text, "after.scrap.end", 0x46db, 0x0fa0, 2, "075d3ddf5a3a826e"},
    // 15ab:04f9, salvaging a wreck for a pilot without a 'Mech:
    ExeSpan{ExeSpan::Kind::Text, "after.salvage.question", 0x46db, 0x0fae, 81, "9da5d0277b02cd0c"},
    ExeSpan{ExeSpan::Kind::Text, "after.salvage.inspecting", 0x46db, 0x0fff, 30, "5d76884b17589341"},
    ExeSpan{ExeSpan::Kind::Text, "after.salvage.salvaging", 0x46db, 0x101d, 18, "4cc603c91c59f6fc"},
    ExeSpan{ExeSpan::Kind::Text, "after.salvage.no_chance", 0x46db, 0x102f, 29, "48b35dc2fed6d882"},
    ExeSpan{ExeSpan::Kind::Text, "after.salvage.none_left", 0x46db, 0x104c, 57, "6a72cce7dd0e4c0f"},
    // 15ab:094b, looting the beaten soldiers:
    ExeSpan{ExeSpan::Kind::Text, "after.loot.pockets", 0x46db, 0x1085, 48, "08b5e997e2848128"},
    ExeSpan{ExeSpan::Kind::Text, "after.loot.cash", 0x46db, 0x10b5, 17, "efddb2cb3b952466"},
    ExeSpan{ExeSpan::Kind::Text, "after.loot.weapons", 0x46db, 0x10c6, 31, "648ede576a6e2588"},
    ExeSpan{ExeSpan::Kind::Text, "after.loot.swap_question", 0x46db, 0x10e5, 26, "1f7bc5f8685bafc3"},
    ExeSpan{ExeSpan::Kind::Text, "after.loot.swap_for", 0x46db, 0x10ff, 20, "003df4936e11d65f"},
    ExeSpan{ExeSpan::Kind::Text, "after.loot.swap_end", 0x46db, 0x1113, 2, "b813d21bdb2ae744"},
    // 15ab:0b5e:
    ExeSpan{ExeSpan::Kind::Text, "fight.computer_question", 0x46db, 0x1115, 43, "7fd83444f555f9ac"},
    // 15ab:1467, the Scan unit browser, and 15ab:18e8, the unit panel:
    ExeSpan{ExeSpan::Kind::Text, "scan.enemy", 0x46db, 0x1148, 7, "072baf8a208a40ad"},
    ExeSpan{ExeSpan::Kind::Text, "scan.enemy_human", 0x46db, 0x114f, 12, "70ea76caa291f7e4"},
    ExeSpan{ExeSpan::Kind::Text, "scan.weapon", 0x46db, 0x115b, 11, "2fc53e3517418035"},
    ExeSpan{ExeSpan::Kind::Text, "scan.direction", 0x46db, 0x1166, 14, "6f111d3e7ad415ce"},
    ExeSpan{ExeSpan::Kind::Text, "scan.menu", 0x46db, 0x1174, 21, "d4ce9b27b5bccf45"},
    ExeSpan{ExeSpan::Kind::Text, "scan.detail", 0x46db, 0x1189, 12, "e39daf232be1ff4f"},
    ExeSpan{ExeSpan::Kind::Text, "scan.done", 0x46db, 0x1195, 5, "8882688380a5a2bc"},
    ExeSpan{ExeSpan::Kind::Text, "unit.possessive", 0x46db, 0x121e, 4, "0b9ccc53c4f46036"},
    ExeSpan{ExeSpan::Kind::Text, "unit.suspicious", 0x46db, 0x1222, 35, "f5be4189bbf43ba0"},
    ExeSpan{ExeSpan::Kind::Text, "unit.weapon_armour", 0x46db, 0x1245, 18, "7abfec3ed2c06183"},
    ExeSpan{ExeSpan::Kind::Text, "unit.ruined", 0x46db, 0x1257, 9, "78b7eeff36c60f0e"},
    ExeSpan{ExeSpan::Kind::Text, "unit.none", 0x46db, 0x1260, 5, "a51ad9ee4c1a4600"},
    // The Space command menu (1000:2c50) and its crew item (1000:4d57).
    ExeSpan{ExeSpan::Kind::Text, "space.items.0", 0x46db, 0x051f, 36, "6540d259539720ac"},
    ExeSpan{ExeSpan::Kind::Text, "space.items.allocate", 0x46db, 0x0543, 24, "dcea796194bb7cec"},
    ExeSpan{ExeSpan::Kind::Text, "space.items.1", 0x46db, 0x055b, 55, "f9f795eb515149ec"},
    ExeSpan{ExeSpan::Kind::Text, "space.items.2", 0x46db, 0x0592, 19, "c4b9c5eb1e2a2b05"},
    // 1000:378d, the Space menu's inspect item:
    ExeSpan{ExeSpan::Kind::Text, "inspect.mech_or_person", 0x46db, 0x06ff, 67, "a48e200cfb93495b"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.which_mech", 0x46db, 0x0742, 22, "c6a4db2848842b37"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.which_character", 0x46db, 0x0758, 26, "f6ef4380afde15ab"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.labels", 0x46db, 0x0772, 49, "6c983ae5be23fb67"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.labels_2", 0x46db, 0x07a3, 92, "eef7662f5d49d61d"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.uneasy", 0x46db, 0x07ff, 58, "38043df56b685966"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.armour", 0x46db, 0x0839, 7, "2c692d3755249e15"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.armour_whole", 0x46db, 0x0840, 22, "d1cde32027e45982"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.armour_lost", 0x46db, 0x0856, 10, "22dae68c94fae685"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.armour_all", 0x46db, 0x0860, 4, "6cc99cd24a91be5c"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.armour_of", 0x46db, 0x0864, 9, "a180311f708fce5c"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.armour_points", 0x46db, 0x086d, 20, "a91cbc83b9b4d15c"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.medkit_yes", 0x46db, 0x0881, 4, "03cb5fb3dd8f56aa"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.medkit_no", 0x46db, 0x0885, 3, "511a5d61e579e32d"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.mapper_yes", 0x46db, 0x0888, 4, "03cb5fb3dd8f56aa"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.mapper_no", 0x46db, 0x088c, 3, "511a5d61e579e32d"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.surgery_kit_yes", 0x46db, 0x088f, 4, "03cb5fb3dd8f56aa"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.surgery_kit_no", 0x46db, 0x0893, 3, "511a5d61e579e32d"},
    // ... and its skill levels (pointers at DGROUP:0194):
    ExeSpan{ExeSpan::Kind::Text, "inspect.skill_level.0", 0x46db, 0x0042, 10, "4a3ad5735b22bd20"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.skill_level.1", 0x46db, 0x004c, 8, "22a656f89e9f9b39"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.skill_level.2", 0x46db, 0x0054, 9, "adc60c26828b7024"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.skill_level.3", 0x46db, 0x005d, 5, "b4d57e976bb4c3d8"},
    ExeSpan{ExeSpan::Kind::Text, "inspect.skill_level.4", 0x46db, 0x0062, 10, "71f2a50ccc7456c1"},
    ExeSpan{ExeSpan::Kind::Text, "settings.title", 0x46db, 0x0896, 51, "cb35709d33532b49"},
    ExeSpan{ExeSpan::Kind::Text, "settings.sound_on", 0x46db, 0x08c9, 3, "05449fa81cf5e5da"},
    ExeSpan{ExeSpan::Kind::Text, "settings.sound_off", 0x46db, 0x08cc, 2, "481daf8515fdae90"},
    ExeSpan{ExeSpan::Kind::Text, "settings.items", 0x46db, 0x08ce, 43, "52501f024d3da1c9"},
    ExeSpan{ExeSpan::Kind::Text, "settings.steps", 0x46db, 0x08f9, 54, "1e93e7d9646d5fdf"},
    ExeSpan{ExeSpan::Kind::Text, "settings.speed", 0x46db, 0x092f, 46, "ff873fed525c718e"},
    ExeSpan{ExeSpan::Kind::Text, "settings.pictures", 0x46db, 0x095d, 54, "8f3704b50511a77c"},
    ExeSpan{ExeSpan::Kind::Text, "settings.quit", 0x46db, 0x0993, 60, "2b4ef6b4e62d67a1"},
    ExeSpan{ExeSpan::Kind::Text, "settings.quit_2", 0x46db, 0x09cf, 31, "fe197a33d38c0cbc"},
    ExeSpan{ExeSpan::Kind::Text, "space.no_dismount", 0x46db, 0x0ace, 64, "42834e59cec1702f"},
    ExeSpan{ExeSpan::Kind::Text, "save.which", 0x46db, 0x0673, 46, "adfebfe6441c4bf9"},
    ExeSpan{ExeSpan::Kind::Text, "save.failed", 0x46db, 0x06ad, 37, "b90cd8aee2599445"},
    ExeSpan{ExeSpan::Kind::Text, "save.refused", 0x46db, 0x06d2, 45, "cfdb78a8c37a5ef3"},
    ExeSpan{ExeSpan::Kind::Text, "load.which", 0x46db, 0x05c7, 46, "3460f6cb03942545"},
    ExeSpan{ExeSpan::Kind::Text, "load.not_a_save", 0x46db, 0x0601, 63, "7defba35cc91348d"},
    ExeSpan{ExeSpan::Kind::Text, "load.not_found", 0x46db, 0x0640, 24, "951595f967c2dc19"},
    ExeSpan{ExeSpan::Kind::Text, "load.not_found_end", 0x46db, 0x0658, 27, "b1e0b75b1eb829e5"},
    // 1000:3fae, the overhead map's caption: outdoors, and anywhere else.
    ExeSpan{ExeSpan::Kind::Text, "overhead.caption_outdoors", 0x46db, 0x09f7, 31, "e31d960c9a387663"},
    ExeSpan{ExeSpan::Kind::Text, "overhead.caption", 0x46db, 0x0a16, 33, "b1dcc12c76193b9f"},
    // The crew screen (1c67:0002).
    ExeSpan{ExeSpan::Kind::Text, "crew.heading", 0x46db, 0x2622, 33, "616ca99f3ce05c91"},
    ExeSpan{ExeSpan::Kind::Text, "crew.on_foot", 0x46db, 0x2643, 10, "c31036879c6c8682"},
    ExeSpan{ExeSpan::Kind::Text, "crew.pilot", 0x46db, 0x264d, 8, "e22a3b85e4c3308d"},
    ExeSpan{ExeSpan::Kind::Text, "crew.rider", 0x46db, 0x2655, 8, "d3dc5a95b6d0d811"},
    ExeSpan{ExeSpan::Kind::Text, "crew.done", 0x46db, 0x265d, 6, "17685db9e6c0081e"},
    ExeSpan{ExeSpan::Kind::Text, "crew.pilot_label", 0x46db, 0x2663, 13, "7e22397a10ce77c2"},
    ExeSpan{ExeSpan::Kind::Text, "crew.no_pilot", 0x46db, 0x2670, 5, "a51ad9ee4c1a4600"},
    ExeSpan{ExeSpan::Kind::Text, "crew.rider_label", 0x46db, 0x2675, 13, "f983c371a383e46b"},
    ExeSpan{ExeSpan::Kind::Text, "crew.no_rider", 0x46db, 0x2682, 5, "a51ad9ee4c1a4600"},
    ExeSpan{ExeSpan::Kind::Text, "crew.not_all", 0x46db, 0x2687, 40, "6b52829a2bbb669e"},
    ExeSpan{ExeSpan::Kind::Text, "crew.abandon_warning", 0x46db, 0x26af, 62, "614c7d70db8804e8"},
    ExeSpan{ExeSpan::Kind::Text, "crew.abandon_question", 0x46db, 0x26ed, 49, "da86d667e75a59a3"},
    ExeSpan{ExeSpan::Kind::Text, "crew.assign_each", 0x46db, 0x271e, 24, "c7f7fd5fb1a116fd"},
    ExeSpan{ExeSpan::Kind::Text, "crew.is", 0x46db, 0x2736, 5, "ffc1790ad386b82f"},
    ExeSpan{ExeSpan::Kind::Text, "crew.not", 0x46db, 0x273b, 5, "436a05fbd6d52251"},
    ExeSpan{ExeSpan::Kind::Text, "crew.qualified", 0x46db, 0x2740, 27, "fd72b7f3d6b33fbd"},
    ExeSpan{ExeSpan::Kind::Text, "crew.which_vehicle", 0x46db, 0x275b, 26, "dcc92b6f2c78cd21"},
    ExeSpan{ExeSpan::Kind::Text, "crew.no_vehicle", 0x46db, 0x2775, 5, "a51ad9ee4c1a4600"},
    ExeSpan{ExeSpan::Kind::Text, "crew.as_pilot", 0x46db, 0x277a, 35, "0e37f708d9b6c548"},
    ExeSpan{ExeSpan::Kind::Text, "crew.uneasy", 0x46db, 0x279d, 17, "176df04d6369f06f"},
    ExeSpan{ExeSpan::Kind::Text, "crew.uneasy_end", 0x46db, 0x27ae, 56, "06d9b9646e4d395d"},
    // A found weapon offered to the party (17dc:15e6).
    ExeSpan{ExeSpan::Kind::Text, "offer.give", 0x46db, 0x16ae, 6, "4bab2e3777e939a2"},
    ExeSpan{ExeSpan::Kind::Text, "offer.to", 0x46db, 0x16b4, 6, "fc653f69fd89a523"},
    ExeSpan{ExeSpan::Kind::Text, "offer.drop", 0x46db, 0x16ba, 8, "8a92a66f9e007a00"},
    ExeSpan{ExeSpan::Kind::Text, "offer.already", 0x46db, 0x16c2, 28, "0553948b51fb4426"},
    // Armour offered to the party (17dc:13de).
    ExeSpan{ExeSpan::Kind::Text, "offer.armour_give", 0x46db, 0x1661, 6, "4bab2e3777e939a2"},
    ExeSpan{ExeSpan::Kind::Text, "offer.armour_to", 0x46db, 0x1667, 6, "fc653f69fd89a523"},
    ExeSpan{ExeSpan::Kind::Text, "offer.armour_drop", 0x46db, 0x166d, 30, "55192b2f926b5a57"},
    ExeSpan{ExeSpan::Kind::Text, "offer.armour_already", 0x46db, 0x168b, 35, "17293260e2ebcae1"},
    // What a recruit's arrival prints (19b8:0d58).
    ExeSpan{ExeSpan::Kind::Text, "recruit.name_is", 0x46db, 0x1e2f, 13, "a4dbe6be3d592b3e"},
    ExeSpan{ExeSpan::Kind::Text, "recruit.will_be", 0x46db, 0x1e3c, 17, "d412e3ba466aeef1"},
    ExeSpan{ExeSpan::Kind::Text, "recruit.on_foot", 0x46db, 0x1e4d, 9, "e6d51eb90eb22c7f"},
    ExeSpan{ExeSpan::Kind::Text, "recruit.passenger", 0x46db, 0x1e56, 26, "166c03341754c9cb"},
    ExeSpan{ExeSpan::Kind::Text, "recruit.mech", 0x46db, 0x1e70, 10, "f0678ba9171ea829"},
    // What special 0a prints after the money (24d3:096f).
    ExeSpan{ExeSpan::Kind::Text, "shop.cbills_end", 0x46db, 0x4c18, 4, "0fee949ff0d918ca"},
    // ComStar (24d3:037a, 0725, 04ad; the names at DG:4e7e; 17dc:19e1).
    ExeSpan{ExeSpan::Kind::Text, "comstar.investment.0", 0x46db, 0x4689, 8, "aea33db254145d8c"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.investment.1", 0x46db, 0x4691, 8, "512d3b0c1991eaad"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.investment.2", 0x46db, 0x4699, 9, "dbe55bdce8a87147"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.none", 0x46db, 0x49d3, 6, "6300f6d5d9feaa60"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.nothing_invested", 0x46db, 0x49d9, 29, "1997d86250431788"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.withdraw_title", 0x46db, 0x49f6, 19, "860ba557452e7f4b"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.withdraw_how_much", 0x46db, 0x4a09, 19, "f392b0d4b04106e7"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.label_invested", 0x46db, 0x4a1c, 15, "86a310d5c21e55c4"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.withdraw_capped", 0x46db, 0x4a2b, 63, "088a9ec8ba9b4500"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.withdraw_done", 0x46db, 0x4a6a, 28, "cfc017f48019c6dd"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.withdraw_from", 0x46db, 0x4a86, 7, "098e2e907e9db032"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.withdraw_left", 0x46db, 0x4a8d, 47, "a69b454d53a2afca"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.withdraw_end", 0x46db, 0x4abc, 4, "0fee949ff0d918ca"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.withdraw_none", 0x46db, 0x4ac0, 11, "a033e1f1b1d2638f"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.withdraw_empty", 0x46db, 0x4acb, 42, "2b28c0aabeae8754"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.no_money", 0x46db, 0x4af5, 70, "c8c2b8dc8949f16c"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.invest_title", 0x46db, 0x4b3b, 23, "80e504d91c9dbfb2"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.invest_how_much", 0x46db, 0x4b52, 21, "6374c4cea0ec9135"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.label_cbills", 0x46db, 0x4b67, 16, "83cb66ef4c1a1d58"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.invest_capped", 0x46db, 0x4b77, 65, "ef5c2eb8553a6248"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.invest_done", 0x46db, 0x4bb8, 32, "cf21dde9684a7f51"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.invest_in", 0x46db, 0x4bd8, 7, "366396fb07cdf693"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.invest_total", 0x46db, 0x4bdf, 40, "a33199c235816622"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.invest_end", 0x46db, 0x4c07, 4, "0fee949ff0d918ca"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.invest_none", 0x46db, 0x4c0b, 13, "ea702b391a49a93b"},
    ExeSpan{ExeSpan::Kind::Text, "comstar.cancelled", 0x46db, 0x1791, 20, "b3048a9cda43e570"},
    // ARENA's own 'Mech (17dc:1a26, 1c67:0b98):
    ExeSpan{ExeSpan::Kind::Text, "arena.which_mech", 0x46db, 0x17a5, 40, "b12462dda2afe62a"},
    ExeSpan{ExeSpan::Kind::Text, "arena.name_sep", 0x46db, 0x2900, 4, "c51fb3911583f0c4"},
    // ARMOR's top-up (24d3:0ef3).
    ExeSpan{ExeSpan::Kind::Text, "shop.topup_full", 0x46db, 0x4c1c, 68, "df9ef48a614e9576"},
    ExeSpan{ExeSpan::Kind::Text, "shop.topup_lacks", 0x46db, 0x4c60, 21, "8e296caa368be362"},
    ExeSpan{ExeSpan::Kind::Text, "shop.topup_none", 0x46db, 0x4c75, 4, "6cc99cd24a91be5c"},
    ExeSpan{ExeSpan::Kind::Text, "shop.topup_points", 0x46db, 0x4c79, 8, "3d271af31db79737"},
    ExeSpan{ExeSpan::Kind::Text, "shop.topup_cost", 0x46db, 0x4c81, 45, "054576405a99fcab"},
    ExeSpan{ExeSpan::Kind::Text, "shop.topup_cbills", 0x46db, 0x4cae, 37, "8e4fcc17b3ec551f"},
    ExeSpan{ExeSpan::Kind::Text, "shop.topup_ask", 0x46db, 0x4cd3, 27, "bd1b8948670888cb"},
    ExeSpan{ExeSpan::Kind::Text, "shop.topup_short", 0x46db, 0x4cee, 38, "aac0974d30cd5cee"},
    ExeSpan{ExeSpan::Kind::Text, "shop.topup_some", 0x46db, 0x4d14, 59, "4ccfcfd14e082c81"},
    ExeSpan{ExeSpan::Kind::Text, "shop.topup_nothing", 0x46db, 0x4d4f, 24, "e6c6ed7ab43c67af"},
    ExeSpan{ExeSpan::Kind::Text, "shop.topup_done", 0x46db, 0x4d67, 35, "aa799a648c10f4a8"},
    // 17dc:0629, the training missions:
    ExeSpan{ExeSpan::Kind::Text, "mission.failed", 0x46db, 0x1434, 55, "9fe5ba45e070d5e2"},
    ExeSpan{ExeSpan::Kind::Text, "mission.not_done", 0x46db, 0x146b, 54, "6449395b589999d5"},
    ExeSpan{ExeSpan::Kind::Text, "mission.course_corner", 0x46db, 0x14a1, 78, "730d843217096f71"},
    ExeSpan{ExeSpan::Kind::Text, "mission.no_hands", 0x46db, 0x14ef, 59, "c7446e981c8a9559"},
    ExeSpan{ExeSpan::Kind::Text, "mission.hands", 0x46db, 0x152a, 51, "2683068fadcb3bca"},
    ExeSpan{ExeSpan::Kind::Text, "mission.board", 0x46db, 0x155d, 61, "84dd3b9083c783a3"},
    ExeSpan{ExeSpan::Kind::Text, "mission.board_2", 0x46db, 0x159a, 58, "0900bee75b5b45f9"},
    ExeSpan{ExeSpan::Kind::Text, "mission.switch", 0x46db, 0x15d4, 36, "0ad385f38d130599"},
    ExeSpan{ExeSpan::Kind::Text, "mission.starts", 0x46db, 0x15f8, 25, "849724d86c5d755b"},
    ExeSpan{ExeSpan::Kind::Text, "mission.wont_start", 0x46db, 0x1611, 33, "139ba8fe743bb574"},
    // 1d43:0004, the Use Weapons screen, and 1d43:07cb, the target picker:
    ExeSpan{ExeSpan::Kind::Text, "weapons.shut_down", 0x46db, 0x2904, 41, "a3994158610ad718"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.sensors_damaged", 0x46db, 0x292d, 55, "f54c9810dcf599c9"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.sensors_destroyed", 0x46db, 0x2964, 63, "87d09d32b9996a19"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.select", 0x46db, 0x29a3, 27, "db61c216dcd98894"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.heading", 0x46db, 0x29be, 27, "623ce05749aa7a68"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.destroyed", 0x46db, 0x29d9, 10, "e3d05a29151d2f45"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.ammo_full", 0x46db, 0x29e3, 5, "21e1fb235cbf19d8"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.ammo_out", 0x46db, 0x29e8, 4, "0fe8ff25f4a1cf74"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.space", 0x46db, 0x29ec, 2, "869f1dfb999a452f"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.human", 0x46db, 0x29ee, 6, "f67fa9bf38446d2c"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.none", 0x46db, 0x29f4, 5, "a51ad9ee4c1a4600"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.done", 0x46db, 0x29f9, 7, "e21d5fe2b63e784c"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.choose_other", 0x46db, 0x2a00, 57, "f2df4c57323e823d"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.no_ammo", 0x46db, 0x2a39, 59, "b5cd8bce609e0087"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.already_firing", 0x46db, 0x2a74, 52, "fc2a81a8817b0044"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.choose_target", 0x46db, 0x2aa8, 30, "de76ab3fa1e17ac4"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.using", 0x46db, 0x2ac6, 20, "99adffe2cded4463"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.against", 0x46db, 0x2ada, 12, "6fb299908b23f97e"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.against_human", 0x46db, 0x2ae6, 6, "f67fa9bf38446d2c"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.at", 0x46db, 0x2aec, 5, "1f7cc42a7996c5ed"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.range_override", 0x46db, 0x2af1, 27, "2acde8d412d49a20"},
    ExeSpan{ExeSpan::Kind::Text, "pick.use", 0x46db, 0x2b0c, 10, "89bd74c50df8d000"},
    ExeSpan{ExeSpan::Kind::Text, "pick.against", 0x46db, 0x2b16, 10, "fbfef609775e0d6a"},
    ExeSpan{ExeSpan::Kind::Text, "pick.target_range", 0x46db, 0x2b22, 20, "ea603da183e9e7eb"},
    ExeSpan{ExeSpan::Kind::Text, "pick.menu", 0x46db, 0x2b36, 30, "8e5303b34b2b343b"},
    ExeSpan{ExeSpan::Kind::Text, "pick.weapon", 0x46db, 0x2b54, 10, "cb2e83bc41e1b0c1"},
    ExeSpan{ExeSpan::Kind::Text, "pick.human", 0x46db, 0x2b5e, 12, "fb7ec584c4271ee1"},
    ExeSpan{ExeSpan::Kind::Text, "pick.blank", 0x46db, 0x2b6a, 3, "4eb504a64a3c90e7"},
    ExeSpan{ExeSpan::Kind::Text, "destroyed.mech", 0x46db, 0x2b6d, 20, "4cfb43b769a70ef7"},
    ExeSpan{ExeSpan::Kind::Text, "destroyed.eject", 0x46db, 0x2b81, 12, "98306288de7e1376"},
    // 1e31: a critical hit (11ab), a person stepped on (19..), the names (1b33), a location (1b8f):
    ExeSpan{ExeSpan::Kind::Text, "shot.critical", 0x46db, 0x315e, 11, "c4bb6d79d219cd38"},
    ExeSpan{ExeSpan::Kind::Text, "shot.enemy_human", 0x46db, 0x319a, 12, "70ea76caa291f7e4"},
    ExeSpan{ExeSpan::Kind::Text, "shot.squashed", 0x46db, 0x31a6, 24, "d837a5dd05649e55"},
    ExeSpan{ExeSpan::Kind::Text, "shot.location_end", 0x46db, 0x3240, 2, "075d3ddf5a3a826e"},
    // 203b:000a, the fight: set-up, the encounter, the double agent, the end:
    ExeSpan{ExeSpan::Kind::Text, "fight.attackers", 0x46db, 0x3402, 18, "3fbeadedfdf8d342"},
    ExeSpan{ExeSpan::Kind::Text, "fight.attackers_mech", 0x46db, 0x3414, 7, "ae1e2888982eaf74"},
    ExeSpan{ExeSpan::Kind::Text, "fight.attackers_and", 0x46db, 0x341b, 6, "abf3b4851d30985b"},
    ExeSpan{ExeSpan::Kind::Text, "fight.attackers_human", 0x46db, 0x3421, 7, "e04b1f413451b6a0"},
    ExeSpan{ExeSpan::Kind::Text, "fight.engage", 0x46db, 0x3428, 18, "64fd1d1c49ba50dd"},
    ExeSpan{ExeSpan::Kind::Text, "fight.not_evaded", 0x46db, 0x343a, 24, "6a15c9d387a9ac28"},
    ExeSpan{ExeSpan::Kind::Text, "fight.raid_mechs", 0x46db, 0x3452, 69, "faf440ece5f727d9"},
    ExeSpan{ExeSpan::Kind::Text, "fight.no_escape_jenners", 0x46db, 0x3497, 33, "094383cf49356848"},
    ExeSpan{ExeSpan::Kind::Text, "fight.no_escape", 0x46db, 0x34b8, 49, "f66afe8791ba650b"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.0", 0x46db, 0x34e9, 13, "16f5b296d3c869a3"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.1", 0x46db, 0x34f6, 26, "7a57f3911dac270b"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.2", 0x46db, 0x3510, 69, "dc29b4e54964452e"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.3", 0x46db, 0x3555, 57, "429c26ef6bd10b11"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.4", 0x46db, 0x358e, 67, "e98af36a1c531953"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.5", 0x46db, 0x35d1, 63, "a9f3c18b7be8bf15"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.6", 0x46db, 0x3610, 67, "9f81769e82a1cf6d"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.7", 0x46db, 0x3653, 19, "800885a896ad070d"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.8", 0x46db, 0x3666, 67, "9a9a69dd02043d5f"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.9", 0x46db, 0x36a9, 77, "1fb58200ad972908"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.10", 0x46db, 0x36f6, 59, "6f891fd2d6582efe"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.11", 0x46db, 0x3731, 67, "e0dbcaeb329f6829"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.12", 0x46db, 0x3774, 68, "c6790cb53a083006"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.13", 0x46db, 0x37b8, 69, "37c352f9ac9745e9"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.14", 0x46db, 0x37fd, 51, "bb3cff787c2fd277"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.15", 0x46db, 0x3830, 74, "f19b220fcd2c01b7"},
    ExeSpan{ExeSpan::Kind::Text, "fight.agent.16", 0x46db, 0x387a, 66, "85f802cdc10bae0d"},
    ExeSpan{ExeSpan::Kind::Text, "fight.some_fled", 0x46db, 0x38bc, 36, "f1c54e0ead6a3b0a"},
    ExeSpan{ExeSpan::Kind::Text, "fight.all_fled", 0x46db, 0x38e0, 28, "cbbc4bec419ebe1c"},
    ExeSpan{ExeSpan::Kind::Text, "fight.beat_people", 0x46db, 0x38fc, 58, "a4d9fd2b70b09a92"},
    ExeSpan{ExeSpan::Kind::Text, "fight.eluded", 0x46db, 0x3936, 30, "e0acd99081a6c9cb"},
    ExeSpan{ExeSpan::Kind::Text, "fight.return", 0x46db, 0x3954, 55, "cbe4d07eb571d910"},
    ExeSpan{ExeSpan::Kind::Text, "fight.return_to_base", 0x46db, 0x398b, 31, "f674a7bdfbc7503b"},
    ExeSpan{ExeSpan::Kind::Text, "fight.arena_won", 0x46db, 0x39aa, 32, "c4559a71d3360b2a"},
    ExeSpan{ExeSpan::Kind::Text, "fight.arena_lost", 0x46db, 0x39ca, 45, "69d3577b21b8a4d9"},
    ExeSpan{ExeSpan::Kind::Text, "fight.eluded_2", 0x46db, 0x39f7, 30, "e0acd99081a6c9cb"},
    // 203b:0fa9, by the encounter (pointers at DG:3a2e):
    ExeSpan{ExeSpan::Kind::Text, "fight.raid.0", 0x46db, 0x32e6, 67, "cfe14dda6d049ea5"},
    ExeSpan{ExeSpan::Kind::Text, "fight.raid.1", 0x46db, 0x3329, 65, "1247cb50874bb04a"},
    ExeSpan{ExeSpan::Kind::Text, "fight.raid.2", 0x46db, 0x336a, 56, "e0cae20ddd6f8474"},
    ExeSpan{ExeSpan::Kind::Text, "fight.raid.3", 0x46db, 0x33a2, 60, "04cd4d4f13726a65"},
    ExeSpan{ExeSpan::Kind::Text, "fight.raid.4", 0x46db, 0x33de, 36, "ad609b9ee1e499e9"},
    // 203b:14c3, the orders menu, and its helpers (1c1f, 1e46, 2223, 24d2, 2591):
    ExeSpan{ExeSpan::Kind::Text, "orders.computer_moving", 0x46db, 0x3a42, 45, "dbecabe0eba4c812"},
    ExeSpan{ExeSpan::Kind::Text, "orders.menu_mech", 0x46db, 0x3a6f, 77, "4d557b5052311b83"},
    ExeSpan{ExeSpan::Kind::Text, "orders.menu_person", 0x46db, 0x3abc, 74, "a11b09e1ba73cb44"},
    ExeSpan{ExeSpan::Kind::Text, "orders.now", 0x46db, 0x3b24, 18, "5bfb72b42e300804"},
    ExeSpan{ExeSpan::Kind::Text, "orders.now_n", 0x46db, 0x3b36, 2, "481daf8515fdae90"},
    ExeSpan{ExeSpan::Kind::Text, "orders.now_ing", 0x46db, 0x3b38, 5, "cfe40f958d3c4fe6"},
    ExeSpan{ExeSpan::Kind::Text, "orders.wish", 0x46db, 0x3b3d, 17, "cae631b6e1294ee0"},
    ExeSpan{ExeSpan::Kind::Text, "orders.instead", 0x46db, 0x3b4e, 10, "592d4b3bf1b912a8"},
    ExeSpan{ExeSpan::Kind::Text, "orders.deleted", 0x46db, 0x3b58, 52, "3d0d59404f0f7a95"},
    ExeSpan{ExeSpan::Kind::Text, "orders.new_moves", 0x46db, 0x3b8c, 33, "9a3792ffe95394ad"},
    ExeSpan{ExeSpan::Kind::Text, "orders.no_jump_jets", 0x46db, 0x3bad, 29, "44313d549ab04d3d"},
    ExeSpan{ExeSpan::Kind::Text, "orders.overheated", 0x46db, 0x3bca, 41, "a3994158610ad718"},
    ExeSpan{ExeSpan::Kind::Text, "orders.inhibited", 0x46db, 0x3bf3, 26, "32e4b8f5844ccb08"},
    ExeSpan{ExeSpan::Kind::Text, "orders.inhibited_legs", 0x46db, 0x3c0d, 13, "e6267536381d972b"},
    ExeSpan{ExeSpan::Kind::Text, "orders.inhibited_heat", 0x46db, 0x3c1a, 25, "624711d6f1c97864"},
    ExeSpan{ExeSpan::Kind::Text, "orders.cant", 0x46db, 0x3c33, 11, "a86e9504c2fb54ec"},
    ExeSpan{ExeSpan::Kind::Text, "orders.move", 0x46db, 0x3c3e, 5, "7099ae71fc25b77c"},
    ExeSpan{ExeSpan::Kind::Text, "orders.no_further", 0x46db, 0x3c43, 24, "d34ee519d286de15"},
    ExeSpan{ExeSpan::Kind::Text, "orders.try_running", 0x46db, 0x3c5b, 46, "89e2b061f682c74b"},
    ExeSpan{ExeSpan::Kind::Text, "orders.you_have", 0x46db, 0x3c89, 10, "4132c1bc048aae1a"},
    ExeSpan{ExeSpan::Kind::Text, "orders.points_left", 0x46db, 0x3c93, 65, "e18dd9de495d84cd"},
    ExeSpan{ExeSpan::Kind::Text, "orders.choose_kick", 0x46db, 0x3ce0, 26, "f9d75ac6ac9568e3"},
    ExeSpan{ExeSpan::Kind::Text, "orders.cant_kick", 0x46db, 0x3cfa, 64, "4e98fd89f29b2c9b"},
    ExeSpan{ExeSpan::Kind::Text, "options.messages", 0x46db, 0x3d3a, 36, "e33e558cb87741de"},
    ExeSpan{ExeSpan::Kind::Text, "options.graphics", 0x46db, 0x3d5e, 21, "04fdb5fdc4c32bc1"},
    ExeSpan{ExeSpan::Kind::Text, "scan.which", 0x46db, 0x3d73, 33, "720303c252ee30a7"},
    ExeSpan{ExeSpan::Kind::Text, "scan.humans_only", 0x46db, 0x3d94, 57, "1857059da47a3806"},
    ExeSpan{ExeSpan::Kind::Text, "scan.no_sensors", 0x46db, 0x3dcd, 76, "2ab4867556304669"},
    ExeSpan{ExeSpan::Kind::Text, "scan.no_humans", 0x46db, 0x3e19, 42, "def389dc814fa8b1"},
    // 203b:1cfe, the movement modes (pointers at DG:3cd4):
    ExeSpan{ExeSpan::Kind::Text, "orders.mode.0", 0x46db, 0x3b16, 5, "35a5a3e556c33ceb"},
    ExeSpan{ExeSpan::Kind::Text, "orders.mode.1", 0x46db, 0x3b1b, 4, "46693f9ace9fb368"},
    ExeSpan{ExeSpan::Kind::Text, "orders.mode.2", 0x46db, 0x3b1f, 5, "d794b8b913e8c7af"},
    // 22e8:000c, one round: the shot's message pieces (docs/combat.md):
    ExeSpan{ExeSpan::Kind::Text, "shot.party_mech", 0x46db, 0x3e44, 9, "3cbb15fdcf6e4ba4"},
    ExeSpan{ExeSpan::Kind::Text, "shot.party_kicks", 0x46db, 0x3e4d, 6, "44ad21cf591a7aa6"},
    ExeSpan{ExeSpan::Kind::Text, "shot.party_kick", 0x46db, 0x3e53, 5, "95d9d94b1d1b98e9"},
    ExeSpan{ExeSpan::Kind::Text, "shot.party_uses", 0x46db, 0x3e58, 8, "0d343328a305c3c7"},
    ExeSpan{ExeSpan::Kind::Text, "shot.party_person_uses", 0x46db, 0x3e60, 9, "a846bea42bf81221"},
    ExeSpan{ExeSpan::Kind::Text, "shot.enemy", 0x46db, 0x3e69, 10, "00872509e8128bff"},
    ExeSpan{ExeSpan::Kind::Text, "shot.enemy_human_uses", 0x46db, 0x3e73, 14, "04d64b7335553244"},
    ExeSpan{ExeSpan::Kind::Text, "shot.enemy_mech", 0x46db, 0x3e81, 6, "42bc856d0ea84fd2"},
    ExeSpan{ExeSpan::Kind::Text, "shot.enemy_uses", 0x46db, 0x3e87, 8, "0d343328a305c3c7"},
    ExeSpan{ExeSpan::Kind::Text, "shot.enemy_kicks", 0x46db, 0x3e8f, 6, "44ad21cf591a7aa6"},
    ExeSpan{ExeSpan::Kind::Text, "shot.enemy_kick", 0x46db, 0x3e95, 5, "95d9d94b1d1b98e9"},
    ExeSpan{ExeSpan::Kind::Text, "shot.on", 0x46db, 0x3e9a, 5, "180dafc4fe544297"},
    ExeSpan{ExeSpan::Kind::Text, "shot.target_mech", 0x46db, 0x3e9f, 8, "cefbdd642fcf3ab9"},
    ExeSpan{ExeSpan::Kind::Text, "shot.target_end", 0x46db, 0x3ea7, 2, "075d3ddf5a3a826e"},
    ExeSpan{ExeSpan::Kind::Text, "shot.target_enemy", 0x46db, 0x3ea9, 10, "c78667aa1e3e5649"},
    ExeSpan{ExeSpan::Kind::Text, "shot.target_enemy_mech", 0x46db, 0x3eb3, 6, "ad7cdc68703d7956"},
    ExeSpan{ExeSpan::Kind::Text, "shot.target_enemy_human", 0x46db, 0x3eb9, 7, "a7d6f2a60022b99a"},
    ExeSpan{ExeSpan::Kind::Text, "shot.target_spectator", 0x46db, 0x3ec0, 13, "211fa3cbac890dad"},
    ExeSpan{ExeSpan::Kind::Text, "shot.person_missed", 0x46db, 0x3ecd, 9, "0ae9f11edebcc79b"},
    ExeSpan{ExeSpan::Kind::Text, "shot.person_hit", 0x46db, 0x3ed6, 8, "8ed282a53cb65745"},
    ExeSpan{ExeSpan::Kind::Text, "shot.mech_missed", 0x46db, 0x3ede, 9, "0ae9f11edebcc79b"},
    ExeSpan{ExeSpan::Kind::Text, "shot.mech_hit", 0x46db, 0x3ee7, 8, "fc7b54e3c9c66076"},
    // 22e8:12c7, after the shot:
    ExeSpan{ExeSpan::Kind::Text, "shot.killed", 0x46db, 0x41fc, 13, "4f69cab62915ffa5"},
    ExeSpan{ExeSpan::Kind::Text, "shot.arena_damaged", 0x46db, 0x4209, 44, "fd9c49494de33334"},
    ExeSpan{ExeSpan::Kind::Text, "shot.urbanmechs", 0x46db, 0x4235, 57, "14d6ff3bfb01a25d"},
    // 1e31:031e, a person's condition by hit points (pointers at DG:2e0c):
    ExeSpan{ExeSpan::Kind::Text, "condition.0", 0x46db, 0x2bb4, 27, "0a874eff9e67177f"},
    ExeSpan{ExeSpan::Kind::Text, "condition.1", 0x46db, 0x2bcf, 31, "547fde09531e0c6d"},
    ExeSpan{ExeSpan::Kind::Text, "condition.2", 0x46db, 0x2bee, 25, "899b091193921e13"},
    ExeSpan{ExeSpan::Kind::Text, "condition.3", 0x46db, 0x2c07, 26, "a914b3618d88abf3"},
    ExeSpan{ExeSpan::Kind::Text, "condition.4", 0x46db, 0x2c21, 30, "ee55863a45e965fb"},
    ExeSpan{ExeSpan::Kind::Text, "condition.5", 0x46db, 0x2c3f, 23, "3c585279fbb7f28d"},
    ExeSpan{ExeSpan::Kind::Text, "condition.6", 0x46db, 0x2c56, 32, "f1567e8fdae95087"},
    ExeSpan{ExeSpan::Kind::Text, "condition.7", 0x46db, 0x2c76, 26, "4d16352e3adc6f26"},
    ExeSpan{ExeSpan::Kind::Text, "condition.8", 0x46db, 0x2c90, 25, "377a0d21bd755d25"},
    ExeSpan{ExeSpan::Kind::Text, "condition.9", 0x46db, 0x2ca9, 28, "4fb3201634537840"},
    ExeSpan{ExeSpan::Kind::Text, "condition.10", 0x46db, 0x2cc5, 27, "59af90a1badcf808"},
    // 1d43, the range bands (pointers at DG:2ebc):
    ExeSpan{ExeSpan::Kind::Text, "weapons.range.0", 0x46db, 0x2ce0, 6, "211d68c609dfb87a"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.range.1", 0x46db, 0x2ce6, 4, "2f0873ac2023beff"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.range.2", 0x46db, 0x2cea, 5, "c7135d78827efa15"},
    ExeSpan{ExeSpan::Kind::Text, "weapons.range.3", 0x46db, 0x2cef, 4, "1bfb8504453f7ae9"},
    // 1e31:1b8f, the locations, by location index (armour byte 0x11 + index; the names' order and DG:3242 differ):
    ExeSpan{ExeSpan::Kind::Text, "location.0", 0x46db, 0x31d2, 9, "eb2b567d61eff022"},
    ExeSpan{ExeSpan::Kind::Text, "location.1", 0x46db, 0x31be, 11, "70de66c476a6624c"},
    ExeSpan{ExeSpan::Kind::Text, "location.2", 0x46db, 0x31c9, 9, "0ef6bf83edcc34b9"},
    ExeSpan{ExeSpan::Kind::Text, "location.3", 0x46db, 0x3229, 5, "92d622999b2a5e9b"},
    ExeSpan{ExeSpan::Kind::Text, "location.4", 0x46db, 0x321c, 13, "a809f295e2ce04c5"},
    ExeSpan{ExeSpan::Kind::Text, "location.5", 0x46db, 0x3201, 10, "f2f968e979a26496"},
    ExeSpan{ExeSpan::Kind::Text, "location.6", 0x46db, 0x31eb, 12, "72031d1cd0fbeb08"},
    ExeSpan{ExeSpan::Kind::Text, "location.7", 0x46db, 0x31f7, 10, "50364d973d946432"},
    ExeSpan{ExeSpan::Kind::Text, "location.8", 0x46db, 0x31db, 16, "285b5a04acb524ee"},
    ExeSpan{ExeSpan::Kind::Text, "location.9", 0x46db, 0x322e, 18, "0527754dcfb18966"},
    ExeSpan{ExeSpan::Kind::Text, "location.10", 0x46db, 0x320b, 17, "ac42c1dbe1dc1f6e"},
    // The personal armour types (pointers at DG:4e8a):
    ExeSpan{ExeSpan::Kind::Text, "armour.0", 0x46db, 0x46a2, 5, "a51ad9ee4c1a4600"},
    ExeSpan{ExeSpan::Kind::Text, "armour.1", 0x46db, 0x46a7, 10, "a01da13d5d11b563"},
    ExeSpan{ExeSpan::Kind::Text, "armour.2", 0x46db, 0x46b1, 10, "f2b6180633d15f1b"},
    ExeSpan{ExeSpan::Kind::Text, "armour.3", 0x46db, 0x46bb, 12, "fe9e952266510b56"},
    ExeSpan{ExeSpan::Kind::Text, "armour.4", 0x46db, 0x46c7, 12, "4d49cde815750482"},
    ExeSpan{ExeSpan::Kind::Text, "armour.5", 0x46db, 0x46d3, 9, "6aab0bee353c5d01"},
    // 1c67:08a8, the training center's quiz (docs/combat.md 7): the message
    // before it, the question, and menu 0x25's 20 part names (the pointers
    // at DG:4de2; 1c67:0ae9 prints 0x14 of them).
    ExeSpan{ExeSpan::Kind::Text, "quiz.message", 0x46db, 0x27f2, 122, "eb0ca8afa59c4dbc"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.question", 0x46db, 0x286c, 26, "7c15df140b6c473d"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.0", 0x46db, 0x426e, 12, "8c538a8268c54ba9"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.1", 0x46db, 0x427a, 9, "b95de38623ddc5bf"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.2", 0x46db, 0x4283, 10, "93add76a773576d6"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.3", 0x46db, 0x428d, 13, "8cf89680065f4694"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.4", 0x46db, 0x429a, 13, "0fa52f9a2a5cdd54"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.5", 0x46db, 0x42a7, 14, "60ed793cc352035a"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.6", 0x46db, 0x42b5, 15, "f30809dc623ad40c"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.7", 0x46db, 0x42c4, 14, "e44001d451847d6c"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.8", 0x46db, 0x42d2, 14, "b036cab2a4d90c87"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.9", 0x46db, 0x42e0, 14, "bbb6a33d02783211"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.10", 0x46db, 0x42ee, 15, "3a826fedbb976cf1"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.11", 0x46db, 0x42fd, 14, "6107befc355be4b1"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.12", 0x46db, 0x430b, 15, "5173d3d2b77a3b54"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.13", 0x46db, 0x431a, 17, "475a9eaff7b9737a"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.14", 0x46db, 0x432b, 11, "6a0de3e12187ae49"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.15", 0x46db, 0x4336, 16, "fe06e083247377db"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.16", 0x46db, 0x4346, 16, "2cf1e4e6ab9c3766"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.17", 0x46db, 0x4356, 10, "d473631446e13256"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.18", 0x46db, 0x4360, 13, "86cf8b0ad80cef9b"},
    ExeSpan{ExeSpan::Kind::Text, "quiz.part.19", 0x46db, 0x436d, 8, "ef0bfc1b90ae3790"},
    // Healing (1c31:000a): the skills by level, the care by level, the lines.
    ExeSpan{ExeSpan::Kind::Text, "heal.skill.1", 0x46db, 0x24b6, 10, "def8cbe01901c853"},
    ExeSpan{ExeSpan::Kind::Text, "heal.skill.2", 0x46db, 0x24c0, 15, "ac13105360b05d5f"},
    ExeSpan{ExeSpan::Kind::Text, "heal.skill.4", 0x46db, 0x24cf, 14, "186198a039f6fc6d"},
    ExeSpan{ExeSpan::Kind::Text, "heal.skill.5", 0x46db, 0x24dd, 17, "8f4d0384adf7b42b"},
    ExeSpan{ExeSpan::Kind::Text, "heal.care.1", 0x46db, 0x24ee, 18, "9e03a9f575a7dbd0"},
    ExeSpan{ExeSpan::Kind::Text, "heal.care.2", 0x46db, 0x2500, 7, "9b1fbe9d427abd6a"},
    ExeSpan{ExeSpan::Kind::Text, "heal.care.3", 0x46db, 0x2507, 18, "53c26f2458148e8b"},
    ExeSpan{ExeSpan::Kind::Text, "heal.care.4", 0x46db, 0x2519, 20, "da1970e9af9ad76e"},
    ExeSpan{ExeSpan::Kind::Text, "heal.doctor", 0x46db, 0x252d, 11, "db317a32c6808cd3"},
    ExeSpan{ExeSpan::Kind::Text, "heal.uses", 0x46db, 0x2538, 11, "aade1a4519119e26"},
    ExeSpan{ExeSpan::Kind::Text, "heal.skill_and", 0x46db, 0x2543, 16, "1826976a31fc0594"},
    ExeSpan{ExeSpan::Kind::Text, "heal.no_skill", 0x46db, 0x2553, 28, "dee50b99b3374928"},
    ExeSpan{ExeSpan::Kind::Text, "heal.to_assist", 0x46db, 0x256f, 24, "d748ad00b52431af"},
    ExeSpan{ExeSpan::Kind::Text, "heal.healing", 0x46db, 0x2587, 10, "d2fab493ded7f756"},
    ExeSpan{ExeSpan::Kind::Text, "heal.nobody", 0x46db, 0x2591, 19, "1a406fa4b46db7b0"},
    ExeSpan{ExeSpan::Kind::Text, "heal.more_time", 0x46db, 0x25a4, 43, "aedf8b581a931198"},
    ExeSpan{ExeSpan::Kind::Text, "heal.nobody_2", 0x46db, 0x25cf, 19, "1a406fa4b46db7b0"},
    ExeSpan{ExeSpan::Kind::Text, "cache.door_wants.0", 0x46db, 0x242c, 21, "3fdcb68e36a347cb"},
    ExeSpan{ExeSpan::Kind::Text, "cache.door_wants.1", 0x46db, 0x2441, 22, "6d8a648fb8435416"},
    ExeSpan{ExeSpan::Kind::Text, "cache.door_wants.2", 0x46db, 0x2457, 23, "1757d07eee6b5571"},
    ExeSpan{ExeSpan::Kind::Text, "cache.item_kind.0", 0x46db, 0x214d, 4, "9b29d46cba180f06"},
    ExeSpan{ExeSpan::Kind::Text, "cache.item_kind.1", 0x46db, 0x2151, 5, "691d1e483ab71fd9"},
    ExeSpan{ExeSpan::Kind::Text, "cache.item_kind.2", 0x46db, 0x2156, 7, "264d1c239189c965"},
    ExeSpan{ExeSpan::Kind::Text, "cache.item_used", 0x46db, 0x215d, 42, "7ca17c2f3b08fe20"},
    ExeSpan{ExeSpan::Kind::Text, "cache.item_take", 0x46db, 0x2187, 43, "8947176059a60f22"},
    ExeSpan{ExeSpan::Kind::Text, "cache.item_number", 0x46db, 0x21b2, 7, "0e1d9ec3fb7c5393"},
    ExeSpan{ExeSpan::Kind::Text, "cache.item_take_end", 0x46db, 0x21b9, 21, "ec43e137cc39f8fd"},
    ExeSpan{ExeSpan::Kind::Text, "cache.wall_opened", 0x46db, 0x2075, 71, "fbd62eeb73be19a6"},
    ExeSpan{ExeSpan::Kind::Text, "cache.screen", 0x46db, 0x20ce, 69, "9fce65b7cd943e15"},
    ExeSpan{ExeSpan::Kind::Text, "cache.hint", 0x46db, 0x2113, 58, "1d20e5069a29f97a"},
    ExeSpan{ExeSpan::Kind::Text, "cache.puzzle_done", 0x46db, 0x22c7, 33, "f2f54163aff4f8a3"},
    ExeSpan{ExeSpan::Kind::Text, "cache.way_down", 0x46db, 0x22e8, 64, "b005b1560117639a"},
    ExeSpan{ExeSpan::Kind::Text, "cache.way_down_2", 0x46db, 0x2328, 45, "6ec77e784936bad2"},
    ExeSpan{ExeSpan::Kind::Text, "cache.way_up", 0x46db, 0x2367, 62, "af5670081b505116"},
    ExeSpan{ExeSpan::Kind::Text, "cache.puzzle_solved", 0x46db, 0x23b2, 87, "48e8a61999d2cf36"},
    ExeSpan{ExeSpan::Kind::Text, "cache.puzzle_wrong", 0x46db, 0x2409, 20, "a8aafd47acf3da5f"},
    ExeSpan{ExeSpan::Kind::Text, "cache.out_closed", 0x46db, 0x2240, 53, "57d671cf36b0cb5b"},
    ExeSpan{ExeSpan::Kind::Text, "cache.out_puzzle", 0x46db, 0x2275, 22, "929fad0a5501689d"},
    ExeSpan{ExeSpan::Kind::Text, "cache.out_findit", 0x46db, 0x228b, 60, "20f0fe53ca4764c5"},
};

constexpr std::uint16_t kLoadSegment = 0x1000;

}  // namespace

std::span<const ExeSpan> OriginalExe::table() { return kDos103; }

std::string OriginalExe::address(const ExeSpan& span) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04x:%04x", span.segment, span.offset);
    return buf;
}

namespace {

const std::string kUnknown = "this BTECH.EXE is not a version the rebuild knows (it supports " +
                             std::string(kSupportedVersion) + ")";

std::uint16_t word_at(const std::vector<std::uint8_t>& b, std::size_t at) {
    return std::uint16_t(b[at] | b[at + 1] << 8);
}

std::string short_hash(std::span<const std::uint8_t> bytes) { return sha256_hex(bytes).substr(0, 16); }

}  // namespace

OriginalExe::OriginalExe(std::span<const std::uint8_t> packed) {
    try {
        unpacked_ = unexepack(packed);
    } catch (const FormatError& e) {
        throw FormatError(kUnknown + ": " + e.what());
    }
    check();
}

OriginalExe OriginalExe::from_unpacked(std::vector<std::uint8_t> unpacked) {
    OriginalExe exe;
    exe.unpacked_ = std::move(unpacked);
    exe.check();
    return exe;
}

// The layout first, then each imported piece against its hash.
void OriginalExe::check() {
    const auto& L = kDos103Layout;
    const auto& b = unpacked_;
    const bool layout = b.size() == L.size && b.size() >= std::size_t(L.header_paragraphs) * 16 &&
                        b[0] == 'M' && b[1] == 'Z' && word_at(b, 6) == L.relocations &&
                        word_at(b, 8) == L.header_paragraphs && word_at(b, 0x0e) == L.ss &&
                        word_at(b, 0x10) == L.sp && word_at(b, 0x14) == L.ip && word_at(b, 0x16) == L.cs &&
                        short_hash(std::span(b).first(std::size_t(L.header_paragraphs) * 16)) == L.header_sha256;
    if (!layout) throw FormatError(kUnknown + ": its layout is not that program's");
    image_start_ = std::size_t(L.header_paragraphs) * 16;

    std::vector<std::string_view> differ;
    for (const auto& span : kDos103)
        if (short_hash(get(span.name)) != span.sha256) differ.push_back(span.name);
    if (differ.empty()) return;
    std::string names;
    for (std::size_t i = 0; i < differ.size() && i < 8; ++i) names += (i ? ", " : "") + std::string(differ[i]);
    if (differ.size() > 8) names += " and " + std::to_string(differ.size() - 8) + " more";
    throw FormatError(kUnknown + ": " + std::to_string(differ.size()) + " of the pieces the rebuild imports differ (" +
                      names + ")");
}

std::string OriginalExe::sha256() const { return sha256_hex(unpacked_); }

OriginalExe OriginalExe::load(const std::filesystem::path& game_dir) {
    const auto path = find_game_file(game_dir, "BTECH.EXE");
    if (!path) throw FormatError("BTECH.EXE not found in " + game_dir.string());
    return OriginalExe(read_file(*path));
}

std::span<const std::uint8_t> OriginalExe::get(std::string_view name) const {
    const auto it = std::find_if(kDos103.begin(), kDos103.end(),
                                 [&](const ExeSpan& s) { return s.name == name; });
    if (it == kDos103.end()) throw FormatError("no piece of BTECH.EXE called " + std::string(name));
    const std::size_t at = image_start_ + std::size_t(it->segment - kLoadSegment) * 16 + it->offset;
    if (at + it->size > unpacked_.size()) throw FormatError("BTECH.EXE: piece out of range");
    return std::span(unpacked_).subspan(at, it->size);
}

std::string OriginalExe::text(std::string_view name) const { return text_of(get(name)); }

std::string OriginalExe::text_of(std::span<const std::uint8_t> bytes) {
    std::size_t n = 0;
    while (n < bytes.size()) {
        const std::uint8_t c = bytes[n];
        if (c == 0) break;
        ++n;
        if (c & 0x80) break;
        if (c == 0x02 || c == 0x06 || c == 0x09 || c == 0x13) ++n;  // and its argument
    }
    return std::string(bytes.begin(), bytes.begin() + std::ptrdiff_t(std::min(n, bytes.size())));
}

}  // namespace chr::formats
