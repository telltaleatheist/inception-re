#include "game/figures.h"

#include "game/map_view.h"

namespace chr::game {

namespace {

// The walking scripts (37e8:0270..02ef) were part of BTECH.EXE; they are
// now imported into tables.json (formats/tables.h, walking_scripts). A byte
// 0-127 is a frame; 0xfd n records direction n; 0xfe n jumps back n bytes
// (counted from the n); 0xff steps back one byte (a frame held for ever).
// One script per direction, for the 'Mechs (0x270 + 8d) and for the
// characters (0x2b0 + 8d). The shot's poses (DG:3ef0..3ff7), the hit
// reaction (DG:2e3c) and the explosion (DG:41d8) are scripts of the same
// kind in the data segment (tables.json too); their offsets do not overlap
// the walking scripts', so an offset names a script either way.
constexpr int kScriptBase = 0x270;
constexpr int character_script(int direction) { return 0x2b0 + 8 * direction; }  // DGROUP:027a

}  // namespace

FigureAnimation::FigureAnimation(const formats::Tables& tables) : tables_(&tables) {
    for (int f = 0; f < kFigures; ++f) {
        // DGROUP:01f6's first pointers: 37e8:0290 for the 'Mechs, 02d0 for the rest.
        pos_[f] = (f < 4 || (f >= 12 && f < 16)) ? mech_script(4) : character_script(4);
        direction_[f] = -1;                              // F:396c = 0xff
        frame_[f] = (f < 4 || (f >= 12 && f < 16)) ? 0 : 0x10;  // F:409a
    }
}

int FigureAnimation::script_byte(int address) const {
    const auto in = [address](int base, const auto& table) {
        return address >= base && address < base + int(table.size());
    };
    const auto& t = *tables_;
    if (in(kScriptBase, t.walking_scripts)) return t.walking_scripts[std::size_t(address - kScriptBase)];
    if (in(formats::Tables::kPoseScriptBase, t.pose_scripts))
        return t.pose_scripts[std::size_t(address - formats::Tables::kPoseScriptBase)];
    if (in(formats::Tables::kReactionScript, t.reaction_script))
        return t.reaction_script[std::size_t(address - formats::Tables::kReactionScript)];
    if (in(formats::Tables::kExplosionScript, t.explosion_script))
        return t.explosion_script[std::size_t(address - formats::Tables::kExplosionScript)];
    if (in(formats::Tables::kSpectatorScript, t.spectator_script))
        return t.spectator_script[std::size_t(address - formats::Tables::kSpectatorScript)];
    return 0xff;  // no script holds it: held, as at a script's end
}

int FigureAnimation::run(int& pos, int* direction) const {
    for (;;) {
        const auto b = std::int8_t(script_byte(pos++));
        if (b >= 0) return b;
        if (b == -3) {
            const int d = script_byte(pos++);
            if (direction) *direction = d;
        } else if (b == -2) {
            pos -= script_byte(pos);
        } else if (b == -1) {
            // The original steps back onto the ff and reads it again, for
            // ever. Its callers stop at an ff (22e8:12c7) or never meet one.
            pos -= 1;
            return -1;
        }
    }
}

int FigureAnimation::step(int f) {
    const int frame = run(pos_[std::size_t(f)], &direction_[std::size_t(f)]);
    return frame < 0 ? frame_[std::size_t(f)] : frame;
}

void FigureAnimation::start_script(int f, int address) { pos_[std::size_t(f)] = address; }

void FigureAnimation::walk(int f, int direction) {
    if (direction_[f] != direction) pos_[f] = character_script(direction);
    frame_[f] = step(f);
}

void FigureAnimation::key(int k, const std::array<bool, 4>& mechs) {
    for (int d = 0; d < 8; ++d) {
        // DGROUP:0160, the walking keys (imported: tables.json, walking_keys).
        if (tables_->walking_keys[std::size_t(d)] != k) continue;
        for (int f = 0; f < 4; ++f) {
            if (!mechs[std::size_t(f)]) continue;
            if (direction_[std::size_t(f)] != d) pos_[std::size_t(f)] = mech_script(d);
            frame_[std::size_t(f)] = step(f);
        }
        for (int f = 4; f < 12; ++f) walk(f, d);
        return;
    }
}

std::vector<Figure> party_figures(const Terrain& terrain, formats::MapPosition position,
                                  const Party& party, const FigureAnimation& animation,
                                  const formats::Tables& tables, const std::array<int, 8>& sprite_base,
                                  bool sinks) {
    std::vector<Figure> figures;
    const int hx = position.half_cell_x(), hy = position.half_cell_y();
    const int parity = (position.y & 1) * 2 + (position.x & 1);
    int k = 0;
    for (int member = 0; member < 8; ++member) {
        if (!party.present[member]) continue;
        Figure f;
        f.sprite = sprite(sprite_base[std::size_t(member)] + animation.character_frame(member));
        // DGROUP:02ae/02be put the k-th figure at the k-th formation offset
        // (DGROUP:0464/047c, imported: formation_x/y), one half-cell right of it.
        f.half_x = hx + tables.formation_x[std::size_t(k)] + 1;
        f.half_y = hy + tables.formation_y[std::size_t(k)];
        // DGROUP:02fe/030e/031e pick the cell under it in the 24x24-cell
        // window at 2c6c:07ad: the one holding that half-cell.
        if (const auto cell = terrain.cell(position, f.half_x >> 1, f.half_y >> 1); cell && sinks) {
            const int tile = *cell;
            // DGROUP:0332 (imported: party_water): the water quarter bits.
            if (tile < 0xf6 && (tile & tables.party_water[std::size_t(k)][std::size_t(parity)]) != 0 && (tile & 0xf0) < 0x30)
                f.sink = (tile & 0xf0) == 0x20 ? 4 : 2;
        }
        figures.push_back(f);
        ++k;
    }
    return figures;
}

}  // namespace chr::game
