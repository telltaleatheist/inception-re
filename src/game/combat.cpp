// The fight's rules (docs/combat.md 3 and 4): one function per original
// function where it has one, in the order the round calls them.
#include "game/combat.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

#include "game/map_view.h"
#include "game/townspeople.h"
#include "game/world.h"

namespace chr::game {

namespace {

// BTECH.EXE 1e31:1df8 (Ghidra FUN_1e31_1df8)
// Where `to` falls in the view centred on `from`, in text cells (the
// centre is column 0x1a, row 0x0c), walking one half-cell at a time across
// area edges; for a figure the drawer did not draw.
std::pair<int, int> view_place(formats::MapPosition from, formats::MapPosition to) {
    int col = 0x1a, row = 0x0c;
    std::uint16_t x = from.x, y = from.y;
    for (int guard = 0; guard < 0x10000 && y > to.y; ++guard) {
        --y;
        if (y & 0x80) y &= 0xf07f;
        --row;
    }
    for (int guard = 0; guard < 0x10000 && y < to.y; ++guard) {
        ++y;
        if (y & 0x80) y = std::uint16_t(y + 0xf80);
        ++row;
    }
    for (int guard = 0; guard < 0x10000 && x > to.x; ++guard) {
        --x;
        if (x & 0x80) x &= 0x0f7f;
        --col;
    }
    for (int guard = 0; guard < 0x10000 && x < to.x; ++guard) {
        ++x;
        if (x & 0x80) x = std::uint16_t(x + 0x80);
        ++col;
    }
    return {col, row};
}

using formats::MapPosition;

// The linear grid 1e31:0bb5 and 1e31:16ab measure on: an area's 128
// half-cells side by side with the next area's.
int linear_x(std::uint16_t x) { return ((x & 0x0f00) >> 1) | (x & 0x7f); }
int linear_y(std::uint16_t y) { return ((y & 0xf000) >> 5) | (y & 0x7f); }

// x + 1 into the next area at the edge, as 1e31:16ab and 1e31:03ab do it.
std::uint16_t plus_one_x(std::uint16_t x) {
    x = std::uint16_t(x + 1);
    if (x & 0x80) x = std::uint16_t(x + 0x80);
    return x;
}

// The area byte of an order (high nibbles of x and y).
std::uint8_t order_area(std::uint16_t x, std::uint16_t y) { return std::uint8_t(((x & 0x0f00) | (y & 0xf000)) >> 8); }

}  // namespace

// BTECH.EXE 1e31:1ea2 (Ghidra FUN_1e31_1ea2)
// The octant of a line, from the screen's y difference (start minus end)
// and x difference (end minus start).
int octant(int dy, int dx) {
    if (dy < 0) {
        if (dx > 0) return -dy > dx ? 3 : 2;
        return -dx <= -dy ? 4 : 5;
    }
    if (dx > 0) return dx <= dy ? 0 : 1;
    return -dx >= dy ? 6 : 7;
}

int d6(Random& random) {
    for (;;) {
        const int r = random.next() & 7;
        if (r <= 5) return r + 1;
    }
}

int two_d6(Random& random) {
    const int a = d6(random);
    return a + d6(random);
}

void MapMarks::add(int code, std::uint16_t x, std::uint16_t y) {
    auto& m = marks[std::size_t(next)];
    m.code = std::uint8_t(code);
    m.area = order_area(x, y);
    m.x = std::uint8_t(x & 0x7f);
    m.y = std::uint8_t(y & 0x7f);
    if (++next > kCount - 1) next = 0;
}

Combat::Combat(const Terrain& terrain, int blocking, MapPosition& party, FightFigures& figs,
               FigureAnimation& animation, std::array<formats::MechRecord, 8>& mechs,
               std::array<CharacterRecord, 16>& characters, Flags& flags, Random& random, CombatState& state,
               MapMarks& marks, const formats::Rules& rules, const formats::Tables& tables, const Fixes& fixes,
               int messages, bool graphics, Hooks hooks)
    : terrain_(terrain), blocking_(blocking), party_(party), figs_(figs), animation_(animation), mechs_(mechs),
      characters_(characters), flags_(flags), random_(random), s_(state), marks_(marks), rules_(rules),
      tables_(tables), fixes_(fixes), messages_(messages), graphics_(graphics), hooks_(std::move(hooks)) {}

const formats::WeaponRecord& Combat::weapon_record(int w) const {
    static const formats::WeaponRecord kNone{};
    if (w < 0 || std::size_t(w) >= rules_.weapons.size()) {
        note("a weapon number past the table: " + std::to_string(w));
        return kNone;
    }
    return rules_.weapons[std::size_t(w)];
}

int Combat::window(int index) const {
    const auto cell = window_cell(terrain_, party_, view_corner_index(party_) + index);
    if (!cell) {
        // Memory the rebuild does not hold (docs/wilderness.md).
        note("a cell past the window's memory: blocked");
        return 0xff;
    }
    return *cell;
}

void Combat::print(const std::vector<std::string>& ids) const {
    if (messages_ != 0 && hooks_.print) hooks_.print(ids);
}

void Combat::set_fg(TextRole role) const {
    if (hooks_.set_fg) hooks_.set_fg(role);
}

std::string Combat::name_id(int c) const {
    if (c < 0 || c > 15) {
        note("a name of a character past the records");
        return "character.0";
    }
    return "character." + std::to_string(int(std::int8_t(characters_[std::size_t(c)][character::kName])));
}

void Combat::shot_line(int s, int t, int w, int c) {
    const bool full = messages_ == 2, kick = w == weapon::kKick;
    if (messages_ != 0 && hooks_.clear_box) hooks_.clear_box();
    set_fg(TextRole::PartyAction);
    if (s < 12) {
        if (full) print({name_id(c)});
        if (s < 4) {
            if (full) print({"shot.party_mech"});
            if (kick) print({messages_ == 1 ? "shot.party_kick" : "shot.party_kicks"});
            else if (full) print({"shot.party_uses"});
        } else if (full) {
            print({"shot.party_person_uses"});
        }
    } else {
        set_fg(TextRole::EnemyAction);
        if (full) print({"shot.enemy"});
        if (s < 16) {
            if (full) print({"shot.enemy_mech"});
            if (kick) print({messages_ == 1 ? "shot.enemy_kick" : "shot.enemy_kicks"});
            else if (full) print({"shot.enemy_uses"});
        } else if (full) {
            print({"shot.enemy_human_uses"});
        }
    }
    if (!kick) print({formats::weapon_text_id(w)});
    if (full) print({"shot.on"});
    if (!full) return;
    if (t < 12) {
        std::vector<std::string> line;
        if (t < 4) line = {name_id(mechs_[std::size_t(t)][mech::kPilot]), "shot.target_mech"};
        else line = {name_id(t - 4)};
        line.push_back("shot.target_end");
        print(line);
    } else if (s_.spectator && t == 13) {
        print({"shot.target_spectator"});
    } else {
        print({"shot.target_enemy"});
        print({t < 16 ? "shot.target_enemy_mech" : "shot.target_enemy_human"});
    }
}

void Combat::note(const std::string& line) const {
    if (hooks_.log) hooks_.log(line);
}

int Combat::mech_mp(int m, int type) {
    const auto& r = mechs_[std::size_t(m)];
    heat_slowed_ = 0;
    legs_slowed_ = false;
    mp_ = r[mech::kWalk];
    if (type == 2) {
        mp_ = r[mech::kJump];
    } else {
        const int left = r[mech::kLeftActuators], right = r[mech::kRightActuators];
        if (((left | right) & 8) == 0) {
            // No hip left: one point, whatever else is gone.
            mp_ = 1;
            legs_slowed_ = true;
        } else {
            if (!(left & 8) || !(right & 8)) {
                mp_ >>= 1;
                if (r[mech::kWalk] & 1) ++mp_;
                legs_slowed_ = true;
            }
            for (int bit = 4; bit > 0; bit >>= 1) {
                if (!(left & bit)) --mp_, legs_slowed_ = true;
                if (!(right & bit)) --mp_, legs_slowed_ = true;
                // Bug 6: lifted to 1 only when exactly 0.
                if (legs_slowed_ && (fixes_.fix_actuator_min ? mp_ < 1 : mp_ == 0)) mp_ = 1;
            }
        }
        heat_slowed_ = s_.heat[std::size_t(m)] / 5;
        mp_ -= heat_slowed_;
        if (type == 1) {
            // Bug 7: the +1 tests the walking MP for oddness, not the MP now.
            const bool odd = fixes_.fix_run_parity ? (mp_ & 1) : (r[mech::kWalk] & 1);
            mp_ += mp_ >> 1;
            if (odd) ++mp_;
        }
    }
    if (s_.heat[std::size_t(m)] == 30) mp_ = 0;
    if (mp_ < 0) mp_ = 0;
    return mp_;
}

int Combat::person_mp(int n) {
    mp_ = n < 12 ? foot_mp(n - 4) : 6;
    if (mp_ > 8) mp_ = 8;
    return mp_;
}

int Combat::foot_mp(int c) const {
    const auto& r = characters_[std::size_t(c)];
    int mp = (3 * std::int8_t(r[character::kD])) >> 2;
    if (mp < 3) mp = 3;
    if (std::int8_t(r[character::kArmourType]) > 1) mp >>= 1;
    return mp;
}

int Combat::distance(std::uint16_t x, std::uint16_t y) const {
    const int dx = std::abs(linear_x(party_.x) - linear_x(x));
    const int dy = std::abs(linear_y(party_.y) - linear_y(y));
    // Bug 1: min + max / 2.
    if (fixes_.fix_distance) return dx > dy ? dx + (dy >> 1) : dy + (dx >> 1);
    return dx > dy ? (dx >> 1) + dy : (dy >> 1) + dx;
}

int Combat::range_band(int t, int w) const {
    std::uint16_t x = figs_.x[std::size_t(t)], y = figs_.y[std::size_t(t)];
    if (is_mech(t) && distance(x, y) > 3) {
        // The aim point: a half-cell toward the shooter, and up two when it
        // is above (a 'Mech is three half-cells tall).
        if (party_.x < x) x = step_x(x, -1);
        if (party_.x > x) x = step_x(x, 1);
        if (party_.y < y) {
            y = std::uint16_t(y - 2);
            if (y & 0x80) y &= 0xf07f;
        }
    }
    const int d = distance(x, y);
    const auto& r = weapon_record(w);
    int band = 3;
    if (r[weapon::kLong] > d) band = 2;
    int medium = r[weapon::kShortMedium] & 0x1f, shortest = r[weapon::kShortMedium] >> 5;
    if (r[weapon::kShots] < 0x80 && w != weapon::kKick) {
        medium *= 3;
        shortest *= 3;
    }
    if (d < medium) band = 1;
    if (d < shortest) band = 0;
    return band;
}

int Combat::slot_weapon(int m, int slot) const {
    const auto& r = mechs_[std::size_t(m)];
    int n = 0;
    for (int i = mech::kItems; i < mech::kItemsEnd; ++i) {
        const int item = r[std::size_t(i)];
        const int code = item & 0x7f;
        if (code < 0x10 || code > 0x20) continue;
        if (n++ != slot) continue;
        // Bug 34: an 11th weapon would read the walking MP as its ammunition.
        if (r[std::size_t(mech::kAmmunition + slot)] == 0) return 0xff;
        return item - 1;
    }
    return 0xff;
}

bool Combat::line_of_sight(int s, int t, std::uint16_t tx, std::uint16_t ty) const {
    (void)s;
    terrain_.release_window();  // 287f:1314, 1df8: the window built at the party's place
    if (s_.spectator && t == 13) return true;
    int index = 0x96;  // the view's middle cell
    int half_x = 1;
    int half_y = party_.y & 1;
    if (party_.x & 1) {
        ++index;
        half_x = 0;
    }
    MapPosition at = party_;
    int dir = direction_toward(at, {tx, ty}, tables_);
    bool clear = true;
    // A shooter standing on a blocking tile sees nothing.
    if (window(index) >= blocking_) return false;
    while (at.x != tx || at.y != ty) {
        const int want = direction_toward(at, {tx, ty}, tables_);
        const int diff = want - dir;
        if (diff != 0) dir = ((diff & 7) < 5 ? dir + 1 : dir - 1);
        dir &= 7;
        const int dx = tables_.los_step_x[std::size_t(dir)], dy = tables_.los_step_y[std::size_t(dir)];
        at.x = std::uint16_t(at.x + dx);
        if (at.x & 0x80) at.x = std::uint16_t(at.x + tables_.los_area_x[std::size_t(dir)]);
        at.y = std::uint16_t(at.y + dy);
        if (at.y & 0x80) at.y = std::uint16_t(at.y + tables_.los_area_y[std::size_t(dir)]);
        if (dx != 0) {
            half_x = (half_x + dx) & 1;
            if (half_x == 0) index += dx;
        }
        if (dy != 0) {
            half_y = (half_y + dy) & 1;
            if (half_y == 0) index += tables_.los_row[std::size_t(dir)];
        }
        if (window(index) >= blocking_) {
            at = {tx, ty};
            clear = false;
        }
    }
    return clear;
}

bool Combat::place_taken(int f, bool squash) {
    const MapPosition at = party_;
    if (!is_mech(f)) {
        // A person: another person on the same half-cell, or a 'Mech's
        // three half-cells (x - 1, x, x + 1), active or not.
        for (int j = 4; j < 12; ++j) {
            if (j != f && figs_.x[std::size_t(j)] == at.x && figs_.y[std::size_t(j)] == at.y) return true;
            if (j + 12 != f && figs_.x[std::size_t(j + 12)] == at.x && figs_.y[std::size_t(j + 12)] == at.y)
                return true;
        }
        for (int j = 0; j < 4; ++j)
            for (const int k : {0, 12}) {
                const int m = j + k;
                if (figs_.y[std::size_t(m)] != at.y) continue;
                const std::uint16_t mx = figs_.x[std::size_t(m)];
                if (mx == at.x || step_x(mx, -1) == at.x || step_x(mx, 1) == at.x) return true;
            }
        return false;
    }
    // A 'Mech: another active 'Mech on the row within 2, or one of its own
    // side's people within 1.
    const int here = linear_x(plus_one_x(at.x));
    for (int j = 0; j < 4; ++j)
        for (const int k : {0, 12}) {
            const int m = j + k;
            if (figs_.active[std::size_t(m)] == 0 || m == f || figs_.y[std::size_t(m)] != at.y) continue;
            if (std::abs(linear_x(plus_one_x(figs_.x[std::size_t(m)])) - here) < 3) return true;
        }
    int base = f < 12 ? 4 : 16;
    const int middle = here - 1;
    for (int p = base; p < base + 8; ++p) {
        if (figs_.active[std::size_t(p)] == 0 || figs_.y[std::size_t(p)] != at.y) continue;
        if (std::abs(middle - linear_x(figs_.x[std::size_t(p)])) < 2) return true;
    }
    if (!squash) return false;
    // The other side's people underfoot are crushed.
    base ^= 0x14;
    for (int p = base; p < base + 8; ++p) {
        if (figs_.active[std::size_t(p)] == 0 || figs_.y[std::size_t(p)] != at.y) continue;
        if (std::abs(middle - linear_x(figs_.x[std::size_t(p)])) >= 2) continue;
        const int c = character_of(p);
        if (hooks_.clear_box) hooks_.clear_box();
        print({p < 12 ? name_id(c) : "shot.enemy_human"});
        print({"shot.squashed"});
        sound(0x12);  // 1000:19bf
        if (hooks_.timed_pause) {  // 1e31:16ab: DG:015e below 5 only
            hooks_.timed_pause();
            if (c < 8) hooks_.timed_pause();
        }
        marks_.add(0x7e, figs_.x[std::size_t(p)], figs_.y[std::size_t(p)]);
        animation_.set_frame(p, 0x7e);
        figs_.sprite_base[std::size_t(p)] = 0;
        figs_.active[std::size_t(p)] = 0;
        characters_[std::size_t(c)][character::kHitPoints] = 0;
        characters_[std::size_t(c)][character::kName] = character::kEmpty;
        if (c < 2) s_.fight_on = false;
    }
    return false;
}

void Combat::step_toward(int f, std::uint16_t tx, std::uint16_t ty, int rel_x, int rel_y) {
    // The planner's paths never wobble (the townspeople's do: 1e31:0006's
    // last argument), and each of their steps clears the shared pattern.
    const int wobble = 0;
    if (hooks_.clear_wander) hooks_.clear_wander();
    int d = figs_.heading[std::size_t(f & 0x7f)];
    step_dx_ = step_dy_ = 0;
    const bool mech = (f & 0x7f) < 4 || (f >= 12 && f < 16);
    const int want = direction_toward(walker_, {tx, ty}, tables_);
    if (want == -1) return;
    const int diff = want - d;
    if (diff != 0) d = (((diff & 7) < 5) ? d + 1 : d - 1) & 7;
    for (int k = 7; k >= 0; --k) {
        d = (d + tables_.turns[std::size_t(wobble + k)]) & 7;
        const int dx = tables_.step_x[std::size_t(d)], dy = tables_.step_y[std::size_t(d)];
        MapPosition next = walker_;
        if (dx != 0) next.x = step_x(next.x, dx);
        if (dy != 0) next.y = step_y(next.y, dy);
        const int vx = rel_x + dx, vy = rel_y + dy;
        int index = (vy >> 1) * kWindowCells + ((vx - 0x0d) >> 1);
        if (!(vx & 1) && (party_.x & 1)) ++index;
        if ((vy & 1) && (party_.y & 1)) index += kWindowCells;
        if (window(index) >= blocking_) continue;
        bool free = true;
        if (mech) {
            // A 'Mech stands on two cells: the one beside must not block either.
            const int other = ((vx ^ party_.x) & 1) == 0 ? index + 1 : index - 1;
            if (window(other) >= blocking_) free = false;
        }
        if (free && f < 0x80) {
            const MapPosition saved = party_;
            party_ = next;
            if (place_taken(f, false)) {
                free = false;
                if (next.x == tx && next.y == ty) target_taken_ = true;
            }
            party_ = saved;
        }
        if (!free) continue;
        walker_ = next;
        figs_.heading[std::size_t(f & 0x7f)] = std::int8_t(d);
        step_dx_ = dx;
        step_dy_ = dy;
        return;
    }
}

bool Combat::reaches(MapPosition to) {
    walker_ = party_;
    int rel_x = 0x1a, rel_y = 0x0c;
    if (walker_ == to) return false;  // 203b:2dad: already there counts as no
    for (int count = 30; count > 0; --count) {  // F:3770
        step_toward(0x80, to.x, to.y, rel_x, rel_y);
        rel_x += step_dx_;
        rel_y += step_dy_;
        if (walker_ == to) return true;
    }
    return false;
}

void Combat::simulate_path(int f) {
    auto& path = s_.paths[std::size_t(f)];
    path.fill(2);
    const auto& orders = s_.orders[std::size_t(f)];
    std::size_t read = 0, written = 0;
    walker_ = party_;
    int rel_x = 0x1a, rel_y = 0x0c;
    const int heading = figs_.heading[std::size_t(f)];
    for (;;) {
        if (read + 4 > orders.size()) {
            // Bug 17's other side: past the 12 orders the original reads the
            // next figure's.
            note("a path past the twelfth order: stopped");
            break;
        }
        const int type = std::int8_t(orders[read++]);
        if (type == -1) break;
        const int area = orders[read++];
        const std::uint16_t tx = std::uint16_t(int(std::int8_t(orders[read++])) | ((area << 8) & 0x0f00));
        const std::uint16_t ty = std::uint16_t(int(std::int8_t(orders[read++])) | ((area << 8) & 0xf000));
        // A party figure the player moves (or one without a heading) faces
        // its waypoint at once; others turn as they go.
        if ((f < 12 && !figs_.computer[std::size_t(f)]) || heading == -1)
            figs_.heading[std::size_t(f)] = std::int8_t(direction_toward(walker_, {tx, ty}, tables_));
        target_taken_ = false;
        while (mp_ > 0) {
            if (walker_.x == tx && walker_.y == ty) break;
            if (target_taken_) break;
            step_toward(f, tx, ty, rel_x, rel_y);
            rel_x += step_dx_;
            rel_y += step_dy_;
            if (written + 2 <= path.size()) {
                path[written] = std::int8_t(step_dx_);
                path[written + 1] = std::int8_t(step_dy_);
            } else {
                // Bug 18: no bound in the original.
                note("a path of more than 12 steps: the rest dropped");
            }
            written += 2;
            int index = (rel_y >> 1) * kWindowCells + ((rel_x - 0x0d) >> 1);
            int quarters = tables_.path_quarters[std::size_t((rel_x & 1) + (rel_y & 1) * 2)];
            if ((rel_x & 1) && (party_.x & 1)) {
                ++index;
                quarters ^= 10;
            }
            if ((rel_y & 1) && (party_.y & 1)) {
                index += kWindowCells;
                quarters ^= 5;
            }
            const int tile = window(index);
            int cost = 1;
            if (type != 2 && tile < 0x40) {
                if (tile & quarters) cost = 2;
                if ((tile & 0xf0) == 0x20 && (tile & quarters)) cost = 3;
            }
            mp_ -= cost;
            if (mp_ < 0) mp_ = 0;
        }
    }
    figs_.heading[std::size_t(f)] = std::int8_t(heading);
}

void Combat::plan(int f) {
    const MapPosition saved = party_;
    walk({figs_.x[std::size_t(f)], figs_.y[std::size_t(f)]});  // 1000:17bb
    auto& orders = s_.orders[std::size_t(f)];
    orders.fill(0xff);
    const bool enemy = f >= 12;
    if (!enemy) figs_.computer[std::size_t(f)] = true;
    const int local = enemy ? f - 12 : f;  // 0-3 a 'Mech, 4-11 a person
    const int shifted = local + (enemy ? 4 : 0);
    // A 'Mech runs when cool, else walks; people walk.
    const int mode = (local > 3 || s_.heat[std::size_t(shifted)] != 0) ? 0 : 1;
    if (local < 4) mech_mp(shifted, mode);
    else if (fixes_.fix_enemy_foot_mp && enemy) mp_ = std::min(foot_mp(f - 8), 8);  // bug 8: its own record
    else person_mp(shifted);

    // The nearest target, from a first list and then a second.
    int count = local > 3 ? 8 : 4;
    int next = local > 3 ? (enemy ? 4 : 16) : (enemy ? 0 : 12);
    int target = 0x17;
    std::uint16_t tx = 0xffff, ty = 0xffff;
    int best = 0x7fff;
    while (count-- != 0) {
        const int c = next;
        if (figs_.active[std::size_t(c)] != 0) {
            bool candidate = true;
            if (s_.spectator && c == 13) candidate = false;
            // The enemy leaves the traitor on foot alone.
            if (enemy && flags_[0x27] != 0 && flags_[0x25] + 4 == c) candidate = false;
            if (candidate) {
                const int d = distance(figs_.x[std::size_t(c)], figs_.y[std::size_t(c)]);
                if (d < best) {
                    best = d;
                    tx = figs_.x[std::size_t(c)];
                    ty = figs_.y[std::size_t(c)];
                    target = c;
                }
            }
        }
        ++next;
        if (count == 0 && tx == 0xffff) {
            if (next == 12 || next == 24) next -= 12;
            count = 8;
        }
    }

    // How close it means to get: a 'Mech four times the shortest short limit
    // among its intact weapons, a person its weapon's short limit.
    int reach = 0;
    if (f < 16 && (f < 4 || f > 11)) {
        int least = 0xff;
        const auto& r = mechs_[std::size_t(mech_of(f))];
        for (int i = mech::kItems; i < mech::kItemsEnd; ++i) {
            const int item = r[std::size_t(i)];
            if (item == 0 || (item & 0x80) || item < 0x10 || item > 0x20) continue;
            // Bug 9: the item code, not item - 1, picks the weapon.
            const int w = fixes_.fix_planner_reach_index ? item - 1 : item;
            const int s = weapon_record(w)[weapon::kShortMedium] & 0xe0;
            if (s < least) least = s;
        }
        // Bug 10: 4 x S (S << 5 >> 3) where the bands use 3 x S.
        reach = least == 0xff ? 0 : (fixes_.fix_planner_reach_scale ? 3 * (least >> 5) : least >> 3);
    } else {
        const int w = std::int8_t(characters_[std::size_t(character_of(f))][character::kWeapon]);
        reach = (weapon_record(w)[weapon::kShortMedium] >> 5) & 7;
    }
    bool sight = line_of_sight(f, target, figs_.x[std::size_t(target)], figs_.y[std::size_t(target)]);

    // A person facing a 'Mech runs: six half-cells away on each axis.
    if ((f > 15 || (f > 3 && f < 12)) && is_mech(target)) {
        // Bug 13: an enemy person writes 0 over a party person's 1.
        if (!(fixes_.fix_retreat_flag && f >= 12)) s_.person_ran = f < 12;
        if (f > 15) s_.enemy_people_ran = true;
        const std::uint16_t px = party_.x, py = party_.y;
        if (tx < px) {
            tx = std::uint16_t(px + 6);
            if (tx & 0x80) tx = std::uint16_t(px + 0x86);
        } else if (px < tx) {
            tx = std::uint16_t(px - 6);
            if (tx & 0x80) tx &= 0x0f7f;
        }
        if (ty < py) {
            ty = std::uint16_t(py + 6);
            if (ty & 0x80) ty = std::uint16_t(py + 0xf86);
        } else if (py < ty) {
            ty = std::uint16_t(py - 6);
            if (ty & 0x80) ty &= 0xf07f;
        }
        sight = false;
        reach = 0;
    }

    bool write_order = true;
    if (reach != 0 && sight) {
        if (best > reach) {
            // A stop point d - R steps from the figure toward the target,
            // the direction fixed once (bug 11).
            MapPosition stop = party_;
            const int sx = stop.x > tx ? -1 : stop.x < tx ? 1 : 0;
            const int sy = stop.y > ty ? -1 : stop.y < ty ? 1 : 0;
            for (int d = best; d > reach; --d) {
                if (sx != 0 && !(fixes_.fix_planner_stop_point && stop.x == tx)) stop.x = step_x(stop.x, sx);
                if (sy != 0 && !(fixes_.fix_planner_stop_point && stop.y == ty)) stop.y = step_y(stop.y, sy);
            }
            tx = stop.x;
            ty = stop.y;
        } else {
            write_order = false;  // in reach and in sight: it stands
        }
    }
    if (write_order) {
        orders[0] = std::uint8_t(mode);
        orders[1] = order_area(tx, ty);
        orders[2] = std::uint8_t(tx & 0x7f);
        orders[3] = std::uint8_t(ty & 0x7f);
    }

    auto& targets = s_.targets[std::size_t(f)];
    if ((f < 4 || f > 11) && f < 16) {
        if ((f < 4 && target < 16) || (f > 11 && f < 16 && target < 4)) {
            // In contact with a 'Mech it sees: it stands and faces it.
            const int fx = linear_x(figs_.x[std::size_t(f)]) + 1, fy = linear_y(figs_.y[std::size_t(f)]);
            const int ox = linear_x(figs_.x[std::size_t(target)]) + 1, oy = linear_y(figs_.y[std::size_t(target)]);
            if (sight && std::abs(ox - fx) <= 3 && std::abs(oy - fy) < 2) {
                animation_.set_direction(f, -1);
                orders[0] = 0xff;
                figs_.heading[std::size_t(f)] = std::int8_t(
                    direction_toward({figs_.x[std::size_t(f)], figs_.y[std::size_t(f)]},
                                     {figs_.x[std::size_t(target)], figs_.y[std::size_t(target)]}, tables_));
            }
        }
        targets.fill(0xff);
        const int m = mech_of(f);
        if (s_.heat[std::size_t(m)] < 30) {
            for (int slot = 0; slot < 10; ++slot)
                if (slot_weapon(m, slot) != 0xff) targets[std::size_t(slot)] = std::uint8_t(target);
            targets[11] = std::uint8_t(target);
        } else {
            // Shut down: no order, nothing fires.
            orders[0] = 0xff;
            for (auto& t : targets) t |= 0x80;
        }
    } else {
        targets[0] = std::uint8_t(target);
        if (flags_[0x27] != 0 && flags_[0x25] + 4 == f) targets[0] = 0xff;
    }
    simulate_path(f);
    walk(saved);  // 1000:17bb
}

void Combat::plan_side(int first) {
    for (int i = 0; i < 12; ++i)
        if (figs_.active[std::size_t(first + i)] != 0) plan(first + i);
}

int Combat::transfer(int location) {
    if (location >= 0x11 && location <= 0x18) return location + 0x0b;
    switch (location) {
    case 0x19: return 0x1d;
    case 0x1a: return 0x20;
    case 0x1b: return 0x22;
    case 0x1c: case 0x1e: return 0x12;
    case 0x1d: case 0x22: return 0x15;
    case 0x21: case 0x23: return 0x17;
    case 0x1f: case 0x20:
        destroyed_ = true;
        // The original returns an unset value here; the record is gone by
        // the time it would be read.
        return location;
    default: return location;
    }
}

int Combat::destroy_item(int m, int first, int count) {
    auto& r = mechs_[std::size_t(m)];
    int intact = 0;
    for (int i = 0; i < count; ++i) {  // 1e31:15fa
        const int item = r[std::size_t(first + i)];
        if (item != 0 && !(item & 0x80)) ++intact;
    }
    if (intact == 0) return 0;
    int i = random_.next() & 3;
    for (;;) {
        if (i >= count) i = 0;
        auto& item = r[std::size_t(first + i)];
        if (item != 0 && !(item & 0x80)) {
            item |= 0x80;
            return 1;
        }
        ++i;
    }
}

void Combat::critical(int m, int location) {
    auto& r = mechs_[std::size_t(m)];
    const bool gone = r[std::size_t(location)] == 0;
    int count = 1;
    const int roll = two_d6(random_);
    if (roll >= 8) count = ((roll - 8) >> 1) + 1;
    else if (fixes_.fix_critical_count) count = 0;  // bug 3
    if (messages_ != 0 && count != 0) {
        if (hooks_.select_box) hooks_.select_box();
        print({"shot.critical"});
        critical_hit_ = true;
    }
    const int part = location - mech::kStructure;
    if (gone) {
        // The location is gone: everything in it, and an arm's or a leg's
        // actuators.
        count = 0;
        const int first = tables_.location_slot_first[std::size_t(part)];
        for (int i = 0; i < tables_.location_slot_count[std::size_t(part)]; ++i)
            if (r[std::size_t(first + i)] != 0) r[std::size_t(first + i)] |= 0x80;
        if (location == 0x1c) r[mech::kLeftActuators] &= 0x0f;
        if (location == 0x1e) r[mech::kLeftActuators] &= 0xf0;
        if (location == 0x21) r[mech::kRightActuators] &= 0x0f;
        if (location == 0x23) r[mech::kRightActuators] &= 0xf0;
    }
    while (count != 0) {
        switch (location) {
        case mech::kHead:
            switch (d6(random_)) {
            case 1: case 6:
                if (r[mech::kLifeSupport] != 0) {
                    r[mech::kLifeSupport] = 0xff;
                    --count;
                }
                break;
            case 2: case 5:
                if (r[mech::kSensorHits] < 2) {
                    ++r[mech::kSensorHits];
                    --count;
                }
                break;
            case 3:
                count = 0;
                r[mech::kHead] = 0;
                destroyed_ = true;
                break;
            case 4: count -= destroy_item(m, 0x55, 1); break;
            }
            break;
        case mech::kCentreTorso:
            if (d6(random_) > 3) {
                count -= destroy_item(m, 0x53, 2);
            } else if (d6(random_) > 3) {
                --count;
                if (++r[mech::kEngineHits] >= 3) {
                    count = 0;
                    r[mech::kCentreTorso] = 0;
                    destroyed_ = true;
                }
            } else {
                --count;
                if (++r[mech::kGyroHits] >= 2) {
                    count = 0;
                    r[mech::kCentreTorso] = 0;
                    destroyed_ = true;
                }
            }
            break;
        case 0x1e: case 0x23: {  // the legs
            const int bits = location == 0x23 ? mech::kRightActuators : mech::kLeftActuators;
            const int slots = location == 0x23 ? 0x51 : 0x4f;
            if (!(r[std::size_t(bits)] & 0x0f)) {
                int intact = 0;
                for (int i = 0; i < 2; ++i)
                    if (r[std::size_t(slots + i)] != 0 && !(r[std::size_t(slots + i)] & 0x80)) ++intact;
                if (intact == 0) {
                    count = 0;
                    break;
                }
            }
            const int v = d6(random_);
            if (v >= 3 && (r[std::size_t(bits)] & 0x0f)) {
                if (r[std::size_t(bits)] & tables_.leg_actuator_bit[std::size_t(v)]) {
                    r[std::size_t(bits)] &= std::uint8_t(tables_.leg_actuator_keep[std::size_t(v)]);
                    --count;
                }
            } else {
                count -= destroy_item(m, slots, 2);
            }
            break;
        }
        case 0x1d: case 0x22: {  // the side torsos
            const int slots = location == 0x22 ? 0x48 : 0x3a;
            int intact = 0;
            for (int i = 0; i < 7; ++i)
                if (r[std::size_t(slots + i)] != 0 && !(r[std::size_t(slots + i)] & 0x80)) ++intact;
            if (intact == 0) count = 0;
            else count -= destroy_item(m, slots, 7);
            break;
        }
        case mech::kLeftArm: case mech::kRightArm: {
            const int bits = location == mech::kRightArm ? mech::kRightActuators : mech::kLeftActuators;
            const int slots = location == mech::kRightArm ? 0x41 : 0x33;
            if (!(r[std::size_t(bits)] & 0xf0)) {
                int intact = 0;
                for (int i = 0; i < 7; ++i)
                    if (r[std::size_t(slots + i)] != 0 && !(r[std::size_t(slots + i)] & 0x80)) ++intact;
                if (intact == 0) {
                    count = 0;
                    break;
                }
            }
            const int v = d6(random_);
            if (v >= 3 && (r[std::size_t(bits)] & 0xf0)) {
                if (r[std::size_t(bits)] & tables_.arm_actuator_bit[std::size_t(v)]) {
                    r[std::size_t(bits)] &= std::uint8_t(tables_.arm_actuator_keep[std::size_t(v)]);
                    --count;
                }
            } else {
                count -= destroy_item(m, slots, 7);
            }
            break;
        }
        default: count = 0; break;
        }
    }
}

void Combat::destroy_mech(int f) {
    figs_.active[std::size_t(f)] = 0;
    figs_.destroyed[std::size_t(f)] = true;
    for (auto& row : s_.targets)
        for (auto& t : row)
            if (std::int8_t(t) == f) t = 0xff;
    const int m = mech_of(f);
    auto& r = mechs_[std::size_t(m)];
    s_.wreck_letter[std::size_t(m)] = r[0];
    r[0] = 0xff;
    // 1d43:0a35 in the Arena: the opponent's fall takes the spectator out of
    // the fight; the spectator's own fall breaks the Arena's wall (1d43:0c72).
    if (s_.spectator && m == 4) {
        mechs_[5][0] = 0xff;
        figs_.active[13] = 0;
    } else if (s_.spectator && m == 5 && hooks_.arena_wall) {
        hooks_.arena_wall(false);
    }
    critical_hit_ = false;
    arm_off_ = false;
    if (hooks_.select_box) hooks_.select_box();
    set_fg(TextRole::Report);  // 1000:28a2
    if (!(s_.spectator && f == 13)) print({"destroyed.mech"});
    if (m < 4) {
        // A party 'Mech: its crew ejects at its place, the second a
        // half-cell right.
        if (hooks_.print) hooks_.print({"destroyed.eject"});  // 2656:03f5: whatever the message level
        for (const int seat : {mech::kPilot, mech::kSecond}) {
            const int c = r[std::size_t(seat)];
            if (c == 0xff) continue;
            characters_[std::size_t(c)][character::kMech] = character::kOnFoot;
            figs_.active[std::size_t(c + 4)] = 1;
            figs_.x[std::size_t(c + 4)] = seat == mech::kPilot ? figs_.x[std::size_t(m)] : plus_one_x(figs_.x[std::size_t(m)]);
            figs_.y[std::size_t(c + 4)] = figs_.y[std::size_t(m)];
        }
        if (hooks_.timed_pause) hooks_.timed_pause();  // 1d43:0a35: DG:015e below 5 only
    }
    set_fg(TextRole::Text);  // F:37fe
}

void Combat::fire_check(int t, int dx, int dy) {
    const int roll = two_d6(random_);
    if (roll >= 5 && roll <= 9) return;
    std::uint16_t x = std::uint16_t(figs_.x[std::size_t(t)] + dx), y = std::uint16_t(figs_.y[std::size_t(t)] + dy);
    if (x & 0x80) x &= 0x0f7f;
    if (y & 0x80) y &= 0xf07f;
    marks_.add(0x7c, x, y);
}

void Combat::stop_everyone() {
    for (auto& p : s_.paths) p.fill(0);
}

void Combat::damage_mech(int s, int t, int w, int dir, int to_hit, bool mission, bool& destroyed) {
    const int m = mech_of(t);
    auto& r = mechs_[std::size_t(m)];
    to_hit += s_.terrain[std::size_t(t)] >> 3;
    int facing = animation_.direction(t);
    if (facing == -1) facing = s_.parked_facing[std::size_t(t)];
    const int side = rules_.combat.facing_sides[std::size_t(std::clamp(facing - dir + 7, 0, 14))];
    int location = rules_.combat.hit_locations[std::size_t(side & 3)][std::size_t(two_d6(random_) - 2)] + mech::kArmour;
    if (w == weapon::kKick) {
        kick_damage_ = mechs_[std::size_t(mech_of(s))][mech::kTonnage] / 5;
        location = rules_.combat.hit_locations[0][(random_.next() & 8) ? 9 : 1] + mech::kArmour;
    }
    const auto& wr = weapon_record(w);
    // Bug 5: a personal weapon at a 'Mech does nothing at all, and the hit
    // flag the graphics get is the round's last shot's.
    if (wr[weapon::kShots] & 0x80) {
        if (fixes_.fix_stale_hit_flag) {
            hit_ = false;
        } else if (!hit_known_) {
            note("a personal weapon at a 'Mech as the round's first shot: the hit flag taken as a miss");
            hit_ = false;
        }
        hit_known_ = true;
        return;
    }
    int damage = w == weapon::kKick ? kick_damage_ : wr[weapon::kDamage];
    if (wr[weapon::kShots] > 1) {
        const int c = two_d6(random_);
        const std::size_t at = std::size_t((c - 2) * 7 + wr[weapon::kShots] - 2);
        const auto* cluster = rules_.combat.clusters.front().data();
        damage = at < 77 ? cluster[at] * damage : 0;
        if (at >= 77) note("a cluster column past the table");
    }
    hit_ = two_d6(random_) >= to_hit;
    hit_known_ = true;
    if (!hit_) {
        print({"shot.mech_missed"});
        damage = 0;
        if (s_.terrain[std::size_t(t)] != 0 && ((w > 0x0f && w < 0x13) || w == weapon::kInferno) &&
            s_.tile[std::size_t(t)] > 0x0f && s_.tile[std::size_t(t)] < 0x40)
            fire_check(t, 0, -1);
        if (s_.standing_on[std::size_t(t)] >= blocking_) {
            std::uint16_t y = std::uint16_t(figs_.y[std::size_t(t)] - s_.sink[std::size_t(t)] - 1);
            if (y & 0x80) y &= 0xf07f;
            marks_.add(0x7f, figs_.x[std::size_t(t)], y);
        }
    } else {
        print({"shot.mech_hit"});
        print({"location." + std::to_string(location - mech::kArmour), "shot.location_end"});  // 1e31:1b8f
    }
    while (damage != 0 && r[0] != 0xff) {
        if (w == weapon::kInferno) {
            // It burns instead, and goes on through the rest with no damage.
            s_.burning[std::size_t(m)] = 3;
            damage = 0;
        }
        const int v = r[std::size_t(location)];
        const bool internal = location > 0x1b && location < 0x24;
        if (v < damage) {
            damage -= v;
            r[std::size_t(location)] = 0;
            if (v != 0 && (location == mech::kLeftArm || location == mech::kRightArm)) arm_off_ = true;
            if (internal) critical(m, location);
            location = transfer(location);
            if (destroyed_) {
                destroy_mech(t);
                destroyed = true;
                critical_hit_ = false;
                // Bug 33: only the exact case below stops everyone.
                if (fixes_.fix_stop_on_loss && t == 0 && mission) stop_everyone();
            }
        } else {
            if (damage == v && (location == mech::kLeftArm || location == mech::kRightArm)) arm_off_ = true;
            r[std::size_t(location)] = std::uint8_t(v - damage);
            damage = 0;
            if (internal) critical(m, location);
            if ((location == mech::kHead || location == mech::kCentreTorso) && r[std::size_t(location)] == 0) {
                destroy_mech(t);
                destroyed = true;
                critical_hit_ = false;
                if (t == 0 && mission) stop_everyone();
            }
        }
    }
}

void Combat::damage_person(int t, int w, int to_hit, bool& killed) {
    const int c = character_of(t);
    auto& r = characters_[std::size_t(c)];
    to_hit += s_.terrain[std::size_t(t)] >> 1;
    const auto& wr = weapon_record(w);
    int damage = 0x7f;
    int shots = wr[weapon::kShots];
    if (shots & 0x80) {
        damage = wr[weapon::kDamage] & 0x0f;
        for (int n = wr[weapon::kDamage] >> 4; n > 0; --n) damage += d6(random_);
    }
    shots &= 0x7f;
    if (damage == 0) damage = 1;
    const int first = damage;
    hit_ = person_hit_ = two_d6(random_) >= to_hit;
    hit_known_ = true;
    if (!hit_) {
        shots = 0;
        if (s_.terrain[std::size_t(t)] != 0 && ((w > 0x0f && w < 0x13) || w == weapon::kInferno) &&
            s_.tile[std::size_t(t)] > 0x0f && s_.tile[std::size_t(t)] < 0x40)
            fire_check(t, -1, 0);
        print({"shot.person_missed"});
    } else {
        print({"shot.person_hit"});
    }
    for (; shots != 0; --shots) {
        if (fixes_.fix_person_damage) damage = first;  // bug 4
        const int armour = std::int8_t(r[character::kArmour]);
        if (armour != 0) {
            const int half = damage >> 1;
            if (armour < half) {
                damage -= armour;
                r[character::kArmour] = 0;
            } else {
                r[character::kArmour] = std::uint8_t(armour - half);
                damage = half;
            }
        }
        const int hp = std::int8_t(r[character::kHitPoints]);
        r[character::kHitPoints] = damage < hp ? std::uint8_t(hp - damage) : 0;
    }
    if (r[character::kHitPoints] == 0) killed = true;
}

int Combat::view_figure() const {
    const int m = std::int8_t(characters_[0][character::kMech]);
    return m < 8 ? m : 4;
}

void Combat::show(int retraces) const {
    if (hooks_.show) hooks_.show(retraces);
}

void Combat::sound(int n) const {
    if (hooks_.sound) hooks_.sound(n);
}

void Combat::redraw(bool after_movement) {
    const int v = view_figure();
    walk({figs_.x[std::size_t(v)], figs_.y[std::size_t(v)]});  // 1000:17bb
    if (graphics_) {
        if (after_movement) terrain_.save_cells(party_);  // 287f:1314, 1ece
        else terrain_.restore_cells();
    }
    if (hooks_.draw_view) hooks_.draw_view(graphics_);
}

void Combat::present(int s, int t, int w, int dir) {
    const auto su = std::size_t(s), tu = std::size_t(t);
    const MapPosition centre = {figs_.x[std::size_t(view_figure())], figs_.y[std::size_t(view_figure())]};
    const auto park = [this](int f) {
        if (animation_.direction(f) != -1) s_.parked_facing[std::size_t(f)] = std::int8_t(animation_.direction(f));
    };
    const auto pose = [this](int table, int d) {  // far pointer d of a table (offset, segment)
        return tables_.mech_poses[std::size_t((table - 0x2d58) / 2 + 2 * d)];
    };
    // The shooter's pose.
    park(s);
    const int kept = animation_.frame(s);
    const bool other = figs_.sprite_base[su] != 0;  // F:d55e
    if (is_mech(s)) {
        if (w == weapon::kKick) animation_.start_script(s, pose(other ? 0x2db8 : 0x2d98, dir));
        else animation_.start_script(s, pose(other ? 0x2d78 : 0x2d58, dir));
    } else if (w >= 0 && w < 0x0f) {
        animation_.start_script(s, tables_.person_poses[std::size_t(w * 16 + dir * 2)]);
    } else {
        note("a person's pose for a weapon past DG:3ff8's table: none");
    }
    // The target's reaction, a person hit by anything but missiles.
    bool reaction = person_hit_;
    int picture = 0;  // [bp-0x24]
    if (w == 0x0a || w == 0x0b) {
        reaction = false;
        if (is_mech(t)) picture = 1;
    }
    if (w >= 0x19 && w <= 0x1f) reaction = false;
    if (reaction) {
        park(t);
        animation_.start_script(t, formats::Tables::kReactionScript);
    }
    while (!animation_.at_end(s)) {
        animation_.advance(s);  // 1000:1732
        if (reaction) animation_.advance(t);
        if (s_.on_screen[su]) {
            redraw();  // 1e31:1f09
            show(5);   // 273d:06c3, 273d:0006(5)
        }
    }
    // 1000:19bf by weapon.
    if (w < 3) sound(0x0d);
    if (w == 0x17 || w == 9 || (w >= 0x13 && w <= 0x16)) sound(3);
    if (w == 3) sound(5);
    if (w == 7 || w == 8) sound(6);
    if (w >= 4 && w <= 6) sound(0x0b);
    // The shot: kind 1 a missile flies, 2 and 3 a line is drawn.
    int kind = 0;
    if (w == 0x0a || w == 0x0b || (w >= 0x19 && w <= 0x1f)) kind = 1;
    if (w == 0x0c || w == 0x0d) kind = 2;
    if (w >= 0x0f && w <= 0x12) {
        kind = 3;
        if (s < 12) picture = other ? 7 : 4;
        else if (t == 0 && other) picture = 3;
    }
    if (kind == 1) sound(1);
    if (kind != 0 && (s_.on_screen[su] || s_.on_screen[tu])) {
        const auto place = [&](int f) {
            if (s_.on_screen[std::size_t(f)]) return std::pair{s_.screen_x[std::size_t(f)], s_.screen_y[std::size_t(f)]};
            const auto [c, r] = view_place(centre, {figs_.x[std::size_t(f)], figs_.y[std::size_t(f)]});
            return std::pair{c << 3, r << 3};
        };
        auto [sx, sy] = place(s);
        auto [tx, ty] = place(t);
        if (is_mech(t)) ty -= 8;
        if (kind != 1) {
            tx += 3;
            ty += 3;
            const int table = is_mech(s) ? (other ? 16 : 0) : 32;
            sx += tables_.muzzle[std::size_t(table + dir)];
            sy += tables_.muzzle[std::size_t(table + 8 + dir)];
        }
        int missile = tables_.mech_poses[std::size_t(0x40 + 2 * dir)];  // DG:2dd8: figure 0x18's script
        const bool far = std::abs(sx - tx) > 8 || std::abs(sy - ty) > 8;
        if (far && kind != 1) {
            // The line, plotted a point at a time along it (273d:031c),
            // where it is inside the map's side of the screen.
            if (hooks_.line) {
                ShownLine line{sx, sy, tx, ty, kind == 2 ? ShownLine::Kind::HandLaser : ShownLine::Kind::MechBeam,
                               s < 12 ? ShownLine::Shooter::Party
                                      : s < 16 ? ShownLine::Shooter::EnemyMech : ShownLine::Shooter::EnemyPerson,
                               w};
                hooks_.line(line);
            }
            if (hooks_.hold) hooks_.hold(0);  // the line, as plotted
        } else if (far) {  // the missile, flown a step at a time
            const int dy = sy - ty, dx = tx - sx;
            int major = std::abs(dy), minor = std::abs(dx);
            if (minor > major) std::swap(major, minor);
            const int o = octant(dy, dx);
            const int scale = 4;  // the missile moves four points a step
            const int step_x = tables_.line_steps[std::size_t(o)] * scale;
            const int step_y = tables_.line_steps[std::size_t(8 + o)] * scale;
            const int diag_x = tables_.line_steps[std::size_t(16 + o)] * scale;
            const int diag_y = tables_.line_steps[std::size_t(24 + o)] * scale;
            int error = major >> 1;
            for (int guard = 0; sx != tx || sy != ty; ++guard) {
                if (guard > 0x4000) {
                    // The original would go on for ever, a step past its end.
                    note("a shot's line misses its end: stopped");
                    break;
                }
                const bool inside = sx >= 0x68 && sx < 0x140 && sy >= 0 && sy < 200;
                const int frame = animation_.run(missile) + 0x68;
                if (inside) {
                    // (On the Tandy only every fourth step is drawn.)
                    redraw();
                    if (hooks_.sprite) hooks_.sprite(frame, sx, sy);  // 1e31:1f73
                    show(0);
                }
                if (sx != tx) sx += step_x;
                if (sy != ty) sy += step_y;
                error -= minor;
                if (error < 0) {
                    if (sx != tx) sx += diag_x;
                    if (sy != ty) sy += diag_y;
                    error += major;
                }
            }
        }
        if (kind != 1) sound(kind == 2 ? 9 : 2);
    }
    if (!hit_) {
        redraw();  // not shown: the line stays on screen
    } else {
        sound(4);
        if (w > 6 && s_.on_screen[tu]) {
            int explosion = formats::Tables::kExplosionScript;  // figure 0x18's script
            const int ex = s_.screen_x[tu], ey = s_.screen_y[tu] - (is_mech(t) ? 8 : 0);
            while (animation_.script_byte(explosion) != 0xff) {
                const int frame = animation_.run(explosion) + 0xfa;
                redraw();
                if (hooks_.sprite) hooks_.sprite(frame, ex, ey);
                show(0);
            }
            if (hooks_.hold) hooks_.hold(5);
            if (picture != 0 && hooks_.picture) hooks_.picture(picture, 0);
        }
    }
    if (is_mech(s)) animation_.set_frame(s, kept);
}

// BTECH.EXE 22e8:1c82..1e03, part of 22e8:12c7 (Ghidra FUN_22e8_12c7)
// The Arena's spectator (figure 13) destroyed: two message boxes with the
// view on figure 0 between them, two URBANMECHs (template 7) as figures 14
// and 15 at the Arena's west side, the spectator gone (F:e48e = 0), and
// flag 23 set: the exit zone opens (docs/combat.md 3.14).
void Combat::spectator_down() {
    if (hooks_.message_box) hooks_.message_box();  // 24d3:17c6
    sound(7);                                      // 1000:19bf
    print({"shot.arena_damaged"});                 // 24d3:17ea
    if (hooks_.key) hooks_.key();                  // 273d:0259
    walk({figs_.x[0], figs_.y[0]});                // 1000:17bb
    terrain_.save_cells(party_);                   // 287f:1314, 1ece
    if (hooks_.draw_view) hooks_.draw_view(true);  // 1000:240b, 287f:18ef, 1000:0e4b
    show(0);                                       // 273d:06c3
    if (hooks_.message_box) hooks_.message_box();
    print({"shot.urbanmechs"});
    if (hooks_.key) hooks_.key();
    const auto& urbanmech = rules_.mechs[7].record;  // 27e8:065b
    mechs_[6] = urbanmech;
    mechs_[7] = urbanmech;
    for (const int f : {14, 15}) {
        const int heading = f == 14 ? 4 : 0;
        animation_.set_frame(f, 0);
        figs_.sprite_base[std::size_t(f)] = 0;
        figs_.heading[std::size_t(f)] = std::int8_t(heading);
        animation_.set_direction(f, heading);
        animation_.start_script(f, FigureAnimation::mech_script(6));  // 27e8:02a0
        figs_.active[std::size_t(f)] = 1;
        figs_.x[std::size_t(f)] = 0x097d;
    }
    s_.spectator = false;  // F:e48e
    flags_[0x23] = 1;
    figs_.y[14] = 0x8030;
    figs_.y[15] = 0x8070;
    if (hooks_.select_box) hooks_.select_box();  // 2656:0281(4)
}

void Combat::after_shot(int t, bool killed, bool destroyed, int c) {
    if (killed) {
        print({"shot.killed"});
        figs_.destroyed[std::size_t(t)] = true;
        figs_.active[std::size_t(t)] = 0;
        characters_[std::size_t(c)][character::kName] = character::kEmpty;
        animation_.set_frame(t, 0x7e);
        figs_.sprite_base[std::size_t(t)] = 0;
        marks_.add(0x7e, figs_.x[std::size_t(t)], figs_.y[std::size_t(t)]);
        for (auto& row : s_.targets)
            for (auto& b : row)
                if ((b & 0x7f) == t) b = 0xff;
        if (c < 2) s_.fight_on = false;
        figs_.x[std::size_t(t)] = figs_.y[std::size_t(t)] = 0xffff;
    }
    if (destroyed) {
        if (!(s_.spectator && t == 13)) {
            std::uint16_t x = std::uint16_t(figs_.x[std::size_t(t)] - 1), y = std::uint16_t(figs_.y[std::size_t(t)] - 1);
            if (x & 0x80) x &= 0x0f7f;
            if (y & 0x80) y &= 0xf07f;
            marks_.add(figs_.sprite_base[std::size_t(t)] != 0 ? 0x81 : 0x80, x, y);
            sound(8);  // 1000:19bf
        } else {
            spectator_down();
        }
        figs_.x[std::size_t(t)] = figs_.y[std::size_t(t)] = 0xffff;
        figs_.active[std::size_t(t)] = 0;
    }
}

bool Combat::fire(int s, int slot, bool mission, const std::array<std::int8_t, 24>& moved) {
    person_hit_ = false;
    destroyed_ = false;
    critical_hit_ = false;
    int t = std::int8_t(s_.targets[std::size_t(s)][std::size_t(slot)]);
    if (s < 4 && s_.heat[std::size_t(s)] > 29) t = -1;
    if (s > 11 && s < 16 && s_.heat[std::size_t(s - 8)] > 29) t = -1;
    if (t & 0x80) return false;
    const MapPosition saved = party_;
    party_ = {figs_.x[std::size_t(s)], figs_.y[std::size_t(s)]};
    int w = 0;
    if (is_mech(s)) {
        if (slot == 11) {
            const auto& r = mechs_[std::size_t(mech_of(s))];
            if (!(r[mech::kLeftActuators] & 8) || !(r[mech::kRightActuators] & 8)) {
                w = 0x80;
                s_.targets[std::size_t(s)][11] = 0xff;
            } else {
                w = weapon::kKick;
            }
        } else {
            w = slot_weapon(mech_of(s), slot);
            if (w == 0xff) s_.targets[std::size_t(s)][std::size_t(slot)] = 0xff;
        }
    } else {
        w = std::int8_t(characters_[std::size_t(character_of(s))][character::kWeapon]);
    }
    if (w & 0x80) {
        party_ = saved;
        return false;
    }
    if (figs_.active[std::size_t(t)] == 0) {
        s_.targets[std::size_t(s)][std::size_t(slot)] = 0xff;
        party_ = saved;
        return false;
    }
    const int band = range_band(t, w);
    if (band >= 3) {
        party_ = saved;
        return false;
    }
    int dir = direction_toward(party_, {figs_.x[std::size_t(t)], figs_.y[std::size_t(t)]}, tables_);
    if (dir == -1) dir = 0;
    // Back to where the round stood, then walked to the shooter, so the
    // grids step at an area edge.
    party_ = saved;
    walk({figs_.x[std::size_t(s)], figs_.y[std::size_t(s)]});  // 1000:17bb
    const bool sight = line_of_sight(s, t, figs_.x[std::size_t(t)], figs_.y[std::size_t(t)]);
    if (sight) {
        const auto& wr = weapon_record(w);
        if (s > 3 && s < 12) {
            // Practice: 256 shots with one skill raise it by one, up to 4
            // (F:d360 + s the skill last used, F:d358 + s the count: flags
            // 0x58-0x5f and 0x50-0x57).
            const int skill = wr[weapon::kSkill];
            auto& last = flags_[std::size_t(0x54 + s)];
            auto& count = flags_[std::size_t(0x4c + s)];
            if (std::int8_t(last) == skill) {
                if (++count == 0) {
                    auto& level = characters_[std::size_t(s - 4)][std::size_t(character::kSkills + skill)];
                    if (std::int8_t(level) < 4) ++level;
                }
            } else {
                last = std::uint8_t(skill);
                count = 0;
            }
        }
        if (is_mech(s) && slot < 11) {
            auto& ammo = mechs_[std::size_t(mech_of(s))][std::size_t(mech::kAmmunition + slot)];
            if (ammo != 0xff) --ammo;
        }
        int to_hit = band * 2 + 4;
        if (w == weapon::kKick) {
            // Bug 2: the original counts the missing leg actuators of 'Mech
            // record s, the figure number, which for an enemy kicker is
            // past the eight records.
            to_hit = 3;
            if (s > 3 && !fixes_.fix_kick_actuators) {
                // Record s (F:cd24 + 0x7d (s - 12)) is in the stack, zero
                // below its deepest use: all six count as missing.
                if (s == 15) note("figure 15's kick reads stack the program has used: taken as 0");
                to_hit += 6;
            } else {
                const auto& r = mechs_[std::size_t(mech_of(s))];
                for (const int b : {mech::kLeftActuators, mech::kRightActuators})
                    for (int bit = 4; bit > 0; bit >>= 1)
                        if (!(r[std::size_t(b)] & bit)) ++to_hit;
            }
        }
        to_hit += std::int8_t(s_.orders[std::size_t(s)][0]) + 1;
        to_hit += rules_.combat.target_movement[std::size_t(std::clamp<int>(moved[std::size_t(t)], 0, 12))];
        int shooter = 0;
        if (s > 3 && s < 12) shooter = s - 4;
        if (s > 15) shooter = s - 8;
        if (is_mech(s)) {
            const int m = mech_of(s);
            const auto& r = mechs_[std::size_t(m)];
            shooter = r[mech::kPilot];
            if (r[mech::kSensorHits] != 0) to_hit += 2;
            s_.weapon_heat[std::size_t(m)] = std::int8_t(s_.weapon_heat[std::size_t(m)] + (wr[weapon::kHeat] & 0x0f));
            const int heat = s_.heat[std::size_t(m)];
            if (heat > 7) {
                to_hit += heat > 12 ? 2 : 1;
                if (heat > 16) ++to_hit;
                if (heat > 23) ++to_hit;
            }
        }
        shot_line(s, t, w, shooter);
        if (shooter < 0 || shooter > 15) {
            // A 'Mech with no pilot: the original reads past the records.
            note("a shot by a 'Mech with no pilot: skill 0");
        } else {
            to_hit -= std::int8_t(characters_[std::size_t(shooter)][std::size_t(character::kSkills + wr[weapon::kSkill])]);
        }
        bool destroyed = false, killed = false;
        int c = 0;
        if (is_mech(t)) {
            c = mech_of(t);
            damage_mech(s, t, w, dir, to_hit, mission, destroyed);
        } else {
            c = character_of(t);
            damage_person(t, w, to_hit, killed);
        }
        if (graphics_ && s_.fight_on) present(s, t, w, dir);
        after_shot(t, killed, destroyed, c);
        if (graphics_) animation_.set_direction(s, -1);  // F:396c + s = ff
        if (arm_off_) {
            critical_hit_ = false;
            if (graphics_ && !(t == 13 && s_.spectator) && hooks_.picture) hooks_.picture(0x10, 0);
        }
        if (critical_hit_ && graphics_ && c == std::int8_t(characters_[0][character::kMech]) && hooks_.picture)
            hooks_.picture(5, 0);
        if (hooks_.pause) hooks_.pause();
        s_.targets[std::size_t(s)][std::size_t(slot)] |= 0x80;
        arm_off_ = false;
        walk(saved);  // 1000:17bb
        party_ = saved;
        return true;
    }
    // Blocked: the same walk back (22e8:0d9f).
    walk(saved);  // 1000:17bb
    party_ = saved;
    return false;
}

void Combat::round(bool mission) {
    const MapPosition saved = party_;
    std::array<std::int8_t, 24> moved{};  // steps taken this round
    s_.path_step.fill(0);
    hit_known_ = false;
    for (int phase = 0; phase < 12; ++phase) {
        if (!s_.fight_on) continue;
        bool any = phase == 0;
        // Movement: one step of each active figure's path.
        for (int f = 0; f < 24; ++f) {
            if (figs_.active[std::size_t(f)] == 0) continue;
            const int at = s_.path_step[std::size_t(f)];
            const int dx = s_.paths[std::size_t(f)][std::size_t(2 * at)];
            const int dy = s_.paths[std::size_t(f)][std::size_t(2 * at + 1)];
            if (dx == 2 || dy == 2 || (dx == 0 && dy == 0)) continue;
            const MapPosition before = party_;
            party_ = {figs_.x[std::size_t(f)], figs_.y[std::size_t(f)]};  // 1000:186f
            if (dx != 0) party_.x = step_x(party_.x, dx);                 // 1000:191b
            if (dy != 0) party_.y = step_y(party_.y, dy);
            if (!place_taken(f, true)) {
                any = true;
                ++s_.path_step[std::size_t(f)];
                figs_.x[std::size_t(f)] = party_.x;
                figs_.y[std::size_t(f)] = party_.y;
                const int dir = tables_.step_direction[std::size_t((dy + 1) * 4 + dx + 1)];
                ++moved[std::size_t(f)];
                if (animation_.direction(f) != dir) {
                    // DG:025a: the walking script for the new direction.
                    const bool person = (f > 3 && f < 12) || f > 15;
                    animation_.start_script(f, FigureAnimation::mech_script(dir + (person ? 8 : 0)));
                }
                animation_.advance(f);  // 1000:1732
                figs_.heading[std::size_t(f)] = std::int8_t(animation_.direction(f));
            }
            party_ = before;
        }
        if (any && (graphics_ || fixes_.fix_terrain_without_graphics)) {
            redraw(true);
            if (graphics_) show(0);  // 273d:06c3
        }
        // Firing: each figure's first slot that fires ends its turn.
        for (int f = 0; f < 24; ++f) {
            if (!s_.fight_on || figs_.active[std::size_t(f)] == 0) continue;
            for (int slot = 0; slot < 12; ++slot)
                if (fire(f, slot, mission, moved)) break;
        }
        // Waypoints: a party figure on its first order's place moves on to
        // the next order.
        for (int f = 0; f < 12; ++f) {
            auto& o = s_.orders[std::size_t(f)];
            if (o[0] == 0xff) continue;
            const std::uint16_t x = figs_.x[std::size_t(f)], y = figs_.y[std::size_t(f)];
            const std::uint8_t area = std::uint8_t((x >> 8) | (y >> 8));
            if (o[1] == area && std::int8_t(o[2]) == (x & 0x7f) && std::int8_t(o[3]) == (y & 0x7f)) {
                for (std::size_t i = 0; i < 0x2c; ++i) o[i] = o[i + 4];
                o[0x2c] = 0xff;
            }
        }
    }
    if (s_.fight_on) walk(saved);  // 1000:17bb
    if (s_.fight_on) end_round(moved);
}

void Combat::end_round(const std::array<std::int8_t, 24>& moved) {
    for (auto& row : s_.targets)
        for (auto& b : row) {
            if (b == 0xff) continue;
            b &= 0x7f;
            if (figs_.active[std::size_t(b)] == 0) b = 0xff;
        }
    // Heat, for each 'Mech record that holds one.
    for (int a = 0; a < 4; ++a)
        for (const int side : {0, 4}) {
            const int m = a + side;
            auto& r = mechs_[std::size_t(m)];
            if (r[0] != 0xff) {
                // Movement: the order type + 1; a jump counts its steps.
                int h = std::int8_t(s_.orders[std::size_t(a)][0]) + 1;
                if (h == 3 && moved[std::size_t(a)] > 3) h = moved[std::size_t(a)];
                if (side != 0) {
                    h = std::int8_t(s_.orders[std::size_t(a + 12)][0]) + 1;
                    if (h == 3 && moved[std::size_t(a + 12)] > 3) h = moved[std::size_t(a + 12)];
                }
                h += 5 * r[mech::kEngineHits];
                h -= r[mech::kEngineSinks];
                for (int i = mech::kItems; i < mech::kItemsEnd; ++i)
                    if (r[std::size_t(i)] == mech::kHeatSinkItem) --h;
                h += s_.weapon_heat[std::size_t(m)];
                auto& heat = s_.heat[std::size_t(m)];
                heat = std::int8_t(heat + h);
                if (s_.burning[std::size_t(m)] != 0) {
                    heat = std::int8_t(heat + 6);
                    --s_.burning[std::size_t(m)];
                }
                // Standing in water cools it.
                const int f = a + (side != 0 ? 12 : 0);
                if (s_.terrain[std::size_t(f)] != 0 && s_.tile[std::size_t(f)] < 0x10) heat = std::int8_t(heat - 4);
                if (heat < 0) heat = 0;
                // Bug 35: the cap at 30 tests party record a, so an enemy
                // 'Mech's heat is never capped.
                const int capped = fixes_.fix_enemy_heat_cap ? m : a;
                if (s_.heat[std::size_t(capped)] > 30) s_.heat[std::size_t(capped)] = 30;
            }
            s_.weapon_heat[std::size_t(m)] = 0;
        }
    // The dead: their figures go.
    for (int c = 0; c < 16; ++c) {
        if (characters_[std::size_t(c)][character::kName] != character::kEmpty) continue;
        const int f = c < 8 ? c + 4 : c + 8;
        if (c != 0) figs_.x[std::size_t(f)] = figs_.y[std::size_t(f)] = 0xffff;
        if (f > 15) figs_.sprite_base[std::size_t(f)] = 0xfe;
        if (c == 0) figs_.sprite_base[std::size_t(f)] = 0x96;
    }
}

}  // namespace chr::game
