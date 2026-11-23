#include "game/movement.h"

#include "game/map_view.h"

namespace chr::game {

using formats::MapPosition;

std::optional<Step> step_for_key(int k) {
    Step s;
    if (k == key::kUp || k == key::kPageUp || k == key::kHome) s.dy = -1;
    if (k == key::kDown || k == key::kPageDown || k == key::kEnd) s.dy = 1;
    if (k == key::kRight || k == key::kPageUp || k == key::kPageDown) s.dx = 1;
    if (k == key::kLeft || k == key::kHome || k == key::kEnd) s.dx = -1;
    if (s.dx == 0 && s.dy == 0) return std::nullopt;
    return s;
}

// BTECH.EXE 2656:0d1d (Ghidra FUN_2656_0d1d)
// The keys a menu (and the program's other key loops) take as arrows: the
// keypad digits, the letters round S (Q W E / A D / Z X C, either case) and a
// few stray symbols. Every other key comes back as it is.
int arrow_key(int k) {
    switch (k) {
    case '1': case 'Z': case 'z': return key::kEnd;
    case '2': case 'X': case 'x': case '`': return key::kDown;
    case '3': case 'C': case 'c': return key::kPageDown;
    case '4': case 'A': case 'a': case '|': return key::kLeft;
    case '6': case 'D': case 'd': case 0x0c: return key::kRight;
    case '7': case 'Q': case 'q': case '\\': return key::kHome;
    case '8': case 'W': case 'w': case '~': return key::kUp;
    case '9': case 'E': case 'e': return key::kPageUp;
    default: return k;
    }
}

int blocking_tile(int map_number) { return map_number == 14 ? 0x21 : 0x55; }

namespace {

// BTECH.EXE 287f:158c / 287f:163b / 287f:16e3 / 287f:17c5 (Ghidra FUN_287f_158c etc.)
// One half-cell up, down, left or right. Past the edge of a 64x64-cell area
// the position moves into the next area (the high byte's area number) and the
// program shifts its area grids and loads the maps there; at the edge of the
// 16x16-area world it does not move. An axis the step does not move is left
// as it is (1000:218f calls them only for -1 and 1), even a place off the
// map such as y ffff.
std::uint16_t move_x(std::uint16_t x, int dx) {
    if (dx == 0) return x;
    const int low = (x & 0xff) + dx, area = x >> 8;
    if (low < 0) return area == 0 ? x : std::uint16_t((area - 1) << 8 | 0x7f);
    if (low > 0x7f) return area == 0x0f ? x : std::uint16_t((area + 1) << 8);
    return std::uint16_t((x & 0xff00) | low);
}
std::uint16_t move_y(std::uint16_t y, int dy) {
    if (dy == 0) return y;
    const int low = (y & 0xff) + dy, area = y >> 8;
    if (low < 0) return area == 0 ? y : std::uint16_t((area - 0x10) << 8 | 0x7f);
    if (low > 0x7f) return area == 0xf0 ? y : std::uint16_t((area + 0x10) << 8);
    return std::uint16_t((y & 0xff00) | low);
}

}  // namespace

StepResult take_step(const formats::MtpMap& map, int map_number, const MapPosition& from,
                     const Party& party, Step step, EntranceQuestion& question,
                     const formats::Tables& tables, const Terrain& terrain, const MechParty& mechs,
                     const MapPosition& window_at, int blocking) {
    StepResult result;
    // A place entered on the way can move the party: what is read after the
    // question is read again, as the original reads its memory.
    auto finish = [&](StepOutcome outcome) {
        result.outcome = outcome;
        result.position = from;
        return result;
    };
    // The window is the one in the original's memory: as 287f:1314 last
    // built it (or the fight or a map load left it), with the corner cell
    // (P:09ed) of that place; the half-cells are the party's now. Both are
    // read as each cell is, so a place entered on the way, which builds its
    // own, is seen. A cell the rebuild does not hold blocks.
    auto tile = [&](int index) {
        index += view_corner_index(window_at);
        if (index >= 0 && index < Terrain::kWindowCells * Terrain::kWindowCells)
            return int(terrain.held_window(window_at).cells[std::size_t(index)]);
        return terrain.window_cell(window_at, index).value_or(0xff);
    };

    // Crossing into the next 8x8 block (16 half-cells) on either axis looks at
    // that block first: the block in the direction of the step, the diagonal
    // one when both axes cross. Its value in the window (P:07a4, mask OR
    // entry) of 0x0f is a terrain block of type 0 whose four neighbours are
    // all type 0: deep water. (DGROUP:04a0 turns the step's edges, up 8,
    // down 4, left 2, right 1, into the block's index: the 3x3 order.)
    int bdx = 0, bdy = 0;
    if (step.dy < 0 && (from.y & 0xf) == 0) bdy = -1;
    if (step.dy > 0 && (from.y & 0xf) == 0xf) bdy = 1;
    if (step.dx < 0 && (from.x & 0xf) == 0) bdx = -1;
    if (step.dx > 0 && (from.x & 0xf) == 0xf) bdx = 1;
    if ((bdx != 0 || bdy != 0) && terrain.held_window(window_at).blocks[std::size_t((1 + bdy) * 3 + 1 + bdx)] == 0x0f)
        return finish(StepOutcome::TooDeep);

    // Each character in turn: first an entrance under its new position, then
    // the tile under its feet, read from the window as the 'Mechs' are.
    bool asked = false;  // F:d55c: one question per turn
    const int limit = blocking >= 0 ? blocking : blocking_tile(map_number);
    // The loop runs over the figures 4-11 that are active: 1000:0bd8 gives
    // the k-th character on foot figure 4 + k at the k-th formation offset,
    // so a character in a 'Mech leaves no gap. The entrance is matched at
    // the figure's position (F:400c / F:403e + 2k) plus the step.
    int k = 0;
    for (int i = 0; i < 8; ++i) {
        if (!party.present[i]) continue;
        const int slot = k++;
        const int fx = tables.formation_x[std::size_t(slot)] + step.dx, fy = tables.formation_y[std::size_t(slot)] + step.dy;
        // The tile is read first (1000:1d9e), the entrances after: a place
        // entered here leaves the tile as it was before the place.
        const int under = tile(window_offset(from, fx, fy));
        if (!asked && map_number != 14) {  // ('Mech mode, map 14, has tile events instead)
            const std::uint16_t x = std::uint16_t(from.x + fx), y = std::uint16_t(from.y + fy);
            for (int e = 0; e < 12; ++e) {
                if (map.entrances[e].x != x || map.entrances[e].y != y) continue;
                asked = true;
                result.offered_place = e;
                result.entered = question.ask(e, false);
                break;
            }
        }
        if (under >= limit) {
            if (map_number == 14) {
                result.event_tile = under;
                result.event_at = {std::uint16_t(from.x + fx), std::uint16_t(from.y + fy)};
            }
            return finish(StepOutcome::Blocked);
        }
    }

    // The 'Mechs: two cells each, the one under the half-cell it is drawn
    // on and the one beside it on the side the half-cell leans to. A cell
    // the rebuild does not hold blocks. An entrance is matched on the
    // 'Mech's row at its x or one either side, and is asked about before
    // the 'Mech is found blocked.
    for (int m = 0; m < 4; ++m) {
        if (!mechs.present[std::size_t(m)]) continue;
        const int dx = tables.mech_step_x[std::size_t(m)] + step.dx, dy = tables.mech_step_y[std::size_t(m)] + step.dy;
        int index = window_offset(from, dx, dy);
        bool blocked = tile(index) >= limit;
        if (!blocked) {
            index += (((dx + 0x1a) ^ from.x) & 1) == 0 ? 1 : -1;
            blocked = tile(index) >= limit;
        }
        if (!asked) {
            const auto& at = mechs.at[std::size_t(m)];
            const std::uint16_t x = std::uint16_t(at.x + step.dx), y = std::uint16_t(at.y + step.dy);
            for (int e = 0; e < 12; ++e) {
                const auto& place = map.entrances[std::size_t(e)];
                if (place.y != y || (place.x != x && place.x != std::uint16_t(x - 1) && place.x != std::uint16_t(x + 1)))
                    continue;
                asked = true;
                result.offered_place = e;
                result.entered = question.ask(e, true);
                break;
            }
        }
            if (blocked) {
            return finish(StepOutcome::Blocked);
        }
    }

    // 1000:218f moves up or down first, then left or right.
    finish(StepOutcome::Moved);
    result.position.y = move_y(from.y, step.dy);
    result.position.x = move_x(from.x, step.dx);
    return result;
}

}  // namespace chr::game
