// Switches for the original's bugs (docs/combat.md, "Original bugs and
// quirks"; the number after each name is the row there).
//
// The rule: the rebuild keeps every bug the player can meet, and each fix is
// off by default. The exceptions are on by default: bugs where the original
// reads or writes memory the rebuild does not hold (past the end of a table,
// uninitialised stack), or never returns. Their effect can't be reproduced
// exactly, so the rebuild takes the fixed path and logs that it did.
// Bugs that DOS 1.03 never reaches are documented only and have no switch.
#pragma once

namespace chr::game {

struct Fixes {
    // Kept by default (off).
    bool fix_distance = false;             // 1: 1e31:0bb5, min + max/2
    bool fix_critical_count = false;       // 3: 1e31:11ab, a critical on every internal hit
    bool fix_person_damage = false;        // 4: 22e8:000c, damage not reset between shots
    bool fix_actuator_min = false;         // 6: 203b:22bc, MP -1 ends as 0
    bool fix_run_parity = false;           // 7: 203b:22bc, the run bonus tests the base MP
    bool fix_enemy_foot_mp = false;        // 8: 1e31:03ab / 203b:2474, figure - 8
    bool fix_planner_reach_index = false;  // 9: 1e31:03ab, the next weapon's range
    bool fix_planner_reach_scale = false;  // 10: 1e31:03ab, 4 x S
    bool fix_planner_stop_point = false;   // 11: 1e31:03ab, fixed diagonal steps
    bool fix_retreat_flag = false;         // 13: 1e31:03ab, F:3992
    bool fix_auto_retreat = false;         // 14: 203b:000a, 2 read as "left the area"
    bool fix_waypoint_compare = false;     // 16: 203b:1c1f, sign extension
    bool fix_order_bound = false;          // 17: 203b:1c1f, a 13th waypoint in the next figure's orders
    bool fix_scan_sensors = false;         // 19: 203b:2591
    bool fix_probe_heading = false;        // 20: 203b:28db, figure 0's heading
    bool fix_flee_target = false;          // 21: 203b:000a, an ff target is figure -1 (F:4002 / F:4034)
    bool fix_heat_sink_count = false;      // 22: 15ab:0002
    bool fix_scrap_multiplier = false;     // 24: 15ab:0002, slot index, not skill
    bool fix_salvage_sprite = false;       // 25: 15ab:04f9
    bool fix_stale_hit_flag = false;       // 5: 22e8:12c7, last shot's flag (the first shot,
                                           //    uninitialised in the original, gets "missed" and a log line)
    bool fix_loot_index = false;           // 27: 15ab:094b, character s + 12 ('Mech 0's bytes)
    bool fix_area_wrap = false;            // 31: 203b:2835
    bool fix_stop_on_loss = false;         // 33: 22e8:000c
    bool fix_enemy_heat_cap = false;       // 35: 1e31:0c63, the cap at 30 tests party record a
    bool fix_terrain_without_graphics = false;  // 36: 22e8:000c, the drawer's terrain without the
                                                //     combat graphics, as with them
    bool fix_terrain_noise = false;        // 37: 287f:0df8, a right side's noise unscaled
    bool fix_talk_let_out = false;         // 38: 17dc:17b9, the last one waiting's place, not this one
    bool fix_overhead_store = false;       // 39: 1000:3fae, maps 12 and 13 move the store on by 512
    bool fix_repair_type_count = false;    // 40: 19b8:0002, a mended weapon type's count down by one only
    bool fix_repair_last_type = false;     // 41: 19b8:06f9, "any left" skips weapon type 0x20
    bool fix_kick_actuators = false;       // 2: 22e8:000c, an enemy kicker's actuators read from
                                           //    'Mech record s, in the stack
    bool fix_weapon_count = false;         // 23: 15ab:0002, the SRM 6 count left on the stack
                                           //    (DGROUP's low byte, combat.md 2.15)

    // Fixed by default (on): memory the rebuild does not hold, or a hang.
    bool fix_path_bound = true;       // 18: 203b:193b, a path of more than 12 steps
    bool fix_wreck_search = true;     // 26: 15ab:04f9, wrong records, may never end
    bool fix_scan_list = true;        // 32: 15ab:1467, may never end
};

}  // namespace chr::game
