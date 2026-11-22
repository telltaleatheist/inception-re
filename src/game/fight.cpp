// A training mission (docs/combat.md 1.2 and 2): special 1 walks the chosen
// 'Mech in, the mission driver places the enemy, and the fight asks its
// three questions and runs its rounds, the party's side ordered by the
// player or fought by the computer.
#include <algorithm>
#include <map>

#include "game/fight.h"
#include "game/map_view.h"
#include "game/text_provider.h"
#include "game/tile_animation.h"
#include "game/town.h"
#include "game/world.h"

namespace chr::game {

namespace {

// 'Mech record bytes (docs/formats/mech.md).
constexpr int kArmourFirst = 0x11, kArmourLast = 0x1b;
constexpr int kSlots = 0x34;
constexpr int kPilot = 0x79;

std::int16_t wrap16(int v) { return std::int16_t(std::uint16_t(v)); }

// Thrown by Combat's key hook when the keys run out inside a round.
struct KeysRanOut {};

}  // namespace

int mech_sink(int tile, int quarters) {
    if (tile < 0 || tile >= 0xf6 || (tile & quarters) == 0 || (tile & 0xf0) >= 0x30) return 0;
    if (((tile & 0xf0) == 0x00 || (tile & 0xf0) == 0x20) && (tile & 0x0f) == 0x0f) return 16;
    return 8;
}

bool Town::on_foot(int character) const {
    const auto& c = characters_[std::size_t(character)];
    return c[character::kName] != character::kEmpty && std::int8_t(c[character::kMech]) >= character::kOnFoot;
}

// BTECH.EXE 1000:051b (Ghidra FUN_1000_051b), as drawn during special 1's
// walk-in: the party's characters on foot, the townspeople, then each
// 'Mech of records 0-3 at the k-th 'Mech place around the party (k counts
// the 'Mechs drawn), 24x24 with its foot cell two half-cells below its
// top. Afterwards the 'Mechs' places are recorded as figures 0-3.
std::vector<Figure> Town::walk_in_figures() {
    Party walking = party();
    for (int i = 0; i < 8; ++i) walking.present[std::size_t(i)] = walking.present[std::size_t(i)] && on_foot(i);
    draw_marks();
    auto all = drawn_marks_;
    const auto party = party_figures(terrain_, position_, walking, animation_, tables_, people_sprite_base(),
                                     flags_[flag::kTileEvents] == 0);
    all.insert(all.end(), party.begin(), party.end());
    const auto people = people_.figures(map_, position_, animation_);
    all.insert(all.end(), people.begin(), people.end());
    int k = 0;
    for (int m = 0; m < 4; ++m) {
        if (mechs_[std::size_t(m)][0] == 0xff) continue;
        const Figure f = party_mech_figure(m, k);
        // F:32ae + m: the rows it sinks.
        combat_.terrain[std::size_t(m)] = std::int8_t(f.sink);
        all.push_back(f);
        figs_.x[std::size_t(k)] = move_x(position_.x, tables_.mech_formation_x[std::size_t(k)]);
        figs_.y[std::size_t(k)] = move_y(position_.y, tables_.mech_formation_y[std::size_t(k)]);
        figs_.active[std::size_t(k)] = 1;
        ++k;
    }
    // 1000:0d30: with F:398e, the bays at (0x0d13 + 4i, 0x702c), each only
    // when all of it is in the view (x 0d-27, y 00-18 in half-cells, the
    // party at 1a, 0c, tested in every area, then the wide ranges):
    // sprite 0x92 (F:39fa + 4 x 0x92 = F:3c42), a 'Mech standing.
    if (bays_drawn_)
        for (int i = 0; i < 4; ++i) {
            const std::uint16_t bx = std::uint16_t(0x0d13 + 4 * i), by = 0x702c;
            const int dx = wrap16(bx - position_.x + 0x1a), dy = wrap16(by - position_.y + 0x0c);
            if (dx < 0x0d || dx > 0x27 || dy < 0 || dy > 0x18) continue;
            if (dx < -0x73 || dx > 0xa7 || dy < wrap16(0xf080) || dy > 0x0f98) continue;
            Figure f;
            f.sprite = sprite(0x92);
            f.half_x = folded_half_x(dx, position_.x) + 1;
            f.half_y = folded_half_y(dy, position_.y);
            all.push_back(f);
        }
    return all;
}

// BTECH.EXE 1000:051b, the 'Mechs' part (0x9fe..0xbc6): 'Mech record m
// drawn at the k-th 'Mech place around the party (k counts the 'Mechs
// drawn), 24x24 with its foot cell two half-cells below its top, sunk by
// the water at its foot cell (none on a map with tile events, F:d346).
Figure Town::party_mech_figure(int m, int k) const {
    const bool odd_x = position_.x & 1, odd_y = position_.y & 1;
    Figure f;
    f.sprite = sprite(figs_.sprite_base[std::size_t(m)] + animation_.frame(m));
    f.half_x = position_.half_cell_x() + tables_.mech_formation_x[std::size_t(k)];
    f.half_y = position_.half_cell_y() + tables_.mech_formation_y[std::size_t(k)] - 2;
    const int index = view_corner_index(position_) + tables_.mech_cell[std::size_t(k)] +
                      (odd_x ? tables_.mech_cell_odd_x[std::size_t(k)] : 0) + (odd_y ? tables_.mech_cell_odd_y[std::size_t(k)] : 0);
    const int quarters = tables_.mech_water[std::size_t(k)] ^ (odd_y ? 5 : 0);
    f.sink = flags_[flag::kTileEvents] != 0 ? 0 : mech_sink(window_cell(terrain_, position_, index).value_or(-1), quarters);
    return f;
}

// BTECH.EXE 1000:0e4b (Ghidra FUN_1000_0e4b)
// The fight's figure drawer: the map's marks (1000:2a93, which turns a
// fire to its other frame), the people (figures 4-11, 16-23) in slot
// order, then the 'Mechs (0-3, 12-15) in view top to bottom, each from its
// own place relative to the view. On the way it writes what the fight
// later reads about each figure drawn (docs/combat.md 3.14, quirk 36): on
// screen, its screen place, the tile under it and how deep it stands.
std::vector<Figure> Town::fight_figures_drawn(bool marks) {
    auto& c = combat_;
    const std::uint16_t px = position_.x, py = position_.y;
    const int corner = view_corner_index(position_);
    const auto cell = [&](int index) { return window_cell(terrain_, position_, index).value_or(-1); };
    const bool level = flags_[0x3a] == 0;  // F:d346
    c.on_screen.fill(false);
    std::vector<Figure> out;
    if (marks) {
        draw_marks();
        out = drawn_marks_;
    }
    for (const int base : {4, 16}) {
        for (int f = base; f < base + 8; ++f) {
            const auto fx = figs_.x[std::size_t(f)], fy = figs_.y[std::size_t(f)];
            if (fx == 0xffff) continue;
            const int col = wrap16(fx - px + 0x1a), row = wrap16(fy - py + 0x0c);
            if ((fx & 0xff00) == (px & 0xff00) && (col < 0x0d || col > 0x27)) continue;
            if ((fy & 0xff00) == (py & 0xff00) && (row < 0 || row > 0x18)) continue;
            if (col < -0x73 || col > 0xa7 || row < wrap16(0xf080) || row > 0x0f98) continue;
            c.on_screen[std::size_t(f)] = true;
            const int cm = (col & 0x7f) - 0x0d, rm = row & 0x7f;
            int index = (rm >> 1) * kWindowCells + (cm >> 1) + corner;
            if ((cm & 1) && (px & 1)) index += 1;
            if ((rm & 1) && (py & 1)) index += kWindowCells;
            const int tile = cell(index);
            const int quarters = tables_.person_quarters[std::size_t(((rm ^ py) & 1) * 2 + ((cm ^ px) & 1))];
            int sink = 0;
            if (level && tile >= 0 && tile < 0xf6 && (tile & quarters) && (tile & 0xf0) < 0x30)
                sink = (tile & 0xf0) == 0x20 ? 4 : 2;
            c.tile[std::size_t(f)] = std::uint8_t(tile);
            c.terrain[std::size_t(f)] = std::int8_t(sink);
            c.screen_x[std::size_t(f)] = (col & 0x7f) << 3;
            c.screen_y[std::size_t(f)] = rm << 3;
            Figure fig;
            // A 16-bit sum: 0xfe + a frame reaches row 40's grey people.
            fig.sprite = sprite(figs_.sprite_base[std::size_t(f)] + animation_.frame(f));
            fig.half_x = folded_half_x(col, px) + 1;
            fig.half_y = folded_half_y(row, py);
            fig.sink = sink;
            out.push_back(fig);
        }
    }
    struct Drawn {
        Figure figure;
        int pixel_y;
    };
    std::vector<Drawn> mechs;
    for (const int f : {0, 1, 2, 3, 12, 13, 14, 15}) {
        const auto fx = figs_.x[std::size_t(f)], fy = figs_.y[std::size_t(f)];
        if (fx == 0xffff) continue;
        const int col = wrap16(fx - px + 0x1a), row = wrap16(fy - py + 0x0c);
        const bool off_x = (fx & 0xff00) == (px & 0xff00) && (col < 0x0b || col > 0x27);
        const bool off_y = (fy & 0xff00) == (py & 0xff00) && (row < 0 || row > 0x1a);
        c.sink[std::size_t(f)] = 0;
        c.standing_on[std::size_t(f)] = 0;
        if (col < -0x75 || col > 0xa7 || row < wrap16(0xf080) || row > 0x0f9a || off_x || off_y) continue;
        c.on_screen[std::size_t(f)] = true;
        const int cm = col & 0x7f, rm = row & 0x7f;
        // DG:0364/039e give the column's cell: (col - 13) / 2, and one more
        // for an odd half-cell when the view's x is odd (columns 11-12: 0).
        int index = (rm >> 1) * kWindowCells;
        if ((row & 1) && (py & 1)) index += kWindowCells;
        int quarters = 1;
        if ((rm ^ py) & 1) {
            quarters = 4;
            c.sink[std::size_t(f)] = 1;
        }
        if (cm >= 13) index += (cm - 13) >> 1;
        if (cm >= 13 && ((cm - 13) & 1) && (px & 1)) index += 1;
        c.standing_on[std::size_t(f)] = std::uint8_t(cell(corner + index - kWindowCells));
        const int tile = cell(corner + index);
        c.tile[std::size_t(f)] = std::uint8_t(tile);
        const int sink = level ? mech_sink(tile, quarters) : 0;
        c.terrain[std::size_t(f)] = std::int8_t(sink);
        c.screen_x[std::size_t(f)] = cm << 3;
        c.screen_y[std::size_t(f)] = rm << 3;
        Figure fig;
        fig.sprite = sprite((figs_.sprite_base[std::size_t(f)] + animation_.frame(f)) & 0xff);
        fig.half_x = folded_half_x(col, px);
        fig.half_y = folded_half_y(row, py) - 2;
        fig.sink = sink;
        mechs.push_back({fig, 8 * rm - 16});
    }
    // Sorted as the original does: for each i, every later one higher on
    // the screen is swapped in.
    for (std::size_t i = 0; i < mechs.size(); ++i)
        for (std::size_t j = i + 1; j < mechs.size(); ++j)
            if (mechs[j].pixel_y < mechs[i].pixel_y) std::swap(mechs[i], mechs[j]);
    for (const auto& m : mechs) out.push_back(m.figure);
    return out;
}

// BTECH.EXE 24d3:0027..0184 (special 1 of 24d3:0004)
// The training center hands Jason the 'Mech chosen in flag 02 and shows it
// walking in from the west, then runs the mission in flag 00.
bool Town::walk_in(KeySource& keys) {
    if (!rules_ || rules_->mechs.size() < 5) {
        log_.push_back("special 1: no rule tables");
        fight_unfinished_ = true;
        return false;
    }
    // Flag 02: 0 the Locust, 1 the Wasp, 2 the Chameleon (the template at
    // 27e8:04e4); the two last have their own sprites from slot 0x92.
    const int choice = flags_[flag::kTrainingMech];
    const int t = choice == 0 ? 0 : choice == 1 ? 1 : 4;
    figs_.sprite_base[0] = choice == 0 ? 0 : 0x92;
    characters_[0][character::kMech] = 0;
    mechs_[0] = rules_->mechs[std::size_t(t)].record;
    mechs_[0][kPilot] = 0;
    show_picture(0, 0);  // 1000:48b7(0, 0); window 4 stays selected

    // Four half-cell steps east, six frames each.
    position_ = {0x0c3c, 0xc04f};
    animation_.start_script(0, FigureAnimation::mech_script(2));
    std::vector<Figure> last;
    while (position_.x < 0x0c40) {
        animation_.advance(0);  // 1000:1732
        for (int frame = 0; frame < 6; ++frame) {
            if (tiles_animate(map_number_)) phase_ = (phase_ + 1) % 3;  // 1000:240b
            last = walk_in_figures();
            // 273d:06c3 copies the map's columns (13-39) to the screen:
            // whatever text was there is gone.
            text_.forget(13, 0, kTextColumns - 13, kTextRows);
        }
        frozen_ = Frozen{live_view(), last};
        ++position_.x;
    }
    animation_.advance(0);  // a fifth time: frame 6, not drawn here
    return run_mission(flags_[0], keys);
}

// BTECH.EXE 17dc:0d49 (Ghidra FUN_17dc_0d49), for a training mission, the
// jail break (bit 0x80 in `people`) or the Arena (bit 0x80 in `mechs`):
// `mechs` enemy 'Mechs in records 4 on, as figures 12 on, and `people`
// enemy people in character records 8 on, as figures 16 on.
void Town::place_mission(int mechs, int people) {
    const bool on_foot = (people & 0x80) != 0;
    const bool arena = (mechs & 0x80) != 0;
    people &= 0x7f;
    figs_.computer[0] = false;
    // The townspeople keep their places in their records; every figure is
    // emptied.
    people_.leave_for_fight();
    townspeople_away_ = true;
    for (int m = 4; m < 8; ++m) mechs_[std::size_t(m)][0] = 0xff;
    for (int c = 8; c < 16; ++c) characters_[std::size_t(c)][character::kName] = character::kEmpty;
    for (int f = 0; f < FightFigures::kCount; ++f) {
        figs_.destroyed[std::size_t(f)] = false;
        figs_.active[std::size_t(f)] = 0;
        figs_.x[std::size_t(f)] = figs_.y[std::size_t(f)] = 0xffff;
    }
    // Jason's 'Mech, or Jason on foot.
    figs_.active[on_foot ? 4 : 0] = 1;
    std::uint16_t x0 = 0, y0 = 0;
    int tmpl_index = 0;  // LOCUST (27e8:02f0)
    int sprite_set = 0;
    if (arena) {
        // The Arena (17dc:0e4e): one of two spots at the east end, three or
        // four draws; with the spectator (F:e48e) a second 'Mech when a held
        // letter is ff. The 2d6 is drawn either way; with the spectator one
        // more draw replaces it, (r & 3) + 3: LOCUST, WASP, LOCUST or STINGER.
        x0 = std::uint16_t(0x0a10 + (random_.next() & 3));
        if (random_.next() & 1) x0 = std::uint16_t(0x0a28 + (random_.next() & 3));
        y0 = std::uint16_t(0x806f + (random_.next() & 3));
        if (combat_.spectator)
            for (const auto letter : held_mechs_)
                if (letter == 0xff) mechs = 2;
        int index = two_d6(random_) - 2;
        if (combat_.spectator) index = (random_.next() & 3) + 3;
        const int offset = tables_.encounter_templates[std::size_t(2 * index)];
        tmpl_index = (offset - 0x02f0) / 0x7d;
        sprite_set = tables_.encounter_sprite_sets[std::size_t(index)];
    } else {
        // Where the enemy starts: two draws, the first thrown away on the
        // course of mission 2, which always starts at x 0c72; both thrown
        // away in the jail break.
        x0 = std::uint16_t(0x0c65 + (random_.next() & 7));
        y0 = std::uint16_t(0xc059 + (random_.next() & 7));
        if (on_foot) {
            x0 = 0x0d10;
            y0 = 0x7024;
        }
        if (flags_[0] == 2) x0 = 0x0c72;
    }
    mechs &= 0x7f;
    // In the raid (F:3772): four 'Mechs, JENNERs (27e8:0561, template 5),
    // and flag 03 set to 0.
    if (raid_) mechs = 4;
    const auto& tmpl = rules_->mechs[std::size_t(raid_ ? 5 : tmpl_index)].record;
    // Flag 03 counts failed tries at the mission: it is kept at 6 at most,
    // and takes armour off the enemy (and, after five, more: below).
    if (raid_) flags_[flag::kTries] = 0;
    if (flags_[flag::kTries] > 6) flags_[flag::kTries] = 6;
    const int tries = flags_[flag::kTries];
    const int cut = rules_->combat.difficulty_armour[std::size_t(tries)];
    int placed = 0;
    for (int i = 0; i < mechs && i < 4; ++i, ++placed) {
        auto& pilot = characters_[std::size_t(8 + i)];
        pilot[character::kName] = 0;
        pilot[character::kMech] = std::uint8_t(4 + i);
        auto& rec = mechs_[std::size_t(4 + i)];
        rec = tmpl;
        // The Arena's second 'Mech is the spectator (27e8:05de, template 6).
        if (combat_.spectator && i > 0) rec = rules_->mechs[6].record;
        for (int b = kArmourFirst; b <= kArmourLast; ++b)
            rec[std::size_t(b)] = rec[std::size_t(b)] <= cut ? 0 : std::uint8_t(rec[std::size_t(b)] - cut);
        if (tries > 4)
            for (int n = 0; n < 5; ++n) {
                auto& slot = rec[std::size_t(kSlots + (random_.next() & 0x1f))];
                if (slot != 0) slot |= 0x80;
            }
        if (tries > 5)
            for (const int b : {0x75, 0x77, 0x76}) rec[std::size_t(b)] = std::uint8_t(random_.next() & 1);
        const int f = 12 + i;
        figs_.sprite_base[std::size_t(f)] = std::uint8_t(sprite_set);
        animation_.set_frame(f, 0x0c);
        animation_.start_script(f, FigureAnimation::mech_script(6));
        animation_.set_direction(f, 6);
        figs_.heading[std::size_t(f)] = 6;
        figs_.active[std::size_t(f)] = 1;
        figs_.x[std::size_t(f)] = std::uint16_t(x0 + tables_.enemy_offset_x[std::size_t(placed)]);
        figs_.y[std::size_t(f)] = std::uint16_t(y0 + tables_.enemy_offset_y[std::size_t(placed)]);
        if (combat_.spectator && i > 0) {
            // The spectator: its own place, frame and one-frame script, facing 2.
            animation_.set_frame(f, 0x10);
            figs_.sprite_base[std::size_t(f)] = 0;
            animation_.start_script(f, formats::Tables::kSpectatorScript);  // DG:4dd8
            figs_.heading[std::size_t(f)] = 2;
            animation_.set_direction(f, 2);
            figs_.x[std::size_t(f)] = 0x0a06;
            figs_.y[std::size_t(f)] = 0x8066;
        }
    }
    // People: the characters after the 'Mechs' pilots, at most to 15, as
    // figures 8 on from their numbers.
    const int end = std::min(16, people + 8 + placed);
    for (int j = 8 + placed; j < end; ++j) {
        auto& r = characters_[std::size_t(j)];
        r[character::kName] = 0;
        r[character::kMech] = character::kOnFoot;
        const int f = j + 8;
        figs_.sprite_base[std::size_t(f)] = 0xfe;
        animation_.set_frame(f, 0x1c);
        animation_.start_script(f, 0x2e0);
        figs_.heading[std::size_t(f)] = 6;
        people_.walkers()[std::size_t(f - 16)].heading = 6;  // the same byte, F:3920 + f
        animation_.set_direction(f, 6);
        figs_.active[std::size_t(f)] = 1;
        // The offset tables are read at the character's number: past 11 the
        // x offset is the y table's and the y offset the armour cut's after
        // it (DG:1642, 164e and 165a lie in a row).
        const auto offset = [&](int k) {
            if (k < 12) return tables_.enemy_offset_x[std::size_t(k)];
            if (k < 24) return tables_.enemy_offset_y[std::size_t(k - 12)];
            return int(std::int8_t(rules_->combat.difficulty_armour[std::size_t(k - 24)]));
        };
        figs_.x[std::size_t(f)] = std::uint16_t(x0 + offset(j));
        figs_.y[std::size_t(f)] = std::uint16_t(y0 + offset(j + 12));
        for (int k = 0; k < 6; ++k) r[std::size_t(character::kSkills + k)] = std::uint8_t(random_.next() & 1);
        r[character::kB] = std::uint8_t(two_d6(random_));  // 1000:19dd
        r[character::kHitPoints] = std::uint8_t(10 * r[character::kB]);
        r[character::kWeapon] = std::uint8_t(random_.next() % 14);
    }
}

// BTECH.EXE 19b8:104e (Ghidra FUN_19b8_104e), special 1e (docs/combat.md 1.6)
// PARTY's alley: the first companion joins, then two to five soldiers on
// foot attack, a fight of kind -1.
bool Town::alley_fight(KeySource& keys) {
    position_ = {0x0a69, 0x805e};
    // The companion, record 1.
    auto& rex = characters_[1];
    rex[character::kName] = next_name_++;
    rex[character::kB] = 12;
    rex[character::kD] = 9;
    rex[character::kC] = 8;
    for (int i = 0; i < character::kSkillCount; ++i)
        rex[std::size_t(character::kSkills + i)] = std::uint8_t(tables_.alley_companion_skills[std::size_t(i)]);
    rex[character::kWeapon] = 7;
    rex[character::kMech] = character::kOnFoot;
    rex[character::kArmourType] = 0;
    rex[character::kArmour] = 0;
    rex[character::kHitPoints] = 120;
    rex[0x10] = 0;
    figs_.sprite_base[5] = 0;
    // His 'Mech, held: the first free letter slot (k = 4 with none free
    // would write past them; not reached).
    int k = 0;
    while (k < 4 && held_mechs_[std::size_t(k)] != 0xff) ++k;
    if (k < 4 && rules_ && rules_->mechs.size() > 3) {
        mechs_[std::size_t(k)] = rules_->mechs[3].record;
        held_mechs_[std::size_t(k)] = 'C';
        mechs_[std::size_t(k)][0] = 0xff;
        mechs_[std::size_t(k)][0x79] = 1;
        figs_.sprite_base[std::size_t(k)] = 0x92;
    } else {
        log_.push_back("the alley companion's 'Mech with no free slot: not rebuilt");
    }
    // The townspeople's places kept in their records; the enemy records
    // emptied; every figure emptied, then Jason and the companion active.
    people_.leave_for_fight();
    townspeople_away_ = true;
    for (int m = 4; m < 8; ++m) mechs_[std::size_t(m)][0] = 0xff;
    for (int c = 8; c < 16; ++c) characters_[std::size_t(c)][character::kName] = character::kEmpty;
    for (int f = 0; f < FightFigures::kCount; ++f) {
        figs_.destroyed[std::size_t(f)] = false;
        figs_.active[std::size_t(f)] = 0;
        figs_.x[std::size_t(f)] = figs_.y[std::size_t(f)] = 0xffff;
    }
    figs_.active[4] = figs_.active[5] = 1;
    figs_.computer[4] = figs_.computer[5] = false;
    // The soldiers: records 8 to 9-12, figures 16-20.
    const int base_x = 0x0a72 + (random_.next() & 1);
    const int base_y = 0x805d + (random_.next() & 1);
    const int last = 10 + (random_.next() & 3);
    for (int c = 8; c < last; ++c) {
        const int f = c + 8;
        auto& r = characters_[std::size_t(c)];
        r[character::kName] = 0;
        r[character::kMech] = character::kOnFoot;
        figs_.sprite_base[std::size_t(f)] = 0xfe;
        animation_.set_frame(f, 0x1c);
        animation_.start_script(f, 0x2e0);
        figs_.heading[std::size_t(f)] = 6;
        people_.walkers()[std::size_t(f - 16)].heading = 6;  // one table (F:3920)
        animation_.set_direction(f, 6);
        figs_.active[std::size_t(f)] = 1;
        figs_.x[std::size_t(f)] = std::uint16_t(base_x + tables_.alley_offset_x[std::size_t(c - 8)]);
        figs_.y[std::size_t(f)] = std::uint16_t(base_y + tables_.alley_offset_y[std::size_t(c - 8)]);
        for (int i = 0; i < 6; ++i) r[std::size_t(character::kSkills + i)] = std::uint8_t(random_.next() & 1);
        r[character::kB] = std::uint8_t(two_d6(random_));
        r[character::kHitPoints] = std::uint8_t(10 * r[character::kB]);
        r[character::kWeapon] = std::uint8_t(random_.next() % 8);
    }
    log_.push_back("the alley fight: " + std::to_string(last - 8) + " soldiers");
    // 2.1, 3: the window's nine block values, for the after-fight steps.
    const auto& blocks = terrain_.held_window(window_at_).blocks;
    for (std::size_t i = 0; i < encounter_blocks_.size(); ++i) encounter_blocks_[i] = std::uint8_t(blocks[i]);
    encounter_entry_ = position_;
    // As after an encounter, the fight's last drawing stays on the screen
    // until the next tick draws the map (2.12).
    tick_redraws_ = true;
    return fight(-1, keys);
}

// BTECH.EXE 203b:000a (Ghidra FUN_203b_000a), a fight of kind 1 (a training
// mission) or 0 (a random encounter) up to its first orders menu: for kind 1
// the party's 'Mechs placed around the party (kind 0 has placed its figures
// already, random_encounter), then three questions in the party box.
bool Town::fight(int kind, KeySource& keys) {
    picked_ = 0x0c;  // 2.1, 1: DG:2b20
    const formats::MapPosition at = position_;
    if (kind != 0) {
    // 2.1: every figure's path ends at once, no targets, no orders; the
    // 'Mechs cool; the computer moves no party figure.
    for (int f = 0; f < CombatState::kFigures; ++f) {
        combat_.paths[std::size_t(f)].fill(2);
        combat_.targets[std::size_t(f)].fill(0xff);
        combat_.orders[std::size_t(f)].fill(0xff);
    }
    combat_.heat.fill(0);
    combat_.burning.fill(0);
    for (int f = 0; f < 12; ++f) figs_.computer[std::size_t(f)] = false;
    // Figures 16-23 share the townspeople's headings (F:3920 + 16 + i).
    for (int i = 0; i < 8; ++i) figs_.heading[std::size_t(16 + i)] = people_.walkers()[std::size_t(i)].heading;
    // Each 'Mech of records 0-3 at the k-th 'Mech place (the walk-in's).
    int k = 0;
    for (int m = 0; m < 4; ++m) {
        if (mechs_[std::size_t(m)][0] == 0xff) continue;
        figs_.x[std::size_t(k)] = move_x(at.x, tables_.mech_formation_x[std::size_t(k)]);
        figs_.y[std::size_t(k)] = move_y(at.y, tables_.mech_formation_y[std::size_t(k)]);
        ++figs_.active[std::size_t(k)];
        ++k;
    }
    {
        // 2.2: the characters on foot at the formation's places, before
        // the 'Mechs in the program.
        int p = 0;
        for (int c = 0; c < 8; ++c) {
            if (!on_foot(c)) continue;
            const int f = 4 + c;
            figs_.x[std::size_t(f)] = move_x(at.x, tables_.foot_formation_x[std::size_t(p)]);
            figs_.y[std::size_t(f)] = move_y(at.y, tables_.foot_formation_y[std::size_t(p)]);
            ++figs_.active[std::size_t(f)];
            ++p;
        }
    }
    }
    // 2.1: the entry position, where kind 3 walks back to.
    if (kind == 3) encounter_entry_ = at;

    // F:009e = 0; then 15ab:0b5e: "Do you want the computer to fight for
    // you?" with F:009e as the default, so always No; not asked with
    // F:0090 (mission 4).
    // In the raid: "...", a key (24d3:17c6 / 17ea, 273d:0259).
    if (raid_) {
        open_message_box();
        print_id("fight.raid_mechs");
        if (!read_key(keys)) {
            place_open_ = true;
            return false;
        }
        raid_line_ = 0;
    }
    if (kind == 2) {
        // 203b:04e6: the Arena's view set on the party and drawn (287f:1314,
        // 18ef, 1000:0e4b, 273d:06c3).
        build_window();
        page_ = Frozen{live_view(), fight_figures_drawn()};
        page_sprites_.clear();
        show_page();
    }
    computer_ = false;
    if (!no_computer_order_) {
        text_.select_window(3);
        text_.clear_window();
        party_box_shown_ = false;
        print_id("fight.computer_question");
        const auto computer = yes_no(computer_, keys);
        if (!computer) {
            place_open_ = true;
            return false;
        }
        computer_ = *computer;
    }
    // 203b:24f0: the combat messages, a menu of three (menu record 3 from
    // row 2, starting on the last choice).
    text_.select_window(3);
    clear_window();
    print_id("options.messages");
    menus_[3].first_row = 2;
    menus_[3].items = 3;
    menu_last_[3] = messages_;
    const auto messages = menu(3, keys);
    if (!messages) return false;
    menus_[3].first_row = 0;
    messages_ = *messages;
    // 203b:2556: "See combat graphics:"
    text_.select_window(3);
    text_.clear_window();
    print_id("options.graphics");
    const auto graphics = yes_no(graphics_, keys);
    if (!graphics) {
        place_open_ = true;
        return false;
    }
    graphics_ = *graphics;
    return fight_rounds(kind, keys);
}

// BTECH.EXE 1e31:02e4 (Ghidra FUN_1e31_02e4)
// A person's condition: the line for hit points / B (character byte 1).
bool Town::print_condition(int c) {
    const auto& r = characters_[std::size_t(c)];
    const int b = std::int8_t(r[1]);
    const int hp = std::int8_t(r[character::kHitPoints]);
    if (b == 0 || hp / b < 0 || hp / b > 10) {
        log_.push_back("a person's condition past the table (hit points / B): not rebuilt");
        fight_unfinished_ = true;
        return false;
    }
    print_id("condition." + std::to_string(hp / b));
    return true;
}

// BTECH.EXE 15ab:18e8 (Ghidra FUN_15ab_18e8), in the upper box. For a
// 'Mech: its pilot's name, "'s" and a new line, and the 'Mech's name (the
// flags unused). For a person: the character's name; with `details` (the
// scan list) then its condition, weapon and armour, unless it is the double
// agent found out (`agent_check`). The orders menu passes both flags 0.
bool Town::print_unit_heading(int fig, bool agent_check, bool details, KeySource* keys) {
    text_.set_fg(TextRole::Text);
    if (fig >= 4) {
        const int c = fig - 4;
        const auto& r = characters_[std::size_t(c)];
        print_id("character." + std::to_string(r[character::kName]));
        if (!details) return true;
        if (flags_[0x27] != 0 && std::int8_t(flags_[0x25]) == c && agent_check) {
            print_id("unit.suspicious");
            if (!keys || !read_key(*keys)) {  // 273d:086a, 273d:0259
                place_open_ = true;
                return false;
            }
            agent_found_ = true;
            return true;
        }
        if (!print_condition(c)) return false;
        text_.set_cursor(0, 7);
        text_.set_fg(TextRole::Label);  // 1000:28a2
        print_id("unit.weapon_armour");
        text_.set_cursor(0, 8);
        print_id(formats::weapon_text_id(std::int8_t(r[0x0b])));
        text_.set_cursor(0, 10);
        const int armour = std::int8_t(r[0x0d]);
        if (armour == 0) {
            print_id("unit.none");
        } else if (r[0x0e] == 0) {
            text_.set_fg(TextRole::Destroyed);
            print_id("unit.ruined");
        } else {
            if (armour < 0 || armour >= int(rules_->combat.personal_armour.size())) {
                log_.push_back("a person's armour type past the table: not rebuilt");
                fight_unfinished_ = true;
                return false;
            }
            if (std::int8_t(rules_->combat.personal_armour[std::size_t(armour)]) != std::int8_t(r[0x0e])) text_.set_fg(TextRole::Worn);
            print_id("armour." + std::to_string(armour));
        }
        return true;
    }
    const auto& rec = mechs_[std::size_t(fig)];
    const auto& pilot = characters_[std::size_t(rec[kPilot])];
    print_id("character." + std::to_string(pilot[character::kName]));
    print_id("unit.possessive");
    if (const auto name = mech_name_id(fig)) print_id(*name);
    else log_.push_back("a 'Mech record with no template's name");
    return true;
}

std::optional<std::string> Town::mech_name_id(int record) const {
    return template_name_id(mechs_[std::size_t(record)]);
}

// The text ID of the template whose name a record's name matches.
std::optional<std::string> Town::template_name_id(const formats::MechRecord& rec) const {
    const auto ids = formats::mech_template_ids();
    for (std::size_t t = 0; t < rules_->mechs.size() && t < ids.size(); ++t)
        if (std::equal(rec.begin(), rec.begin() + formats::kMechNameSize, rules_->mechs[t].record.begin()))
            return formats::mech_text_id(ids[t]);
    return std::nullopt;
}

// BTECH.EXE 203b:14c3 (Ghidra FUN_203b_14c3)
// The orders menu, from the first active party unit: its heading and its
// screen, then the menu, until Begin Fight or Flee. The bar starts on Begin
// Fight when the menu is entered and then stays where it was left.
std::optional<int> Town::orders_menu(Combat& combat, KeySource& keys) {
    const auto stop = [this](const std::string& what) {
        log_.push_back(what + ": not rebuilt");
        fight_unfinished_ = true;
        return std::optional<int>{};
    };
    int fig = 0;
    while (fig < 12 && figs_.active[std::size_t(fig)] == 0) ++fig;
    if (fig == 12) return stop("orders with no party unit");
    if (no_computer_order_) figs_.computer[std::size_t(fig)] = false;
    bool entered = true;
    for (;;) {
        text_.select_window(4);
        clear_window();
        print_unit_heading(fig, false, false);
        text_.set_fg(TextRole::Text);
        if (figs_.computer[std::size_t(fig)]) {
            print_id("orders.computer_moving");
            plan_shown(combat, fig);
        } else {
            show_unit(combat, fig, true);
        }
        text_.select_window(3);
        text_.clear_window();
        text_.set_fg(TextRole::Text);
        const bool mech = fig < 4;
        print_id(mech ? "orders.menu_mech" : "orders.menu_person");  // DG:3a6f, DG:3abc
        const int items = mech ? 10 : 8;
        menus_[3].items = items;
        if (entered) menu_last_[3] = items - 1;
        entered = false;
        const auto chosen = menu(3, keys);
        if (!chosen) return std::nullopt;
        const int c = *chosen;
        if (c >= items - 2) return c == items - 2 ? 1 : 0;
        if ((mech && c < 3) || (!mech && c == 0))
            if (!move_order(combat, fig, c, keys)) return std::nullopt;
        if (!mech && c == 1) {
            // Clear Moves: the orders deleted, the computer's control off.
            combat_.orders[std::size_t(fig)].fill(0xff);
            figs_.computer[std::size_t(fig)] = false;
            show_unit(combat, fig, false);
        }
        if (mech && c == 4)
            if (!kick_order(combat, fig, keys)) return std::nullopt;                                  // 203b:2231
        if (((mech && c == 5) || (!mech && c == 3)) && !no_computer_order_) plan_shown(combat, fig);
        if ((mech && c == 3) || (!mech && c == 2))
            if (!weapons_order(combat, fig, keys)) return std::nullopt;
        if ((mech && c == 6) || (!mech && c == 4))
            if (!scan_order(combat, fig, keys)) return std::nullopt;
        if (c == items - 3) {
            // Next Unit: the next active party unit, 11 wrapping to 0, the
            // bar on Next Unit in its menu.
            do fig = (fig + 1) % 12;
            while (figs_.active[std::size_t(fig)] == 0);
            menu_last_[3] = fig < 4 ? 7 : 5;
        }
    }
}

// BTECH.EXE 1e31:03ab (Ghidra FUN_1e31_03ab) with show: the computer plans
// party unit `fig`, and its screen is shown with the planned path
// (203b:1774(fig, 1)); then the position it started from.
void Town::plan_shown(Combat& combat, int fig) {
    const auto saved = position_;
    combat.plan(fig);
    show_unit(combat, fig, true);
    walk_position(saved);  // 1000:17bb
}

// BTECH.EXE 203b:1774 (Ghidra FUN_203b_1774)
// Unit `fig` on the orders screen: with `redraw` the view moved onto it and
// drawn with the fight's figures; the page shown, a box XORed over the unit
// (3x3 cells for a 'Mech, one for a person) and, for a party unit, its
// path worked out from its orders and drawn as arrows from its cell.
void Town::show_unit(Combat& combat, int fig, bool redraw) {
    if (redraw) {
        walk_position({figs_.x[std::size_t(fig)], figs_.y[std::size_t(fig)]});  // 1000:17bb
        page_ = Frozen{live_view(), fight_figures_drawn()};                  // 287f:1314, 18ef, 1000:0e4b
        page_sprites_.clear();
    }
    show_page();
    const bool mech = fig < 4 || (fig >= 12 && fig < 16);
    boxes_ = {mech ? XorBox{25, 10, 3, 3, mark_bits(BoxMark::Unit)} : XorBox{26, 12, 1, 1, mark_bits(BoxMark::Unit)}};  // 287f:2b87 per row
    boxes_since_ = text_.paint_clock();
    if (fig >= 12) return;
    if (fig < 4) combat.mech_mp(fig, std::int8_t(combat_.orders[std::size_t(fig)][0]));  // 203b:22bc
    else combat.person_mp(fig);                                                         // 203b:2474
    combat.simulate_path(fig);
    int col = 0x1a, row = 0x0c;
    const auto& path = combat_.paths[std::size_t(fig)];
    for (int i = 0; i + 1 < CombatState::kPathBytes && path[std::size_t(i)] != 2; i += 2) {
        const int dx = path[std::size_t(i)], dy = path[std::size_t(i + 1)];
        col += dx;
        row += dy;
        const char glyph = char(tables_.path_arrows[std::size_t((dy + 1) * 4 + dx + 1)]);
        // An arrow written inside the box covers the XOR there (boxes()).
        text_.put_text(std::string_view(&glyph, 1), col, row, TextRole::Text, TextRole::Background);  // 273d:00d5
    }
    settle_boxes();
    path_end_col_ = col;
    path_end_row_ = row;
}

// BTECH.EXE 1d43:0004 (Ghidra FUN_1d43_0004)
// Use Weapons for unit `fig`: a 'Mech's weapons listed in window 5 with
// their ammunition, target and range band, until Done; a person's one
// weapon at a new target (docs/combat.md 2.6). The display is EGA's.
bool Town::weapons_order(Combat& combat, int fig, KeySource& keys) {
    auto& targets = combat_.targets[std::size_t(fig)];
    if (fig < 12) figs_.computer[std::size_t(fig)] = false;
    const auto finish = [&] {
        text_.set_fg(TextRole::Text);
        text_.select_window(4);
        clear_window();
        text_.frame(4);
        text_.set_fg(TextRole::Text);
        text_.select_window(3);
        text_.clear_window();
        text_.frame(3);
        text_.set_fg(TextRole::Text);
        return true;
    };
    if (fig >= 4) {
        // On foot: the weapon's target now, and whether to change it.
        show_unit(combat, fig, true);
        text_.select_window(4);
        clear_window();
        const int t = std::int8_t(targets[0]);
        const int w = std::int8_t(characters_[std::size_t(fig - 4)][0x0b]);
        bool change = true;
        if (t != -1) {
            print_id("weapons.using");
            print_id(formats::weapon_text_id(w));
            print_id("weapons.against");
            if (t >= 16) print_id("weapons.human");
            else if (const auto name = mech_name_id(t - 8)) print_id(*name);
            print_id("weapons.at");
            print_id("weapons.range." + std::to_string(combat.range_band(t, w)));
            print_id("weapons.range_override");
            const auto yes = yes_no(true, keys);
            if (!yes) {
                place_open_ = true;
                return false;
            }
            change = *yes;
        }
        if (change) {
            text_.clear_window();
            print_id("pick.use");
            print_id(formats::weapon_text_id(w));
            print_id("pick.against");
            if (!pick_target(combat, fig, w, 0, keys)) return false;
        }
        return finish();
    }
    // A 'Mech: its first twelve weapon items (0x10-0x20, bit 7 destroyed).
    const auto& rec = mechs_[std::size_t(fig)];
    std::array<std::uint8_t, 12> items{};
    int count = 0;
    for (int b = mech::kItems; b < mech::kItemsEnd; ++b) {
        const int t = rec[std::size_t(b)] & 0x7f;
        if (count < 12 && t >= 0x10 && t <= 0x20) items[std::size_t(count++)] = rec[std::size_t(b)];
    }
    text_.select_window(4);
    clear_window();
    if (combat_.heat[std::size_t(fig)] == 0x1e) {
        // Shut down: every weapon held this round.
        print_id("weapons.shut_down");
        if (!press_key(keys)) return false;
        for (auto& t : targets) t |= 0x80;
        return finish();
    }
    const int sensors = rec[mech::kSensorHits];
    if (sensors == 1 || sensors == 2) {
        print_id(sensors == 1 ? "weapons.sensors_damaged" : "weapons.sensors_destroyed");
        if (!read_key(keys)) return false;
        text_.clear_window();
        if (sensors == 2) return true;  // straight back, without the windows' frames
    }
    print_id("weapons.select");  // once: the loop comes back after it (1d43:0151)
    int choice = 0;
    do {
        show_unit(combat, fig, true);
        // Window 5 holds the list: one row a weapon, a heading and Done.
        text_.place_window(5, 0x16 - count, count + 2);
        text_.select_window(5);
        text_.frame(0);
        text_.clear_window();
        text_.set_fg(TextRole::Heading);  // 1000:28a2
        print_id("weapons.heading");
        for (int i = 0; i < 12; ++i) {
            text_.set_cursor(0, i + 1);
            if (items[std::size_t(i)] == 0) continue;
            int v = std::int8_t(items[std::size_t(i)]);
            auto& target = targets[std::size_t(i)];
            // Yellow with a target, white without, grey destroyed.
            text_.set_fg(target == 0xff ? TextRole::Text : TextRole::Targeted);
            if (v & 0x80) text_.set_fg(TextRole::Destroyed);
            print_id(formats::weapon_text_id((v & 0x7f) - 1));
            text_.set_cursor(0x0b, i + 1);
            if (v & 0x80) {
                print_id("weapons.destroyed");
                target = 0xff;
                continue;
            }
            v &= 0x7f;
            const int ammo = rec[std::size_t(mech::kAmmunition + i)];
            if (ammo == 0xff) {
                print_id("weapons.ammo_full");
            } else if (ammo == 0) {
                print_id("weapons.ammo_out");
            } else {
                const std::string n = std::to_string(ammo);  // 287f:3bb6
                for (std::size_t k = n.size(); k < 3; ++k) print_id("weapons.space");
                text_.print(n);
            }
            text_.set_cursor(0x10, i + 1);
            if (target != 0xff && figs_.active[std::size_t(target & 0x7f)] == 0) target = 0xff;
            if (target != 0xff) {
                // Listing a weapon clears its held bit.
                target &= 0x7f;
                const int t = target;
                if (t >= 16) print_id("weapons.human");
                else if (const auto name = mech_name_id(t - 8)) print_id(*name);
                text_.set_cursor(0x18, i + 1);
                print_id("weapons.range." + std::to_string(combat.range_band(t, v - 1)));
            } else {
                print_id("weapons.none");
            }
        }
        text_.set_cursor(0, count + 1);
        print_id("weapons.done");
        menus_[5].items = count + 1;
        text_.select_window(5);
        const auto chosen = menu(5, keys);
        if (!chosen) return false;
        choice = *chosen;
        if (choice >= count) break;
        const auto bar_off = [&] {
            // The menu left its bar on the weapon: XORed off again.
            const auto& w = text_.window();
            text_.menu_bar(w.x, w.y + menus_[5].first_row + choice, menus_[5].width, 5);
        };
        const auto message = [&](const std::string& id) {
            // 1e31:1057: in window 4, a key, then back.
            const int previous = text_.window_number();
            text_.select_window(4);
            clear_window();
            print_id(id);
            const bool got = read_key(keys).has_value();
            text_.clear_window();
            text_.select_window(previous);
            return got;
        };
        if (items[std::size_t(choice)] & 0x80) {
            if (!message("weapons.choose_other")) return false;
            bar_off();
        } else if (rec[std::size_t(mech::kAmmunition + choice)] == 0) {
            if (!message("weapons.no_ammo")) return false;
            bar_off();
        } else {
            bool go = true;
            if (targets[std::size_t(choice)] != 0xff) {
                text_.select_window(4);
                clear_window();
                print_id("weapons.already_firing");
                const auto yes = yes_no(true, keys);
                if (!yes) {
                    place_open_ = true;
                    return false;
                }
                go = *yes;
                text_.clear_window();
            }
            if (go) {
                targets[std::size_t(choice)] = 0xff;
                text_.select_window(4);
                text_.frame(4);
                text_.clear_window();
                print_id("weapons.choose_target");
                if (!pick_target(combat, fig, (items[std::size_t(choice)] & 0x7f) - 1, choice, keys)) return false;
                text_.select_window(3);
                text_.clear_window();
                text_.select_window(4);
                clear_window();
            }
        }
    } while (choice != count);
    return finish();
}

// BTECH.EXE 203b:2591 (Ghidra FUN_203b_2591)
// Scan Unit: Friends, Enemies or Cancel, then the list of that side. A
// person cannot see enemy 'Mechs (and is told so while any is active); a
// 'Mech whose sensors are destroyed cannot either.
bool Town::scan_order(Combat& combat, int fig, KeySource& keys) {
    bool mechs = true;  // whether the list holds 'Mechs
    text_.select_window(3);
    text_.clear_window();
    print_id("scan.which");
    menus_[3].first_row = 1;
    menus_[3].items = 3;
    menu_last_[3] = 0;
    const auto chosen = menu(3, keys);
    if (!chosen) return false;
    const int c = *chosen;
    bool any = true;
    if (c == 1) {
        text_.select_window(4);
        clear_window();
        if (fig >= 4) {
            bool enemy_mech = false;
            for (int i = 12; i < 16; ++i)
                if (figs_.active[std::size_t(i)] != 0) enemy_mech = true;
            if (enemy_mech) {
                print_id("scan.humans_only");
                if (!read_key(keys)) return false;  // 273d:0259
            }
            any = false;
            mechs = false;
        } else if (mechs_[std::size_t(fig)][mech::kSensorHits] == 2) {
            print_id("scan.no_sensors");
            mechs = false;
            if (!read_key(keys)) return false;
            text_.clear_window();
            // Bug 19: the "no enemy humans" check below is skipped.
            if (fixes_.fix_scan_sensors) any = false;
        }
    }
    for (int i = 16; i < 24; ++i)
        if (figs_.active[std::size_t(i)] != 0 && figs_.x[std::size_t(i)] != 0xffff) any = true;
    if (!any && !mechs && c == 1) {
        text_.select_window(4);
        clear_window();
        print_id("scan.no_humans");
        if (!press_key(keys)) return false;  // 1000:2a4f
    }
    text_.clear_window();
    if (c < 2 && (any || mechs))
        if (!scan_list(combat, fig, 12 * c, mechs, keys)) return false;
    menus_[3].first_row = 0;
    menu_last_[3] = fig < 4 ? 6 : 4;  // F:00c8: the bar back on Scan Unit
    return true;
}

// BTECH.EXE 15ab:1467 (Ghidra FUN_15ab_1467)
// The scan list: each active, placed figure of the twelve from `base` (the
// people only, without `mechs`) in turn, shown on the map with its heading,
// until Done.
bool Town::scan_list(Combat& combat, int fig, int base, bool mechs, KeySource& keys) {
    const int first = base + (mechs ? 0 : 4);
    int t = first;
    int chosen = 0;
    int unseen = 0;  // figures passed over in a row (fix 32)
    do {
        if (figs_.active[std::size_t(t)] != 0 && figs_.x[std::size_t(t)] != 0xffff &&
            figs_.y[std::size_t(t)] != 0xffff) {
            unseen = 0;
            text_.select_window(4);
            clear_window();
            text_.set_fg(TextRole::Text);
            if (t < 12) {
                if (!print_unit_heading(t, true, true, &keys)) return false;  // 15ab:18e8(t, 1, 1)
            } else {
                if (t < 16) {
                    // An enemy 'Mech: its record's name as the record holds
                    // it (padded with spaces).
                    print_id("scan.enemy");
                    const auto& rec = mechs_[std::size_t(t - 8)];
                    std::string name;
                    for (int i = 0; i < formats::kMechNameSize && rec[std::size_t(i)] != 0; ++i)
                        name += char(rec[std::size_t(i)]);
                    text_.print(name);
                } else {
                    // An enemy person: its condition and its weapon.
                    print_id("scan.enemy_human");
                    if (!print_condition(t - 8)) return false;  // 1e31:02e4
                    text_.set_cursor(0, 7);
                    text_.set_fg(TextRole::Label);  // 1000:28a2
                    print_id("scan.weapon");
                    text_.set_cursor(0, 8);
                    print_id(formats::weapon_text_id(std::int8_t(characters_[std::size_t(t - 8)][0x0b])));
                }
                // Then the direction it lies in.
                text_.set_fg(TextRole::Label);  // 1000:28a2
                print_id("scan.direction");
                const int d = direction_toward({figs_.x[std::size_t(fig)], figs_.y[std::size_t(fig)]},
                                               {figs_.x[std::size_t(t)], figs_.y[std::size_t(t)]}, tables_);
                static const char* const kNames[] = {"north", "northeast", "east", "southeast",
                                                     "south", "southwest", "west", "northwest"};
                if (d >= 0) print_id(std::string("walk.direction.") + kNames[d]);
                else log_.push_back("bug 32: the scan's direction of one figure from itself (not printed)");
            }
            show_unit(combat, t, true);
            text_.select_window(3);
            text_.clear_window();
            menus_[3].items = 2;
            print_id("scan.menu");
            bool detail = false;
            if (mechs && (t < 4 || (t >= 12 && t < 16))) {
                print_id("scan.detail");
                menus_[3].items = 3;
                detail = true;
            }
            print_id("scan.done");
            menus_[3].first_row = 1;
            menu_last_[3] = 0;
            const auto c = menu(3, keys);
            if (!c) return false;
            chosen = *c;
            if (chosen == 1 && detail) {
                // Detail Scan: the stats screen, the screen drawn again, and
                // the same figure shown again.
                chosen = 0;
                if (!mech_stats(t, keys)) return false;  // 15ab:1afe
                redraw_map_screen(true);                 // 1e31:032f(1)
                --t;
            }
        } else if (fixes_.fix_scan_list && ++unseen > 12) {
            log_.push_back("fix 32: a scan list with nobody in it ends");
            break;
        }
        if (++t > base + 11) t = first;
        if (combat_.spectator && t > 11) t = 12;  // the Arena: the opponent only
    } while (chosen == 0);
    menus_[3].first_row = 0;
    menu_last_[3] = 6;
    text_.set_fg(TextRole::Text);
    return true;
}

// BTECH.EXE 203b:2231 (Ghidra FUN_203b_2231)
// Kick: with both hips, the target picker for the kick (weapon item 0x21)
// into target slot 11; without, a message and a key. The orders menu's bar
// goes back to Kick.
bool Town::kick_order(Combat& combat, int fig, KeySource& keys) {
    text_.select_window(4);
    clear_window();
    const auto& rec = mechs_[std::size_t(fig)];
    if ((rec[mech::kLeftActuators] & 8) && (rec[mech::kRightActuators] & 8)) {
        print_id("orders.choose_kick");
        if (!pick_target(combat, fig, 0x20, 0x0b, keys)) return false;
    } else {
        print_id("orders.cant_kick");
        if (!read_key(keys)) return false;  // 273d:0259
    }
    menu_last_[3] = 4;  // F:00c8
    return true;
}

// BTECH.EXE 1d43:07cb (Ghidra FUN_1d43_07cb)
// The target picker: the enemy figures 12-23 in turn from the last one
// picked, each shown on the map with its name and range band from unit
// `fig` for weapon `w`, until one is taken for target slot `slot` or the
// choice is cancelled. No range or line-of-sight check.
bool Town::pick_target(Combat& combat, int fig, int w, int slot, KeySource& keys) {
    text_.set_fg(TextRole::Label);  // 1000:28a2
    print_id("pick.target_range");
    int choice = 1;
    do {
        if (figs_.active[std::size_t(picked_)] != 0) {
            const int t = picked_;
            text_.select_window(3);
            text_.frame(3);
            text_.clear_window();
            menus_[3].items = 3;
            print_id("pick.menu");
            show_unit(combat, t, true);
            text_.select_window(4);
            if (t >= 16) {
                text_.set_fg(TextRole::Label);
                text_.set_cursor(0, 8);
                print_id("pick.weapon");
            } else {
                text_.fill(1, 9, 10, 2, TextRole::Background);  // 273d:01fb (8, 0x48)-(0x57, 0x57)
            }
            text_.set_cursor(0, 5);
            if (t > 15) {
                print_id("pick.human");
                text_.fill(1, 10, 10, 1, TextRole::Background);
                const int weapon = characters_[std::size_t(t - 8)][0x0b];
                text_.put_text(lookup(texts_, formats::weapon_text_id(weapon)), 1, 10, TextRole::Text, TextRole::Background);  // 273d:00d5
            } else {
                if (const auto name = mech_name_id(t - 8)) print_id(*name);
                text_.fill(1, 9, 10, 1, TextRole::Background);
            }
            text_.set_cursor(0, 7);
            walk_position({figs_.x[std::size_t(fig)], figs_.y[std::size_t(fig)]});  // 1000:17bb
            print_id("weapons.range." + std::to_string(combat.range_band(t, w)));
            print_id("pick.blank");
            text_.select_window(3);
            const auto chosen = menu(3, keys);
            if (!chosen) return false;
            choice = *chosen;
            if (choice == 0) combat_.targets[std::size_t(fig)][std::size_t(slot)] = std::uint8_t(t);
            if (choice == 2) combat_.targets[std::size_t(fig)][std::size_t(slot)] = 0xff;
            if (choice != 1) continue;
        }
        if (++picked_ > 0x17) picked_ = 0x0c;
    } while (choice == 1);
    return true;
}

void Town::xor_box(const XorBox& box) {
    // On a cell of text written over the map (a path's arrow) the XOR
    // flips the text's own colours.
    const auto text_at = [&](int c, int r) {
        return c >= 0 && c < kTextColumns && r >= 0 && r < kTextRows && text_.cell(c, r).kind != TextCell::Kind::Empty;
    };
    bool on_text = false;
    for (int r = box.row; r < box.row + box.height; ++r)
        for (int c = box.col; c < box.col + box.width; ++c) on_text = on_text || text_at(c, r);
    if (on_text) {
        for (int r = box.row; r < box.row + box.height; ++r)
            for (int c = box.col; c < box.col + box.width; ++c)
                if (text_at(c, r)) text_.box_mark(c, r, 1, box.marks);
                else xor_box({c, r, 1, 1, box.marks});
        return;
    }
    settle_boxes();
    const auto overlap = [](const XorBox& a, const XorBox& b) {
        return a.col < b.col + b.width && b.col < a.col + a.width && a.row < b.row + b.height && b.row < a.row + a.height;
    };
    std::map<std::pair<int, int>, std::uint8_t> cells;
    const auto add = [&](const XorBox& b) {
        for (int r = b.row; r < b.row + b.height; ++r)
            for (int c = b.col; c < b.col + b.width; ++c) cells[{c, r}] ^= b.marks;
    };
    std::vector<XorBox> kept;
    for (const auto& b : boxes_) {
        if (overlap(b, box)) add(b);
        else kept.push_back(b);
    }
    if (cells.empty()) {
        boxes_.push_back(box);
        return;
    }
    add(box);
    for (const auto& [cell, marks] : cells)
        if (marks != 0) kept.push_back({cell.first, cell.second, 1, 1, marks});
    boxes_ = kept;
}

// BTECH.EXE 203b:1c1f (Ghidra FUN_203b_1c1f)
// A move order of `type` (0 walk, 1 run, 2 jump; on foot 0) for unit `fig`:
// at most one waypoint, placed with a cursor on the map (docs/combat.md 2.6).
bool Town::move_order(Combat& combat, int fig, int type, KeySource& keys) {
    auto& orders = combat_.orders[std::size_t(fig)];
    if (fig < 12) {
        if (figs_.computer[std::size_t(fig)]) {
            orders.fill(0xff);
            show_unit(combat, fig, true);
        }
        figs_.computer[std::size_t(fig)] = false;
    }
    const auto no_jump_jets = [&] { return mechs_[std::size_t(fig)][mech::kJump] == 0; };
    // A 'Mech with orders of another type: change them? Yes deletes them.
    const int current = std::int8_t(orders[0]);
    if (fig < 4 && current != -1 && !(type == 2 && no_jump_jets()) && current != type) {
        text_.select_window(4);
        clear_window();
        print_id("orders.now");
        std::string doing = lookup(texts_, "orders.mode." + std::to_string(current));
        if (current == 1) doing += lookup(texts_, "orders.now_n");
        doing += lookup(texts_, "orders.now_ing");
        text_.print(doing);  // one string, built in DG:0012 (287f:3b68, 3b22)
        print_id("orders.wish");
        print_id("orders.mode." + std::to_string(type));
        print_id("orders.instead");
        const auto change = yes_no(true, keys);
        if (!change) {
            place_open_ = true;
            return false;
        }
        if (!*change) {
            text_.clear_window();
            return true;
        }
        text_.clear_window();
        print_id("orders.deleted");
        print_id("orders.new_moves");
        orders.fill(0xff);
        show_unit(combat, fig, false);
        if (!read_key(keys)) return false;
    }
    // The new waypoint goes in the first free order.
    int n = 0;
    while (n < CombatState::kOrderBytes && orders[std::size_t(n)] != 0xff) n += 4;
    if (fig < 4) combat.mech_mp(fig, type);
    else combat.person_mp(fig);
    combat.simulate_path(fig);
    text_.select_window(4);
    clear_window();
    if (combat.mp() == 0) {
        // Why not, and a key.
        if (type == 2 && no_jump_jets()) {
            print_id("orders.no_jump_jets");
        } else if (fig < 4 && combat_.heat[std::size_t(fig)] == 0x1e) {
            print_id("orders.overheated");
        } else {
            if (fig >= 4 && fig < 12) combat.clear_slowed();
            if (combat.legs_slowed() || combat.heat_slowed() != 0) {
                print_id("orders.inhibited");
                if (combat.legs_slowed()) print_id("orders.inhibited_legs");
                else print_id("orders.inhibited_heat");
            } else {
                print_id("orders.cant");
                print_id(fig < 4 ? "orders.mode." + std::to_string(type) : "orders.move");
                print_id("orders.no_further");
                if (fig < 4 && type != 1) print_id("orders.try_running");
            }
            combat.clear_slowed();
        }
        if (!press_key(keys)) return false;
        text_.clear_window();
        return true;
    }
    print_id("orders.you_have");
    text_.print(std::to_string(combat.mp()));  // 273d:0053
    print_id("orders.points_left");
    // The cursor starts where the shown path ends.
    xor_box({path_end_col_, path_end_row_, 1, 1, mark_bits(BoxMark::Cursor)});
    for (;;) {
        const auto k = read_key(keys);  // 273d:0259
        if (!k) return false;
        const int key = arrow_key(*k);
        const auto step = step_for_key(key);
        const int dx = step ? step->dx : 0, dy = step ? step->dy : 0;
        // Kept inside the map's columns and rows on screen.
        path_end_col_ += dx;
        if (path_end_col_ < 0x0d || path_end_col_ > 0x27) path_end_col_ -= dx;
        path_end_row_ += dy;
        if (path_end_row_ < 0 || path_end_row_ > 0x18) path_end_row_ -= dy;
        if (dx != 0 || dy != 0) {
            // The waypoint under the cursor (1000:191b from the unit),
            // unless the orders already hold it.
            const auto saved = position_;
            position_ = {move_x(position_.x, path_end_col_ - 0x1a), move_y(position_.y, path_end_row_ - 0x0c)};
            const int wx = position_.x & 0x7f, wy = position_.y & 0x7f;
            const int area = ((position_.y & 0xf000) | (position_.x & 0x0f00)) >> 8;
            // The slot: past the twelfth order, the next figure's first (bug 17).
            std::uint8_t* slot = nullptr;
            if (n < CombatState::kOrderBytes) slot = &orders[std::size_t(n)];
            else if (!fixes_.fix_order_bound && fig + 1 < CombatState::kFigures)
                slot = &combat_.orders[std::size_t(fig + 1)][0];
            else log_.push_back("fix 17: a 13th waypoint is not written");
            bool fresh = true;
            for (int j = 0; j < n; j += 4) {
                // Bug 16: the area is compared sign-extended.
                const int a = fixes_.fix_waypoint_compare ? orders[std::size_t(j + 1)] : std::int8_t(orders[std::size_t(j + 1)]);
                if (a == area && std::int8_t(orders[std::size_t(j + 2)]) == wx && std::int8_t(orders[std::size_t(j + 3)]) == wy) {
                    fresh = false;
                    if (slot) slot[0] = 0xff;
                }
            }
            if (fresh && slot) {
                slot[0] = std::uint8_t(type);
                slot[1] = std::uint8_t(area);
                slot[2] = std::uint8_t(wx);
                slot[3] = std::uint8_t(wy);
            }
            position_ = saved;
            const int col = path_end_col_, row = path_end_row_;
            show_unit(combat, fig, false);
            path_end_col_ = col;
            path_end_row_ = row;
            xor_box({col, row, 1, 1, mark_bits(BoxMark::Cursor)});
        }
        if (key == key::kSpace || key == key::kEnter) break;
    }
    text_.clear_window();
    return true;
}

// BTECH.EXE 1000:2a4f (Ghidra FUN_1000_2a4f)
// "Press a key." in the current window, and a key.
bool Town::press_key(KeySource& keys) {
    print_id("ui.press_key");
    if (!read_key(keys)) {
        place_open_ = true;
        return false;
    }
    return true;
}

// BTECH.EXE 273d:06c3 (Ghidra FUN_273d_06c3)
// The page's map columns (13-39) copied to the screen: whatever text, line
// or box was there is gone.
void Town::show_page() {
    if (page_) frozen_ = page_;
    screen_sprites_ = page_sprites_;
    screen_lines_.clear();
    boxes_.clear();
    boxes_since_ = text_.paint_clock();
    text_.forget(13, 0, kTextColumns - 13, kTextRows);
}

void Town::hold_screen(int frames) {
    events_.push_back({Shown{view(), figures(), screen_sprites_, screen_lines_, text_, boxes(), picture_}, frames, -1});
}

void Town::play_sound(int n) {
    events_.push_back({std::nullopt, 0, n});
    if (sound_.on) play_entries(sound_, tables_.sound_lists, n);  // 27c5:0002
}

Combat Town::combat_rules() {
    Combat::Hooks hooks;
    hooks.print = [this](const std::vector<std::string>& ids) {
        std::string line;
        for (const auto& id : ids) line += lookup(texts_, id);
        text_.print(line);
    };
    hooks.set_fg = [this](TextRole role) { text_.set_fg(role); };
    hooks.clear_wander = [this] { people_.clear_wander(); };
    hooks.select_box = [this] { text_.select_window(4); };
    hooks.clear_box = [this] {
        text_.select_window(4);
        clear_window();
    };
    hooks.walk = [this](formats::MapPosition to) { walk_position(to); };
    hooks.draw_view = [this](bool shown) {
        if (!shown) {
            fight_figures_drawn(false);  // the fix's unshown drawing: not in the original
            return;
        }
        if (tiles_animate(map_number_)) phase_ = (phase_ + 1) % 3;  // 1000:240b
        page_ = Frozen{live_view(), fight_figures_drawn()};
        page_sprites_.clear();
    };
    hooks.show = [this](int frames) {
        show_page();
        hold_screen(frames);
    };
    hooks.hold = [this](int frames) { hold_screen(frames); };
    hooks.sprite = [this](int slot, int x, int y) { page_sprites_.push_back({sprite(slot), x, y}); };
    hooks.line = [this](const ShownLine& line) { screen_lines_.push_back(line); };
    hooks.sound = [this](int n) { play_sound(n); };
    hooks.picture = [this](int number, int mode) { show_picture(number, mode); };
    // 1e31:1dcc: 12 frames for each step of the message speed (DG:015e);
    // from 5 up it waits for a key instead (docs/combat.md 3.8).
    hooks.pause = [this] { hold_screen(12 * message_speed_); };
    hooks.log = [this](const std::string& line) {
        if (log_.empty() || log_.back() != line) log_.push_back(line);
    };
    hooks.message_box = [this] { open_message_box(); };
    hooks.key = [this] {
        if (!round_keys_ || !read_key(*round_keys_)) throw KeysRanOut{};
    };
    hooks.arena_wall = [this](bool restore) { arena_wall(restore); };
    return Combat(terrain_, blocking_tile(map_number_), position_, figs_, animation_, mechs_, characters_, flags_,
                  random_, combat_, marks_, *rules_, tables_, fixes_, messages_, graphics_, std::move(hooks));
}

// BTECH.EXE 203b:0f52..0ffb
// After each round of the raid, its next story line (DG:3a2e); when the
// fight is over, all those left. Before the second the place picture 2;
// after the third map 11 takes the town's place (27e8:00fc = 11).
bool Town::raid_lines(bool over, KeySource& keys) {
    for (int n = over ? 5 - raid_line_ : 1; n > 0; --n) {
        if (raid_line_ >= 5) continue;
        if (raid_line_ == 1) show_picture(2, 1);
        open_message_box();
        print_id("fight.raid." + std::to_string(raid_line_++));
        if (!read_key(keys)) {
            place_open_ = true;
            return false;
        }
        if (raid_line_ == 3) {
            area_maps_[0xcc] = 11;
            if (!load_town_map(11)) {
                fight_unfinished_ = true;
                return false;
            }
        }
        text_.select_window(4);
        clear_window();
    }
    return true;
}

// BTECH.EXE 203b:056b..1481, the rounds (docs/combat.md 2.5-2.8, 2.12,
// 2.13).
bool Town::fight_rounds(int kind, KeySource& keys) {
    Combat combat = combat_rules();
    bool over = false;
    int fled_how = 0;  // [bp-0x38]: 2 when the raid's way out was reached
    bool exit_zone = false;  // [bp-0xc]: the Arena's exit zone reached with flag 23
    while (!over) {
        // Each round draws the map first (287f:1314).
        terrain_.release_window();
        const formats::MapPosition start = position_;
        combat_.person_ran = false;
        if (!computer_) {
            // The player's side: units without a place drop out, every
            // party unit's path is reset, and the orders menu.
            for (int f = 0; f < 12; ++f) {
                if (figs_.active[std::size_t(f)] == 0) continue;
                if (figs_.x[std::size_t(f)] == 0xffff || figs_.y[std::size_t(f)] == 0xffff)
                    figs_.active[std::size_t(f)] = 0;
                combat_.paths[std::size_t(f)].fill(2);
            }
            const auto chosen = orders_menu(combat, keys);
            if (!chosen) return false;
            int fled = *chosen;
            // In the raid, figure 0 outside the box around the way out has
            // escaped (2), whatever was chosen.
            if (raid_) {
                const int x = std::int16_t(figs_.x[0]);
                const std::uint16_t y = figs_.y[0];
                if (y >= 0xd000 || x >= 0x0d00 || y < 0xb07f || x < 0x0b7f) fled = 2;
            }
            if (fled != 0) {
                // 2.7: Flee escapes on r & 3 not 0.
                if (fled != 2) fled = (random_.next() & 3) != 0 ? 1 : 0;
                text_.select_window(3);
                text_.clear_window();
                if (kind >= 2) fled = 0;
                if (raid_ && fled != 2) {
                    print_id("fight.no_escape_jenners");
                    fled = 0;
                    if (!press_key(keys)) return false;
                } else if (fled == 0) {
                    print_id("fight.no_escape");
                    if (!press_key(keys)) return false;
                } else if (kind > 0) {
                    combat_.person_ran = true;  // the mission is failed
                }
                log_.push_back(fled != 0 ? "flee: escaped" : "flee: failed");
            }
            over = fled != 0;
            fled_how = fled;
            // Each party unit in slot order: the computer's plan, or the
            // path of its orders when the orders screen has not worked it out.
            for (int f = 0; f < 12 && !over; ++f) {
                if (figs_.active[std::size_t(f)] == 0) continue;
                if (figs_.computer[std::size_t(f)]) {
                    combat.plan(f);  // 1e31:03ab(f, 0)
                } else if (combat_.paths[std::size_t(f)][0] == 2 && combat_.orders[std::size_t(f)][0] != 0xff) {
                    walk_position({figs_.x[std::size_t(f)], figs_.y[std::size_t(f)]});  // 1000:17bb
                    if (f < 4) combat.mech_mp(f, std::int8_t(combat_.orders[std::size_t(f)][0]));
                    else combat.person_mp(f);
                    combat.simulate_path(f);
                }
            }
        } else {
            // 203b:1482(0): the computer plans the party.
            combat.plan_side(0);
            if (combat_.person_ran) {
                // A party person met a 'Mech: with no party 'Mech left, the
                // party may run (r & 3).
                bool no_mech = true;
                for (int m = 0; m < 4; ++m)
                    if (mechs_[std::size_t(m)][0] != 0xff) no_mech = false;
                if (no_mech && (random_.next() & 3) != 0) over = true;
            }
        }
        if (!over) {
            position_ = start;
            combat_.enemy_people_ran = false;
            combat.plan_side(12);
            position_ = start;
            // A key the round waits for (the Arena's spectator) may find none:
            // the fight stops there, as the original waits.
            round_keys_ = &keys;
            try {
                combat.round(kind > 0);
            } catch (const KeysRanOut&) {
                round_keys_ = nullptr;
                place_open_ = true;
                return false;
            }
            round_keys_ = nullptr;
            if (combat_.fight_on && flags_[0x27] != 0 && agent_found_ && !double_agent_acts(keys)) return false;
            // 2.8: the end tests.
            exit_zone = false;
            if (flags_[0x23] != 0 && figs_.active[0] != 0 && figs_.x[0] > 0x0900 && figs_.x[0] < 0x0a07)
                exit_zone = true;
            over = true;
            for (int f = 12; f < 24; ++f) {
                if (figs_.x[std::size_t(f)] == 0xffff || figs_.y[std::size_t(f)] == 0xffff)
                    figs_.active[std::size_t(f)] = 0;
                if (figs_.active[std::size_t(f)] != 0) over = false;
            }
            if (!over && combat_.enemy_people_ran && kind == 0 && !enemy_people_run(keys, over)) return false;
            if (kind == 1 && figs_.active[0] == 0) over = true;
            if (!exit_zone && kind == 2 && figs_.active[0] == 0 && flags_[0x23] == 0) over = true;
            if (exit_zone) over = true;
            if (!combat_.fight_on) over = true;
        }
        if (raid_ && !raid_lines(over, keys)) return false;
        // With the computer in control a waiting key hands the party back
        // to the player (273d:002f, no demo playing), and is thrown away:
        // the orders menu comes next round.
        if (computer_) {
            const auto waiting = keys.poll();
            if (!waiting) {
                place_open_ = true;  // the keys ran out between rounds
                poll_open_ = true;
                return false;
            }
            if (*waiting) computer_ = false;
        }
    }

    // After the loop (203b:1040): window 3 is cleared.
    text_.select_window(3);
    clear_window();
    if (kind == 1 && combat_.fight_on && raid_) {
        // Out by the way out: window 3 again, "...", "Press a key."
        // (203b:13e2, 1000:2a4f).
        if (fled_how == 2) {
            text_.select_window(3);
            clear_window();
            print_id("fight.eluded_2");
            if (!press_key(keys)) return false;
        }
    } else if (kind == 1 && combat_.fight_on) {
        // 24d3:17c6 / 17ea and a key.
        open_message_box();
        print_id("fight.return_to_base");
        if (!read_key(keys)) {
            place_open_ = true;
            return false;
        }
    } else if (kind == 0 || kind == -1 || kind == 3) {
        // 203b:1401 / 16a1: kinds below 1 and kind 3.
        return encounter_end(fled_how == 0, keys);
    } else if (kind == 2 && combat_.fight_on && !raid_ && !exit_zone) {
        // 203b:1757..1790: the Arena's verdict, flag 21 = won, and a key.
        open_message_box();
        flags_[0x21] = 1;
        if (mechs_[0][0] != 0xff) {
            print_id("fight.arena_won");
        } else {
            print_id("fight.arena_lost");
            flags_[0x21] = 0;
        }
        if (!read_key(keys)) {
            place_open_ = true;
            return false;
        }
    } else if (kind != 1 && kind != 2) {
        log_.push_back("the ending of a fight of kind " + std::to_string(kind) + ": not rebuilt");
        fight_unfinished_ = true;
        return false;
    }
    fight_ends();
    return true;
}

// BTECH.EXE 203b:1135..12a1, part of 203b:000a (docs/combat.md 2.10)
// An enemy person planned against a 'Mech this round: with an enemy 'Mech
// still up, the people too far from their first target leave; with none,
// a coin toss ends the fight.
// BTECH.EXE 203b:08a4..0cdb, in the fight loop (Ghidra FUN_203b_0418)
// The double agent, found out, acts after the round (combat.md 2.9).
bool Town::double_agent_acts(KeySource& keys) {
    text_.select_window(6);
    clear_window();   // 2656:0388
    text_.frame(0);   // 2656:0004(0)
    const auto character_name = [this](int c) {
        return "character." + std::to_string(characters_[std::size_t(c)][character::kName]);
    };
    const int agent = std::int8_t(flags_[0x25]);
    const int m = characters_[std::size_t(agent)][character::kMech];
    if (m == character::kOnFoot) {
        const std::size_t f = std::size_t(4 + agent);
        print_id("fight.agent.0");
        print_id(character_name(agent));
        for (const char* id : {"fight.agent.1", "fight.agent.2", "fight.agent.3", "fight.agent.4"}) print_id(id);
        marks_.add(0x7e, figs_.x[f], figs_.y[f]);
        figs_.x[f] = figs_.y[f] = 0xffff;
    } else {
        auto& rec = mechs_[std::size_t(m)];
        const std::size_t mf = std::size_t(m);
        if (rec[mech::kPilot] == agent) {
            const int other = rec[mech::kSecond];
            print_id(character_name(agent));
            print_id("fight.agent.5");
            if (other != 0xff && characters_[std::size_t(other)][character::kSkills + 4] != 0) {
                // The companion throws him out and takes the controls.
                print_id(character_name(other));
                for (const char* id : {"fight.agent.10", "fight.agent.11", "fight.agent.12"}) print_id(id);
                rec[mech::kPilot] = std::uint8_t(other);
                rec[mech::kSecond] = 0xff;
                marks_.add(0x7e, figs_.x[mf], figs_.y[mf]);
            } else {
                if (other == 0xff) {
                    print_id("fight.agent.6");
                    print_id("fight.agent.7");
                } else {
                    // The companion, who cannot pilot, gets out on foot.
                    print_id(character_name(other));
                    print_id("fight.agent.8");
                    print_id("fight.agent.9");
                    characters_[std::size_t(other)][character::kMech] = character::kOnFoot;
                    figs_.active[std::size_t(4 + other)] = 1;
                    figs_.x[std::size_t(4 + other)] = figs_.x[mf];
                    figs_.y[std::size_t(4 + other)] = figs_.y[mf];
                }
                // The 'Mech is lost.
                rec[0] = 0xff;
                marks_.add(figs_.sprite_base[mf] != 0 ? 0x81 : 0x80, figs_.x[mf], figs_.y[mf]);
                figs_.x[mf] = figs_.y[mf] = 0xffff;
                figs_.active[mf] = 0;
            }
        } else {
            // A passenger: the pilot throws him out.
            rec[mech::kSecond] = 0xff;
            marks_.add(0x7e, figs_.x[mf], figs_.y[mf]);
            print_id(character_name(agent));
            print_id("fight.agent.13");
            print_id("fight.agent.14");
            print_id(character_name(rec[mech::kPilot]));
            print_id("fight.agent.15");
            print_id("fight.agent.16");
        }
    }
    if (!read_key(keys)) {  // 273d:086a, 273d:0259
        place_open_ = true;
        return false;
    }
    characters_[std::size_t(agent)][character::kName] = character::kEmpty;
    figs_.active[std::size_t(4 + agent)] = 0;
    flags_[0x27] = 0;
    flags_[0x24] = 0x7f;
    return true;
}

bool Town::enemy_people_run(KeySource& keys, bool& over) {
    bool mech_up = false;
    for (int f = 12; f < 16; ++f)
        if (figs_.active[std::size_t(f)] != 0) mech_up = true;
    if (mech_up) {
        const formats::MapPosition at = position_;
        combat_.enemy_people_ran = false;
        bool fled = false;
        Combat combat = combat_rules();
        for (int f = 16; f < 24; ++f) {
            if (figs_.active[std::size_t(f)] == 0) continue;
            position_ = {figs_.x[std::size_t(f)], figs_.y[std::size_t(f)]};
            const int target = combat_.targets[std::size_t(f)][0];
            // cbw: a byte past 0x7f indexes before the position arrays.
            // ff is figure -1 (F:4002, F:4034; docs/sound.md), bug 21.
            std::uint16_t tx = 0, ty = 0;
            if (target < 0x80) {
                tx = figs_.x[std::size_t(target)];
                ty = figs_.y[std::size_t(target)];
            } else if (target == 0xff && !fixes_.fix_flee_target) {
                tx = sound_.minus_one_x();
                ty = sound_.minus_one_y();
            } else {
                if (target != 0xff) {
                    char byte[8];
                    std::snprintf(byte, sizeof byte, "%02x", target);
                    log_.push_back(std::string("an enemy person's target byte ") + byte +
                                   " reads before the position arrays: not fled");
                }
                continue;
            }
            if (combat.distance(tx, ty) > 25) {
                fled = true;
                figs_.active[std::size_t(f)] = 0;
                figs_.x[std::size_t(f)] = figs_.y[std::size_t(f)] = 0xffff;
            }
        }
        // The message and its key come before the party position is put
        // back: meanwhile it is the last active person's.
        if (fled) {
            open_message_box();  // 24d3:17c6 / 17ea, 273d:0259
            print_id("fight.some_fled");
            if (!read_key(keys)) {
                place_open_ = true;
                return false;
            }
        }
        position_ = at;
        return true;
    }
    combat_.enemy_people_ran = (random_.next() & 1) != 0;
    if (combat_.enemy_people_ran) {
        over = true;
        open_message_box();
        print_id("fight.all_fled");
        if (!read_key(keys)) {
            place_open_ = true;
            return false;
        }
    }
    return true;
}

// BTECH.EXE 203b:141a..1473, every fight's end (docs/combat.md 2.13): the
// townspeople are put back; F:396c of figures 0-7 cleared; the
// townspeople's frame 0x10.
void Town::fight_ends() {
    std::array<std::int8_t, 8> headings{};
    for (int i = 0; i < 8; ++i) headings[std::size_t(i)] = figs_.heading[std::size_t(16 + i)];
    people_.back_from_fight(headings);
    townspeople_away_ = false;
    for (int i = 0; i < 8; ++i) {
        figs_.x[std::size_t(16 + i)] = people_.walkers()[std::size_t(i)].at.x;
        figs_.y[std::size_t(16 + i)] = people_.walkers()[std::size_t(i)].at.y;
        // F:397c + i and F:396c + i: figures 16-23 and 0-7 face no way.
        animation_.set_direction(16 + i, -1);
        animation_.set_direction(i, -1);
        animation_.set_frame(16 + i, 0x10);
    }
}

}  // namespace chr::game
