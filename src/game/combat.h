// A fight's rules: movement points, paths, the computer's plans, the round
// with its shots and damage, and the end of a round (docs/combat.md 3 and 4).
// No screen and no keys: what the fight shows goes out through Combat::Hooks,
// and the Town runs the loop around it (game/fight.cpp).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "formats/mtp.h"
#include "formats/rules.h"
#include "formats/tables.h"
#include "game/fight.h"
#include "game/figures.h"
#include "game/terrain.h"
#include "game/fixes.h"
#include "game/flags.h"
#include "game/random.h"
#include "game/records.h"
#include "game/shown.h"
#include "game/text_colour.h"

namespace chr::game {

// BTECH.EXE 1000:19f3 (Ghidra FUN_1000_19f3)
// A d6: r & 7, drawn again while above 5, plus 1.
int d6(Random& random);
// BTECH.EXE 1000:19dd (Ghidra FUN_1000_19dd)
// 2d6: two d6, the first drawn first.
int two_d6(Random& random);

// The map marks (F:d457 code, F:d497 area, F:d4d7 x, F:d517 y): 64 places,
// the oldest overwritten (docs/combat.md 3.13).
struct MapMark {
    std::uint8_t code = 0, area = 0, x = 0, y = 0;
    bool operator==(const MapMark&) const = default;
};
struct MapMarks {
    static constexpr int kCount = 64;
    std::array<MapMark, kCount> marks{};
    int next = 0;  // F:d557
    // BTECH.EXE 203b:27c9 (Ghidra FUN_203b_27c9)
    void add(int code, std::uint16_t x, std::uint16_t y);
};

// 'Mech record bytes the fight uses (docs/formats/mech.md).
namespace mech {
inline constexpr int kTonnage = 0x10;
inline constexpr int kArmour = 0x11;         // 0x11-0x1b: LA LT LL H CT RA RT RL, rear LT CT RT
inline constexpr int kStructure = 0x1c;      // 0x1c-0x23: LA LT LL H CT RA RT RL
inline constexpr int kHead = 0x1f, kCentreTorso = 0x20;
inline constexpr int kLeftArm = 0x1c, kRightArm = 0x21;
inline constexpr int kLeftActuators = 0x24;  // the left arm (high nibble) and leg (low)
inline constexpr int kRightActuators = 0x25;
inline constexpr int kEngineSinks = 0x26;
inline constexpr int kAmmunition = 0x27;     // one per weapon slot, ff: unlimited
inline constexpr int kWalk = 0x31, kJump = 0x32;
inline constexpr int kItems = 0x33, kItemsEnd = 0x56;
inline constexpr int kEngineHits = 0x75, kGyroHits = 0x76, kSensorHits = 0x77, kLifeSupport = 0x78;
inline constexpr int kPilot = 0x79, kSecond = 0x7a;
inline constexpr int kHeatSinkItem = 0x22;
}  // namespace mech

// BTECH.EXE 1e31:1ea2 (Ghidra FUN_1e31_1ea2)
// The octant (0-7) of a line on the screen, from its y difference (start
// minus end) and x difference (end minus start): which entries of the line
// step table (Tables::line_steps) walk it.
int octant(int dy, int dx);

// The weapon record's bytes (DG:2ed8 + 0x11 w).
namespace weapon {
inline constexpr int kDamage = 0x0b;   // personal: low nibble + (high nibble) d6
inline constexpr int kShots = 0x0c;    // bit 7: a personal weapon; else the cluster column
inline constexpr int kHeat = 0x0d;     // low nibble
inline constexpr int kShortMedium = 0x0e;  // S in bits 5-7, M in bits 0-4
inline constexpr int kLong = 0x0f;
inline constexpr int kSkill = 0x10;
inline constexpr int kKick = 0x20, kInferno = 0x0b;
}  // namespace weapon

// The fight's tables in F: that are not the figures' places (fight.h):
// one entry per figure, or per 'Mech record.
struct CombatState {
    static constexpr int kFigures = FightFigures::kCount;
    static constexpr int kOrderBytes = 0x30;  // 12 orders of (type, area, x, y)
    static constexpr int kSlots = 12;         // weapon slots; 11 is the Kick
    static constexpr int kPathBytes = 0x18;   // 12 steps of (dx, dy); 2 ends it
    std::array<std::array<std::uint8_t, kOrderBytes>, kFigures> orders{};   // F:32c6; type ff: none
    std::array<std::array<std::uint8_t, kSlots>, kFigures> targets{};       // F:3800; ff none, bit 7 fired
    std::array<std::array<std::int8_t, kPathBytes>, kFigures> paths{};      // F:40b4
    std::array<std::uint8_t, kFigures> path_step{};      // F:0078: steps of the path taken this round
    // Written by the figure drawer (1000:0e4b), only for figures it draws:
    std::array<std::int8_t, kFigures> terrain{};         // F:32ae: rows it sinks, 0 on dry land
    std::array<std::uint8_t, kFigures> tile{};           // F:3750: the tile under it
    std::array<std::uint8_t, kFigures> standing_on{};    // F:45ce: a 'Mech's tile one cell up
    std::array<std::uint8_t, kFigures> sink{};           // F:4554: a 'Mech in its cell's other half-row
    std::array<bool, kFigures> on_screen{};              // F:42f6
    std::array<int, kFigures> screen_x{}, screen_y{};    // F:324c/327c: its pixel place
    std::array<std::int8_t, kFigures> parked_facing{};   // F:45b6
    std::array<std::int8_t, 8> heat{};                   // F:006e, by 'Mech record
    std::array<std::int8_t, 8> weapon_heat{};            // F:0092: this round's
    std::array<std::uint8_t, 8> burning{};               // F:d576: rounds left
    std::array<std::uint8_t, 8> wreck_letter{};          // F:323e: a destroyed record's first byte
    bool fight_on = true;        // DG:014a: 0 when the leader is killed
    bool person_ran = false;     // F:3992: the party fled (kinds 1-3), or a party person was planned
                                 // against a 'Mech
    bool enemy_people_ran = false;  // F:374c
    bool spectator = false;      // F:e48e: the Arena's spectator (figure 13)
};

class Combat {
public:
    // BTECH.EXE 203b:28db (Ghidra FUN_203b_28db), its walk
    // Whether the party's place reaches `to` in at most 30 steps of
    // 1e31:0006 (mode 0x80: no figure blocks, figure 0's heading turns),
    // the view followed as the steps go.
    bool reaches(formats::MapPosition to);
    // What a fight shows (and only shows). print: the texts of the IDs
    // joined into one string and printed into the current window
    // (2656:03f5; pieces the original copies into its buffer DG:0012 come
    // as one call); select_box: window 4 made current; clear_box: and
    // cleared; set_fg: the current window's colour, as a role (F:37fe); pause: 1e31:1dcc; draw_view:
    // the view and the figures drawn at the party's place (287f:18ef,
    // 1000:0e4b) with the tiles' next frame (1000:240b), or with `shown`
    // false only the drawer's writes (fix_terrain_without_graphics);
    // picture: 1000:48b7. With combat graphics: show: the page drawn so
    // far made the screen (273d:06c3) and held `frames` (273d:0006); hold:
    // the screen as it is held; sprite: a sprite slot drawn on the page at a
    // screen point (1e31:1f73); line: a shot's line (22e8:000c, plotted
    // by 273d:031c); sound: 1000:19bf.
    struct Hooks {
        std::function<void(int frames)> show;
        std::function<void(int frames)> hold;
        std::function<void(int slot, int x, int y)> sprite;
        std::function<void(const ShownLine& line)> line;
        std::function<void(int n)> sound;
        std::function<void(bool shown)> draw_view;
        // 1000:17bb: the party position walked to a place a cell at a time,
        // the grids moving at each area edge. Unset: the position is set.
        std::function<void(formats::MapPosition to)> walk;
        std::function<void(int number, int mode)> picture;
        std::function<void(const std::vector<std::string>& ids)> print;
        std::function<void()> select_box;
        std::function<void()> clear_box;
        std::function<void(TextRole role)> set_fg;
        // DGROUP:315c, shared with the townspeople's step: 1e31:0006 called
        // with its last argument 0 (the reach test, the paths) clears it.
        std::function<void()> clear_wander;
        std::function<void()> pause;
        std::function<void(const std::string&)> log;
        // 24d3:17c6: the message box opened and made current.
        std::function<void()> message_box;
        // 273d:0259: a key read. When the keys run out it does not return
        // (it throws), so the fight stops where the original waits.
        std::function<void()> key;
        // 1d43:0c72: the Arena's wall cells patched (false) or put back (true).
        std::function<void(bool restore)> arena_wall;
    };

    Combat(const Terrain& terrain, int blocking, formats::MapPosition& party, FightFigures& figs,
           FigureAnimation& animation, std::array<formats::MechRecord, 8>& mechs,
           std::array<CharacterRecord, 16>& characters, Flags& flags, Random& random, CombatState& state,
           MapMarks& marks, const formats::Rules& rules, const formats::Tables& tables, const Fixes& fixes,
           int messages, bool graphics, Hooks hooks);

    // BTECH.EXE 203b:22bc (Ghidra FUN_203b_22bc)
    // 'Mech record m's movement points for an order type (0 walk, 1 run,
    // 2 jump, -1 none), into F:3770.
    int mech_mp(int m, int type);
    // What the last movement-point call left: the points (F:3770) and why
    // they were cut (F:377c the legs, F:4592 the heat).
    int mp() const { return mp_; }
    bool legs_slowed() const { return legs_slowed_; }
    int heat_slowed() const { return heat_slowed_; }
    void clear_slowed() {
        legs_slowed_ = false;
        heat_slowed_ = 0;
    }
    // BTECH.EXE 203b:2474 (Ghidra FUN_203b_2474)
    // A person's movement points; n is the figure as the caller passes it.
    int person_mp(int n);
    // BTECH.EXE 203b:193b (Ghidra FUN_203b_193b)
    // Simulates figure f's orders from where it stands and writes its path.
    void simulate_path(int f);
    // BTECH.EXE 1e31:03ab (Ghidra FUN_1e31_03ab)
    // The computer plans figure f: a target, one order, every weapon at it.
    void plan(int f);
    // BTECH.EXE 203b:1482 (Ghidra FUN_203b_1482)
    // Plans the active figures among the twelve from `first` (0 or 12).
    void plan_side(int first);
    // BTECH.EXE 22e8:000c (Ghidra FUN_22e8_000c)
    // One round of 12 phases. `mission`: a fight of kinds 1-3.
    void round(bool mission);

    // BTECH.EXE 1e31:0bb5 (Ghidra FUN_1e31_0bb5)
    // The distance from the party's position.
    int distance(std::uint16_t x, std::uint16_t y) const;
    // BTECH.EXE 1e31:0f24 (Ghidra FUN_1e31_0f24)
    // The range band (0 short, 1 medium, 2 long, 3 out) from the party's
    // position to figure t with weapon w.
    int range_band(int t, int w) const;
    // BTECH.EXE 1e31:10a2 (Ghidra FUN_1e31_10a2)
    // The weapon in 'Mech record m's slot (item - 1, bit 7 if destroyed), or
    // ff when there is none or its ammunition is out.
    int slot_weapon(int m, int slot) const;
    // BTECH.EXE 1e31:1bfe (Ghidra FUN_1e31_1bfe)
    // Line of sight from the party's position (figure s) to figure t at (x, y).
    bool line_of_sight(int s, int t, std::uint16_t x, std::uint16_t y) const;
    // BTECH.EXE 1e31:16ab (Ghidra FUN_1e31_16ab)
    // Whether the party's position is taken for figure f; with `squash`, a
    // 'Mech steps on enemy people there.
    bool place_taken(int f, bool squash);

private:
    static bool is_mech(int f) { return f < 4 || (f >= 12 && f < 16); }
    static int mech_of(int f) { return f < 12 ? f : f - 8; }
    static int character_of(int f) { return f < 12 ? f - 4 : f - 8; }
    const formats::WeaponRecord& weapon_record(int w) const;
    // 3 x character c's byte 2 / 4, at least 3, halved in heavy armour.
    int foot_mp(int c) const;
    // The cell window (P:07ad) at `index` from the view's corner, around the
    // party's position.
    int window(int index) const;
    // BTECH.EXE 1e31:0006 (Ghidra FUN_1e31_0006)
    // One step of figure f toward (tx, ty) from F:e486/e488, the view place
    // (rel_x, rel_y) followed for the window.
    void step_toward(int f, std::uint16_t tx, std::uint16_t ty, int rel_x, int rel_y);
    // The firing step of one figure and slot (22e8:000c's second loop).
    // True when the slot fired (the figure's turn in this phase is over).
    bool fire(int s, int slot, bool mission, const std::array<std::int8_t, 24>& moved);
    void damage_mech(int s, int t, int w, int dir, int to_hit, bool mission, bool& destroyed);
    void damage_person(int t, int w, int to_hit, bool& killed);
    // BTECH.EXE 1e31:11ab (Ghidra FUN_1e31_11ab)
    void critical(int m, int location);
    // BTECH.EXE 1e31:163e (Ghidra FUN_1e31_163e)
    int destroy_item(int m, int first, int count);
    // BTECH.EXE 1e31:1122 (Ghidra FUN_1e31_1122)
    int transfer(int location);
    // BTECH.EXE 1d43:0a35 (Ghidra FUN_1d43_0a35)
    void destroy_mech(int f);
    // BTECH.EXE 22e8:1e46 (Ghidra FUN_22e8_1e46)
    void fire_check(int t, int dx, int dy);
    // BTECH.EXE 22e8:12c7 (Ghidra FUN_22e8_12c7), the combat graphics'
    // part: the poses, the shot and the explosion (docs/combat.md 3.14).
    void present(int s, int t, int w, int dir);
    // 22e8:12c7's kills and wrecks, which come after it.
    void after_shot(int t, bool killed, bool destroyed, int c);
    void spectator_down();
    // Jason's figure, the view's centre (F:c620: his 'Mech, or figure 4).
    int view_figure() const;
    // The view redrawn around Jason's figure (1000:17bb, then draw_view):
    // 22e8:000c's after the movement, 1e31:1f09 during a shot.
    // 1e31:1f09, or with after_movement the round's own (22e8:1210..1281).
    void redraw(bool after_movement = false);
    void show(int frames) const;
    void sound(int n) const;
    // BTECH.EXE 1e31:0c63 (Ghidra FUN_1e31_0c63)
    void end_round(const std::array<std::int8_t, 24>& moved);
    // BTECH.EXE 1c67:0d7e (Ghidra FUN_1c67_0d7e): every path zeroed.
    void stop_everyone();
    // BTECH.EXE 1e31:1dab (Ghidra FUN_1e31_1dab): prints only with combat
    // messages on (DG:2e38).
    void print(const std::vector<std::string>& ids) const;
    void set_fg(TextRole role) const;
    // The shot's line (22e8:000c, docs/combat.md 3.8).
    void shot_line(int s, int t, int w, int c);
    // A character's name ("character." + its name byte).
    std::string name_id(int c) const;
    void note(const std::string& line) const;

    const Terrain& terrain_;
    int blocking_;  // DG:0150
    formats::MapPosition& party_;
    FightFigures& figs_;
    FigureAnimation& animation_;
    std::array<formats::MechRecord, 8>& mechs_;
    std::array<CharacterRecord, 16>& characters_;
    Flags& flags_;
    Random& random_;
    CombatState& s_;
    MapMarks& marks_;
    const formats::Rules& rules_;
    const formats::Tables& tables_;
    const Fixes& fixes_;
    int messages_;
    bool graphics_;
    void walk(formats::MapPosition to) {
        if (hooks_.walk) hooks_.walk(to);
        else party_ = to;
    }
    Hooks hooks_;

    // Working values the original keeps in F:, between functions.
    int mp_ = 0;                 // F:3770
    bool legs_slowed_ = false;   // F:377c
    int heat_slowed_ = 0;        // F:4592
    formats::MapPosition walker_{};  // F:e486/e488: where a path being simulated has got to
    int step_dx_ = 0, step_dy_ = 0;  // F:458e/4590: the step 1e31:0006 took
    bool hit_ = false;       // 22e8:000c's [bp-0x56]: the shot hit
    bool hit_known_ = false;  // set by a shot this round (bug 5: unset at first)
    bool person_hit_ = false;  // [bp-0x6]: a person was hit (its reaction is shown)
    bool target_taken_ = false;  // F:d57e
    bool destroyed_ = false;     // F:e484
    bool critical_hit_ = false;  // F:4586
    bool arm_off_ = false;       // F:3986
    int kick_damage_ = 0;        // DG:3103: the Kick's damage byte
};

}  // namespace chr::game
