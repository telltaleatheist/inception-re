#include "game/town.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "game/tile_animation.h"
#include "game/world.h"

namespace chr::game {

void ScriptKeys::apply_pokes() {
    while (!items_.empty() && items_.front().is_poke()) {
        const auto item = items_.front();
        items_.pop_front();
        if (!poke_) throw std::invalid_argument("a mech: or char: item with nothing to poke");
        // record -1: the C-Bills (cbills:), the value the amount; -2: the
        // generator's state (random:), the value its three bytes; -3: a
        // 'Mech's heat (heat:), the byte its record; -4: the party's place
        // (pos:), the byte x and the value y.
        poke_(item.poke_character,
              item.poke_cbills ? -1 : item.poke_random ? -2 : item.poke_heat ? -3 : item.poke_position ? -4 : item.poke_record,
              item.poke_byte, item.poke_value);
    }
}

std::optional<ScriptItem> ScriptKeys::next_item() {
    apply_pokes();
    if (items_.empty()) return std::nullopt;
    const auto item = items_.front();
    items_.pop_front();
    return item;
}

std::optional<int> ScriptKeys::next() {
    // Pokes and idle passes in any order before the key (a cbills: poke has
    // idle passes of its own after it).
    for (;;) {
        apply_pokes();
        if (items_.empty() || items_.front().idle == 0) break;
        items_.pop_front();
    }
    const auto item = next_item();
    if (!item) return std::nullopt;
    if (item->is_set()) throw std::invalid_argument("a set: item where the game waits for a key");
    return item->key;
}

std::optional<bool> ScriptKeys::poll() {
    apply_pokes();
    if (items_.empty()) return std::nullopt;
    auto& item = items_.front();
    if (item.idle > 0) {
        if (--item.idle == 0) items_.pop_front();
        return false;
    }
    if (item.is_set()) throw std::invalid_argument("a set: item where the game asks for a key");
    items_.pop_front();
    return true;
}

std::optional<int> ScriptKeys::waiting_key() {
    apply_pokes();
    if (items_.empty()) return std::nullopt;
    auto& item = items_.front();
    if (item.idle > 0) {
        if (--item.idle == 0) items_.pop_front();
        return kNoKey;
    }
    if (item.is_set()) throw std::invalid_argument("a set: item where the game asks for a key");
    const int k = item.key;
    items_.pop_front();
    return k;
}

std::deque<ScriptItem> parse_key_script(const std::string& script) {
    std::deque<ScriptItem> keys;
    std::string token;
    auto add = [&](const std::string& t) {
        if (t.empty()) return;
        if (t.rfind("set:", 0) == 0) {
            const auto eq = t.find('=');
            int f = -1, v = -1;
            try {
                if (eq != std::string::npos) {
                    f = std::stoi(t.substr(4, eq - 4), nullptr, 16);
                    v = std::stoi(t.substr(eq + 1), nullptr, 16);
                }
            } catch (const std::exception&) {
            }
            if (f < 0 || f >= kFlagCount || v < 0 || v > 0xff)
                throw std::invalid_argument("bad flag setting \"" + t + "\" (use set:NN=VV, hex)");
            if (f == flag::kQuizLock)
                throw std::invalid_argument("\"" + t + "\": flag 14 decides whether the training quiz runs; not settable");
            keys.push_back({0, 0, f, v});
            return;
        }
        if (t.rfind("cbills:", 0) == 0) {
            // cbills:VVVV: the C-Bills (F:d370), hex, at most 0xffff.
            int v = -1;
            try {
                std::size_t used = 0;
                v = std::stoi(t.substr(7), &used, 16);
                if (used != t.size() - 7) v = -1;
            } catch (const std::exception&) {
            }
            if (v < 0 || v > 0xffff) throw std::invalid_argument("bad C-Bills poke \"" + t + "\" (use cbills:VVVV, hex)");
            ScriptItem item;
            item.poke_cbills = true;
            item.poke_record = 0;
            item.poke_value = v;
            keys.push_back(item);
            return;
        }
        if (t.rfind("random:", 0) == 0) {
            // random:AABBCC: the generator's state (DG:4fc0-4fc2), hex, six digits.
            int v = -1;
            try {
                std::size_t used = 0;
                if (t.size() == 13) v = std::stoi(t.substr(7), &used, 16);
                if (used != 6) v = -1;
            } catch (const std::exception&) {
            }
            if (v < 0) throw std::invalid_argument("bad random poke \"" + t + "\" (use random:AABBCC, hex)");
            ScriptItem item;
            item.poke_random = true;
            item.poke_record = 0;
            item.poke_value = v;
            keys.push_back(item);
            return;
        }
        if (t.rfind("heat:", 0) == 0) {
            // heat:M=VV: 'Mech record M's heat (F:006e + M), hex.
            int m = -1, v = -1;
            const auto eq = t.find('=');
            try {
                std::size_t used = 0;
                if (eq == 6) m = std::stoi(t.substr(5, 1), &used, 16);
                if (used != 1) m = -1;
                v = std::stoi(t.substr(eq + 1), &used, 16);
                if (used != t.size() - eq - 1) v = -1;
            } catch (const std::exception&) {
            }
            if (m < 0 || m > 7 || v < 0 || v > 0xff)
                throw std::invalid_argument("bad heat poke \"" + t + "\" (use heat:M=VV, hex, M 0-7)");
            ScriptItem item;
            item.poke_heat = true;
            item.poke_record = 0;
            item.poke_byte = m;
            item.poke_value = v;
            keys.push_back(item);
            return;
        }
        if (t.rfind("pos:", 0) == 0) {
            // pos:XXXX:YYYY: the party's place (P:a44b, P:a44d), hex.
            unsigned x = 0, y = 0;
            char tail = 0;
            if (t.size() != 13 || std::sscanf(t.c_str() + 4, "%4x:%4x%c", &x, &y, &tail) != 2)
                throw std::invalid_argument("bad place poke \"" + t + "\" (use pos:XXXX:YYYY, hex)");
            ScriptItem item;
            item.poke_position = true;
            item.poke_record = 0;
            item.poke_byte = int(x);
            item.poke_value = int(y);
            keys.push_back(item);
            return;
        }
        const bool character = t.rfind("char:", 0) == 0;
        if (t.rfind("mech:", 0) == 0 || character) {
            // mech: the eight 'Mech records, bytes below 0x7d; char: the 16
            // character records, bytes below 0x11. Data only, nothing else.
            const int records = character ? 16 : 8, size = character ? 0x11 : 0x7d;
            int r = -1, b = -1, v = -1;
            const auto colon = t.find(':', 5), eq = t.find('=');
            try {
                if (colon != std::string::npos && eq != std::string::npos && eq > colon) {
                    r = std::stoi(t.substr(5, colon - 5), nullptr, 16);
                    b = std::stoi(t.substr(colon + 1, eq - colon - 1), nullptr, 16);
                    v = std::stoi(t.substr(eq + 1), nullptr, 16);
                }
            } catch (const std::exception&) {
            }
            if (r < 0 || r >= records || b < 0 || b >= size || v < 0 || v > 0xff)
                throw std::invalid_argument("bad record poke \"" + t + "\" (use mech:R:BB=VV or char:R:BB=VV, hex)");
            ScriptItem item;
            item.poke_character = character;
            item.poke_record = r;
            item.poke_byte = b;
            item.poke_value = v;
            keys.push_back(item);
            return;
        }
        if (t.rfind("idle:", 0) == 0) {
            int n = 0;
            try {
                n = std::stoi(t.substr(5));
            } catch (const std::exception&) {
                throw std::invalid_argument("bad count in \"" + t + "\"");
            }
            if (n < 0) throw std::invalid_argument("bad count in \"" + t + "\"");
            if (n > 0) keys.push_back({0, n});
            return;
        }
        std::string name = t;
        int count = 1;
        if (const auto star = t.find('*'); star != std::string::npos) {
            name = t.substr(0, star);
            try {
                count = std::stoi(t.substr(star + 1));
            } catch (const std::exception&) {
                throw std::invalid_argument("bad repeat in \"" + t + "\"");
            }
        }
        int k = 0;
        if (name == "up") k = key::kUp;
        else if (name == "down") k = key::kDown;
        else if (name == "left") k = key::kLeft;
        else if (name == "right") k = key::kRight;
        else if (name == "home") k = key::kHome;
        else if (name == "end") k = key::kEnd;
        else if (name == "pgup") k = key::kPageUp;
        else if (name == "pgdn") k = key::kPageDown;
        else if (name == "space") k = key::kSpace;
        else if (name == "enter") k = key::kEnter;
        else if (name == "esc") k = key::kEscape;
        else if (name == "backspace") k = key::kBackspace;
        else if (name.size() == 1) k = static_cast<unsigned char>(name[0]);
        else throw std::invalid_argument("unknown key \"" + name + "\"");
        for (int i = 0; i < count; ++i) keys.push_back({k, 0});
    };
    for (const char c : script) {
        if (c == ' ' || c == ',') {
            add(token);
            token.clear();
        } else {
            token += c;
        }
    }
    add(token);
    return keys;
}

Town::Town(const formats::MtpMap& map, int map_number, formats::MapPosition start,
           const TextProvider& texts, const formats::Tables& tables)
    : map_(map), map_number_(map_number), tile_sheet_(tile_sheet_for_map(map_number)),
      position_(start), texts_(texts), tables_(tables) {
    window_at_ = start;
    // F:d562 (figure 4's sprite base): Jason's people from the new-game
    // setup (1000:4f0f); the other characters' stay 0, MECHSHAP.CMP's red row.
    figs_.sprite_base[4] = 0x96;
    load_terrain();
    people_ = Townspeople::place(map_, map_number_, random_, tables_);
    record_party_places();
}

void Town::load_terrain() {
    terrain_.generate(((position_.y & 0xf000) | (position_.x & 0x0f00)) >> 8);
    terrain_.place_map(map_, 4);
    terrain_.link_blocks();
}

// BTECH.EXE 1000:0bc9..0c7d (the end of 1000:051b)
// After drawing, each character drawn is recorded where it stands: the
// party's position plus the k-th formation offset. The townspeople's step
// refuses these places until the next drawing.
void Town::record_party_places() {
    // 1000:0537..0561: no figure 0-11 is active until drawn.
    for (int f = 0; f < 12; ++f) figs_.active[std::size_t(f)] = 0;
    party_places_ = {};
    int k = 0;
    for (int member = 0; member < 8; ++member) {
        if (!party().present[member] || !on_foot(member)) continue;
        const formats::MapPosition at{move_x(position_.x, tables_.formation_x[std::size_t(k)]),
                                      move_y(position_.y, tables_.formation_y[std::size_t(k)])};
        party_places_[std::size_t(k)] = at;
        figs_.x[std::size_t(4 + k)] = at.x;
        figs_.y[std::size_t(4 + k)] = at.y;
        figs_.active[std::size_t(4 + k)] = 1;
        ++k;
    }
    // 1000:0c93..: each 'Mech of records 0-3 as figure k at the k-th 'Mech
    // place.
    k = 0;
    for (int m = 0; m < 4; ++m) {
        if (mechs_[std::size_t(m)][0] == 0xff) continue;
        figs_.x[std::size_t(k)] = move_x(position_.x, tables_.mech_formation_x[std::size_t(k)]);
        figs_.y[std::size_t(k)] = move_y(position_.y, tables_.mech_formation_y[std::size_t(k)]);
        figs_.active[std::size_t(k)] = 1;
        ++k;
    }
}

MapView Town::view() const {
    if (frozen_) return frozen_->view;
    return live_view();
}

MapView Town::live_view() const {
    auto view = view_of(terrain_, tile_sheet_, position_);
    if (tiles_animate(map_number_))
        for (auto& cell : view.cells)
            if (const auto frame = animated_tile(std::uint8_t(cell.tile.index), phase_, tables_)) cell.tile = *frame;
    return view;
}

// BTECH.EXE 1000:2a93 (Ghidra FUN_1000_2a93)
// The map's marks (wrecks, bodies, fires: docs/combat.md 3.13) in view, in
// ring order, each as its code's sprite at its own place, placed as the
// people are. A fire drawn changes to its other frame (code 0x7c <-> 0x7d)
// for the next drawing. 1000:051b skips it on a map with tile events (F:d346).
void Town::draw_marks() {
    drawn_marks_.clear();
    if (flags_[flag::kTileEvents] != 0) return;
    const std::uint16_t px = position_.x, py = position_.y;
    const int party_area = ((px | py) >> 8) & 0xff;
    for (auto& m : marks_.marks) {
        const std::uint16_t x = std::uint16_t(m.x | ((m.area & 0x0f) << 8));
        const std::uint16_t y = std::uint16_t(m.y | ((m.area & 0xf0) << 8));
        const int col = std::int16_t(std::uint16_t(x - px + 0x1a));
        const int row = std::int16_t(std::uint16_t(y - py + 0x0c));
        const bool off_x = (party_area & 0x0f) == (m.area & 0x0f) && (col < 0x0d || col > 0x27);
        const bool off_y = (party_area & 0xf0) == (m.area & 0xf0) && (row < 0 || row > 0x18);
        if (col < -0x73 || col > 0xa7 || row < -0x0f80 || row > 0x0f98 || off_x || off_y) continue;
        Figure f;
        f.sprite = sprite(m.code);
        f.half_x = folded_half_x(col, px) + 1;
        f.half_y = folded_half_y(row, py);
        drawn_marks_.push_back(f);
        if ((m.code & 0x7e) == 0x7c) m.code ^= 1;
    }
}

std::vector<Figure> Town::figures() const {
    if (frozen_) return frozen_->figures;
    auto all = drawn_marks_;
    // Characters in a 'Mech are drawn as their 'Mech, below.
    Party walking = party();
    for (int i = 0; i < 8; ++i) walking.present[std::size_t(i)] = walking.present[std::size_t(i)] && on_foot(i);
    const auto party = party_figures(terrain_, position_, walking, animation_, tables_, people_sprite_base(),
                                     flags_[flag::kTileEvents] == 0);
    all.insert(all.end(), party.begin(), party.end());
    const auto people = people_.figures(map_, position_, animation_);
    all.insert(all.end(), people.begin(), people.end());
    int k = 0;
    for (int m = 0; m < 4; ++m)
        if (mechs_[std::size_t(m)][0] != 0xff) all.push_back(party_mech_figure(m, k++));
    return all;
}

// The party box's bars as last drawn, none while the box is cleared.
std::vector<StatBar> Town::bars() const {
    if (!party_box_shown_) return {};  // the party box was cleared (a fight's questions)
    return drawn_bars_;
}

// BTECH.EXE 1000:4aa6 (Ghidra FUN_1000_4aa6)
// Beside each of the first four characters' names (row 14, 16, ...): bars at
// columns 9, 10 and 11. The first bar's top part, its number less a tenth
// of byte 15 (at least 1), is drawn as a warning.
std::vector<StatBar> Town::party_bars() const {
    std::vector<StatBar> bars;
    int line = 0;
    for (int i = 0; i < 8 && line < 4; ++i) {
        if (!party().present[i]) continue;
        const auto& c = characters_[std::size_t(i)];
        const int b = c[character::kB];
        bars.push_back({line, StatBar::Stat::B, b, b - std::max(c[character::kHitPoints] / 10, 1)});
        bars.push_back({line, StatBar::Stat::D, c[character::kD], 0});
        bars.push_back({line, StatBar::Stat::C, c[character::kC], 0});
        ++line;
    }
    return bars;
}

// BTECH.EXE 1000:4cac (Ghidra FUN_1000_4cac)
// The party box: window 3 framed (cleared with `clear`), its heading, each
// member's line and bars (1000:4aa6), the C-Bills; window 4 last.
// `blank_column` blacks out columns 0-12 first, which 4cac itself does not.
void Town::draw_panel(bool clear, bool blank_column) {
    party_box_shown_ = true;
    if (blank_column) text_.fill(0, 0, 13, kTextRows, TextRole::Background);
    text_.select_window(3);
    text_.frame(3);
    if (clear) text_.clear_window();
    text_.put_text(lookup(texts_, "panel.party_heading"), 9, 13, TextRole::Text, TextRole::Background);
    // The first four characters of the party, a line each from row 14, two
    // rows apart, with their bars (bars()).
    int line = 0;
    for (int i = 0; i < 8 && line < 4; ++i) {
        if (!party().present[i]) continue;
        text_.put_text(lookup(texts_, "character." + std::to_string(i)), 1, 14 + 2 * line, TextRole::Text, TextRole::Background);
        ++line;
    }
    drawn_bars_ = party_bars();  // 1000:4d27: 1000:4aa6
    draw_cbills();
    text_.select_window(4);
    text_.frame(4);
}

void Town::open_message_box() {
    text_.select_window(7);
    text_.clear_window();
    text_.frame(0);  // over the orders screen's XORed box too (boxes())
}

std::vector<XorBox> Town::boxes() const {
    std::vector<XorBox> out;
    for (const auto& b : boxes_) {
        bool covered = false;
        for (int r = b.row; r < b.row + b.height; ++r)
            for (int c = b.col; c < b.col + b.width; ++c)
                covered = covered || text_.painted_at(c, r) > boxes_since_;
        if (!covered) {
            out.push_back(b);
            continue;
        }
        for (int r = b.row; r < b.row + b.height; ++r)
            for (int c = b.col; c < b.col + b.width; ++c)
                if (text_.painted_at(c, r) <= boxes_since_) out.push_back({c, r, 1, 1, b.marks});
    }
    return out;
}

void Town::settle_boxes() {
    boxes_ = boxes();
    boxes_since_ = text_.paint_clock();
}

std::optional<bool> Town::yes_no(bool yes_first, KeySource& keys) {
    const TextColour saved_fg = text_.window().fg;
    bool yes = yes_first;
    text_.print(lookup(texts_, yes ? "ui.answer_yes_picked" : "ui.answer_no_picked"));
    for (;;) {
        const auto k = keys.next();
        if (!k) return std::nullopt;
        bool done = false;
        const int up = std::toupper(*k);
        if (*k == key::kSpace || *k == key::kEnter) done = true;
        else if (up == 'Y') yes = done = true;
        else if (up == 'N') { yes = false; done = true; }
        else if (*k == key::kLeft) yes = true;
        else if (*k == key::kRight) yes = false;
        // Every key, even one it ignores, puts the answer line up again.
        text_.cursor_up();
        text_.print(lookup(texts_, yes ? "ui.answer_yes_picked" : "ui.answer_no_picked"));
        if (done) break;
    }
    text_.set_fg(saved_fg);
    return yes;
}

class Town::Question : public EntranceQuestion {
public:
    Question(Town& town, KeySource& keys) : town_(town), keys_(keys) {}
    bool ask(int place, bool mech) override {
        auto& t = town_.text_;
        town_.open_message_box();
        t.print(lookup(town_.texts_, mech ? "walk.mech_enter_question" : "walk.enter_question"));
        t.print(lookup(town_.texts_, place_text_id(town_.map_number_, place)));
        t.print(lookup(town_.texts_, mech ? "walk.mech_enter_question_end" : "walk.enter_question_end"));
        const auto answer = town_.yes_no(true, keys_);
        if (!answer) {
            town_.question_open_ = true;
            return false;
        }
        if (*answer) {
            town_.entered_place_ = place;
            town_.log_.push_back("entered place " + std::to_string(place) + " (" +
                                 lookup(town_.texts_, place_text_id(town_.map_number_, place)) + ")");
            // BTECH.EXE 17dc:0008 runs the place right here, inside the step.
            if (town_.game_dir_) town_.visit_place(place, keys_);
            town_.step_mechs_ = town_.active_mechs();
        }
        return *answer;
    }

private:
    Town& town_;
    KeySource& keys_;
};

void Town::draw_cbills() {
    const int previous = text_.window_number();
    text_.select_window(3);
    text_.set_cursor(0, 9);
    text_.print(lookup(texts_, "panel.cbills_label"));
    // BTECH.EXE 1000:28a2 (Ghidra FUN_1000_28a2)
    text_.set_fg(TextRole::Amount);
    // The amount in decimal, padded with spaces to ten characters so a
    // shorter number covers a longer one.
    std::string amount = std::to_string(money_.cbills);
    if (amount.size() < 10) amount.resize(10, ' ');
    text_.print(amount);
    text_.set_fg(TextRole::Text);
    text_.select_window(previous);
}

// BTECH.EXE 1000:17bb (Ghidra FUN_1000_17bb)
// The party position walked to `to` a cell at a time, up or down first, then
// left or right, each step through 287f:158c / 163b / 16e3 / 17c5, so the
// grids move over at each area edge. Their map loads are only queued (P:09f3):
// the queue is worked through by the next 1000:218f.
void Town::walk_position(formats::MapPosition to) {
    const auto area_of = [](std::uint16_t x, std::uint16_t y) { return ((y >> 8) & 0xf0) | ((x >> 8) & 0x0f); };
    const auto step = [&](int dx, int dy) {
        const formats::MapPosition next{dx ? step_x(position_.x, dx) : position_.x, dy ? step_y(position_.y, dy) : position_.y};
        if (area_of(next.x, next.y) != area_of(position_.x, position_.y))
            terrain_.step_area(dx, dy, area_of(next.x, next.y));
        position_ = next;
    };
    while (to.y < position_.y) step(0, -1);
    while (to.y > position_.y) step(0, 1);
    while (to.x < position_.x) step(-1, 0);
    while (to.x > position_.x) step(1, 0);
}

void Town::run(ScriptKeys& input) {
    if (stage_ != Stage::Town && !run_start_up(input)) {
        start_up_open_ = true;
        return;
    }
    for (const auto& [f, v] : pending_flags_) {
        flags_[std::size_t(f)] = std::uint8_t(v);
        log_.push_back("test aid: flag " + std::to_string(f) + " set");
    }
    pending_flags_.clear();
    // BTECH.EXE 1000:0024: the main loop starts with 1000:48b7(0x0f, 0),
    // whose first act is a random draw (docs/town_loop.md, "Random
    // numbers"); after the start-up's questions and any load they start.
    if (!main_loop_started_) {
        main_loop_started_ = true;
        random_.next();
    }
    while (!stopped()) {
        const auto item = input.next_item();
        if (!item) break;
        if (item->is_set()) {
            flags_[std::size_t(item->set_flag)] = std::uint8_t(item->set_value);
            log_.push_back("test aid: flag " + std::to_string(item->set_flag) + " set");
        } else if (item->idle > 0) {
            // A question asked in a pass waits for a key: the rest of the
            // wait is spent there, not in the main loop.
            question_waiting_ = false;
            for (int i = 0; i < item->idle && !stopped() && !question_waiting_; ++i) idle_pass(&input);
            question_waiting_ = false;
        } else {
            key(item->key, input);
        }
    }
}

void Town::idle_pass(KeySource* keys) {
    if (stopped()) return;
    if (--countdown_ < 0) game_tick(keys);
}

void Town::key(int k, KeySource& more) {
    if (stopped()) return;
    turn(k, more);
    if (stopped()) return;
    // BTECH.EXE 1000:0081: after the step the figures are drawn (1000:051b),
    // which records where the party's characters now stand, before the
    // townspeople move in the tick below.
    record_party_places();
    // BTECH.EXE 1000:00fb: the figures' walking frames move on, then the key
    // half goes straight on to a game tick.
    animation_.key(k, mech_records());
    // BTECH.EXE 1000:0101: Space opens the command menu, before the tick.
    if (k == key::kSpace && !command_menu(more)) return;
    mark_seen();
    game_tick(&more);
}

// BTECH.EXE 1000:010b..01af (in Ghidra FUN_1000_00bb)
// After a key, the seen map gets the party's patch: the bits for x's bits
// 4-6 in the party's row, and the rows above and below it. With flag 31 set
// outside a tile-event map, a whole column of the area instead.
void Town::mark_seen() {
    const int x = position_.x & 0xffff, y = position_.y & 0xffff;
    const int column = ((y & 0xf000) >> 5) + (x >> 8);
    // Past either end the original writes into 'Mech record 7 or the flags;
    // the rebuild leaves those alone and logs it.
    auto put = [this](int i, std::uint8_t bits, bool set) {
        if (i < 0 || i >= kSeenBytes) {
            log_.push_back("seen map: a write outside it skipped");
            return;
        }
        seen_[std::size_t(i)] = set ? bits : std::uint8_t(seen_[std::size_t(i)] | bits);
    };
    if (flags_[flag::kMapper] != 0 && flags_[flag::kTileEvents] == 0) {
        for (int r = 0; r < 0x80; r += 0x10) put(column + r, 0xff, true);
        return;
    }
    const int i = column + (y & 0x70);
    const auto bits = std::uint8_t(tables_.seen_bits[std::size_t((x & 0x70) >> 4)]);
    put(i, bits, false);
    if (y != 0) put(i - 0x10, bits, false);
    if (y < 0xf07f) put(i + 0x10, bits, false);
}

void Town::game_tick(KeySource* keys) {
    // BTECH.EXE 1000:0213..0286: the timers (script flags, docs/scripts.md).
    // Flag 29 counts down to 0.
    auto count_down = [this](int f) {
        if (flags_[std::size_t(f)] != 0) --flags_[std::size_t(f)];
    };
    count_down(0x29);
    // Flag 37 says the word in flags 38-39 counts down. When it reaches 0
    // with the party inside x 0800-0cff, y 6000-afff of the world, 1d43:0c72
    // runs (docs/town_loop.md).
    if (flags_[0x37] != 0) {
        const int word = (flag_word(flags_, 0x38) - 1) & 0xffff;
        flags_[0x38] = std::uint8_t(word);
        flags_[0x39] = std::uint8_t(word >> 8);
        flags_[0x37] = std::uint8_t(flags_[0x38] | flags_[0x39]);
        if (flags_[0x37] == 0 && position_.x >= 0x0800 && position_.x < 0x0d00 && position_.y >= 0x6000 &&
            position_.y < 0xb000)
            arena_wall(true);  // 1d43:0c72(1)
    }
    // Outdoors, a draw with none of flag 24's bits starts a random
    // encounter (docs/combat.md 1.1), unless F:d346.
    const std::uint8_t r = random_.next();
    if (outdoors() && (r & flags_[0x24]) == 0 && flags_[0x3a] == 0) {
        // A tick without keys (an idle pass in the window) has none to give
        // the encounter's questions: it waits there for the next key.
        ScriptKeys none({});
        random_encounter(keys ? *keys : none);
        if (stopped()) return;
    }
    // BTECH.EXE 1000:02b6..02ed: flags 1d, 14, 15 and 16 count down to 0.
    for (const int f : {0x1d, 0x14, 0x15, 0x16}) count_down(f);
    if (money_tick(money_, flags_, outdoors(), random_, tables_)) draw_cbills();
    countdown_ = 10;
    if (tiles_animate(map_number_)) phase_ = (phase_ + 1) % 3;  // 1000:240b
    // Every third tick the townspeople move (1000:24c2).
    if (third_++ == 2) {
        third_ = 0;
        move_townspeople();
    }
    // With the leader killed in a fight (DG:014a is 0) the game is over
    // (1000:04a4).
    if (!combat_.fight_on) {
        ScriptKeys none({});
        game_over(keys ? *keys : none, true);
        return;
    }
    // The map and the figures are drawn again (DGROUP:014a is set in town),
    // over any message box (1000:046e), the window first built again at
    // the party's place (287f:1314 at 1000:0480).
    if (tick_redraws_) {
        frozen_.reset();
        tick_redraws_ = false;
    }
    build_window();
    text_.forget(13, 0, kTextColumns - 13, kTextRows);
    draw_marks();
    record_party_places();
    if (outdoors() && !frozen_) last_drawn_ = Frozen{live_view(), figures()};
    ++ticks_;
    // With the game won (DG:01a8, set by the cache's way out), the same
    // question after the draw (1000:049d).
    if (game_won_) {
        ScriptKeys none({});
        game_over(keys ? *keys : none, false);
    }
}

// BTECH.EXE 1000:04a4..050d, the main loop's game over: the message box;
// when the leader was killed, a text naming him (Jason's record gone, or
// the other) and a key; then "a new game?" with Yes first.
void Town::game_over(KeySource& keys, bool leader_killed) {
    open_message_box();  // 14d3:17c6
    if (leader_killed) {
        print_id(characters_[0][0] == 0xff ? "over.leader.0" : "over.leader.1");
        print_id("over.killed");  // 14d3:17ea
        if (!read_key(keys)) {
            place_open_ = true;
            return;
        }
        clear_window();
    }
    print_id("over.new_game");
    const auto again = yes_no(true, keys);  // 1000:1a13(1)
    if (!again) {
        place_open_ = true;
        return;
    }
    if (*again) {
        new_game();  // 1000:4dc7, then the main loop's key half (1000:002d)
        return;
    }
    log_.push_back("the game is over: quit");
    quit_ = true;  // DG:0152
}

namespace {

// The name of a step's direction, in the order of the keys at DGROUP:0160.
std::string direction_text_id(Step s) {
    static constexpr const char* kNames[3][3] = {
        {"northwest", "north", "northeast"}, {"west", "", "east"}, {"southwest", "south", "southeast"}};
    return std::string("walk.direction.") + kNames[s.dy + 1][s.dx + 1];
}

}  // namespace

MechParty Town::active_mechs() const {
    MechParty mechs;
    for (int m = 0; m < 4; ++m) {
        mechs.present[std::size_t(m)] = figs_.active[std::size_t(m)] != 0;
        mechs.at[std::size_t(m)] = {figs_.x[std::size_t(m)], figs_.y[std::size_t(m)]};
    }
    return mechs;
}

// BTECH.EXE 1000:24c2 (Ghidra FUN_1000_24c2)
void Town::move_townspeople() {
    std::array<formats::MapPosition, 8> mechs;
    for (int m = 0; m < 4; ++m) {
        mechs[std::size_t(m)] = {figs_.x[std::size_t(m)], figs_.y[std::size_t(m)]};
        mechs[std::size_t(4 + m)] = {figs_.x[std::size_t(12 + m)], figs_.y[std::size_t(12 + m)]};
    }
    people_.set_mech_places(mechs);
    people_.move(map_, terrain_, position_, party_places_, animation_, random_, std::span(combat_.on_screen).first<8>(),
                 flags_[flag::kLetOut] != 0);
    if (people_.outside_window) {
        people_.outside_window = false;
        log_.push_back("a person's step looked outside the cell window: read the map instead");
    }
    // A townsperson's place and heading are figure 16 + i's (F:4004 / 4036,
    // F:3920; docs/townspeople.md): one memory in the original.
    for (int i = 0; i < 8; ++i) {
        const auto& w = people_.walkers()[std::size_t(i)];
        figs_.x[std::size_t(16 + i)] = w.at.x;
        figs_.y[std::size_t(16 + i)] = w.at.y;
        figs_.heading[std::size_t(16 + i)] = w.heading;
    }
}

void Town::turn(int k, KeySource& keys) {
    // BTECH.EXE 1000:0070: every key but Space goes to 1000:218f; one that is
    // no direction is a step on the spot.
    if (k == ' ') {
        log_.push_back("key " + std::to_string(k) + ": nothing");
        return;
    }
    const auto step = step_for_key(k);
    // BTECH.EXE 1000:0069..00a0: the step DG:015a times, the figures drawn
    // after each (the last by the key half), stopping when a message or
    // question came up (F:d55c).
    std::optional<StepResult> r;
    bool message = false;
    for (int i = 0; i < steps_per_key_; ++i) {
        if (i > 0) record_party_places();  // 1000:051b
        r = step_party(step.value_or(Step{}), keys);
        if (!r) return;
        message = r->offered_place || r->outcome == StepOutcome::TooDeep || turn_message_;
        if (message || stopped()) break;
    }
    // The main loop then writes the direction in the upper panel box, unless
    // a message or question came up this turn (DG:0160 lists only the eight
    // direction keys).
    if (!message && step) {
        text_.select_window(4);
        text_.set_fg(TextRole::Text);
        text_.print(lookup(texts_, direction_text_id(*step)));
    }
}

// BTECH.EXE 1000:218f, after each axis's move: each queued grid whose area
// has a map in the area-to-map table (27e8:0030 in memory: area_maps_)
// gets it (1000:2da8, 287f:1da8); the entry is emptied
// either way.
void Town::run_map_loads() {
    for (std::size_t i = 0; i < 3; ++i) {
        if (terrain_.queued_grids[i] == 0xff) continue;
        const int map = area_maps_[terrain_.queued_areas[i]];
        if (map != 0) load_town_map(map, terrain_.queued_grids[i]);
        terrain_.queued_grids[i] = 0xff;
    }
}

// BTECH.EXE 1000:218f (Ghidra FUN_1000_218f)
// The step check (1000:1c12) and the move. The characters walking are those
// on foot (figures 4-11 active, F:4072); the 'Mechs those of figures 0-3
// that are active (F:406a), where they were last drawn. Nothing when the
// game now waits in a question or a place.
std::optional<StepResult> Town::step_party(Step step, KeySource& keys) {
    Question question(*this, keys);
    turn_message_ = false;
    Party walking = party();
    for (int i = 0; i < 8; ++i) walking.present[std::size_t(i)] = walking.present[std::size_t(i)] && on_foot(i);
    step_mechs_ = active_mechs();
    auto r = take_step(map_, map_number_, position_, walking, step, question, tables_, terrain_, step_mechs_,
                       window_at_, blocking_);
    if (r.event_tile) {
        if (!cache_step(*r.event_tile, r.event_at, keys)) {
            log_.push_back("waiting in the cache");
            return std::nullopt;
        }
        r.position = position_;  // the step was stopped; the tile may have moved the party
    }
    if (place_open_) {
        log_.push_back("waiting in place " + std::to_string(*r.offered_place));
        return std::nullopt;
    }
    if (question_open_) {  // the original waits for the answer before anything else
        log_.push_back("asked to enter place " + std::to_string(*r.offered_place) + " (waiting)");
        return std::nullopt;
    }
    // Without the game folder the place can't run, and the walk stops at it.
    if (r.entered && !game_dir_) return std::nullopt;
    // 1000:218f moves y, then x; a move into the next area moves the grids
    // over (287f:158c and the others) before the other axis.
    const auto area_of = [](std::uint16_t x, std::uint16_t y) { return ((y >> 8) & 0xf0) | ((x >> 8) & 0x0f); };
    if ((r.position.y ^ position_.y) & 0xff00)
        terrain_.step_area(0, step.dy, area_of(position_.x, r.position.y));
    run_map_loads();
    if ((r.position.x ^ position_.x) & 0xff00)
        terrain_.step_area(step.dx, 0, area_of(r.position.x, r.position.y));
    run_map_loads();
    position_ = r.position;
    char where[32];
    std::snprintf(where, sizeof where, " at %04x,%04x", position_.x, position_.y);
    std::string line;
    if (r.offered_place) line += "asked to enter place " + std::to_string(*r.offered_place) + " (no); ";
    switch (r.outcome) {
    case StepOutcome::Moved: line += "moved"; break;
    case StepOutcome::Blocked: line += "blocked"; break;
    case StepOutcome::TooDeep:
        open_message_box();
        text_.print(lookup(texts_, "walk.too_deep"));
        line += "too deep";
        break;
    }
    log_.push_back(line + where);
    build_window();  // 287f:1314 at the end of 1000:218f
    terrain_.corner_words_set = true;  // 287f:1df8
    return r;
}

namespace {
std::uint32_t place_key(const formats::MapPosition& p) { return (std::uint32_t(p.x) << 16) | p.y; }
}  // namespace

std::vector<Step> Town::WalkTree::steps_to(std::uint32_t k) const {
    std::vector<Step> path;
    while (came_from.at(k).first != k) {
        path.push_back(came_from.at(k).second);
        k = came_from.at(k).first;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

Town::WalkTree Town::walk_tree() const {
    struct NoQuestion : EntranceQuestion {
        bool ask(int, bool) override { return false; }
    } no;
    Party walking = party();
    for (int i = 0; i < 8; ++i) walking.present[std::size_t(i)] = walking.present[std::size_t(i)] && on_foot(i);
    const MechParty mechs = active_mechs();
    WalkTree tree;
    std::unordered_set<std::uint64_t> events_seen;
    std::deque<formats::MapPosition> queue{position_};
    tree.came_from[place_key(position_)] = {place_key(position_), {}};
    while (!queue.empty()) {
        const auto at = queue.front();
        queue.pop_front();
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const Step step{dx, dy};
                const auto r = take_step(map_, map_number_, at, walking, step, no, tables_, terrain_, mechs, at, blocking_);
                if (r.event_tile) {
                    const std::uint64_t e = (std::uint64_t(*r.event_tile) << 32) | place_key(r.event_at);
                    if (!events_seen.insert(e).second) continue;
                    auto path = tree.steps_to(place_key(at));
                    path.push_back(step);
                    tree.events.push_back({*r.event_tile, r.event_at, std::move(path)});
                    continue;
                }
                if (r.outcome != StepOutcome::Moved) continue;
                // Within the area: a walk that leaves it would move the grids.
                if (((r.position.x ^ at.x) | (r.position.y ^ at.y)) & 0xff00) continue;
                if (!tree.came_from.emplace(place_key(r.position), std::pair{place_key(at), step}).second) continue;
                queue.push_back(r.position);
            }
    }
    return tree;
}

std::vector<Town::Walk> Town::walks() const { return walk_tree().events; }

Town::Walk Town::walk_toward(formats::MapPosition target) const {
    struct NoQuestion : EntranceQuestion {
        bool ask(int, bool) override { return false; }
    } no;
    Party walking = party();
    for (int i = 0; i < 8; ++i) walking.present[std::size_t(i)] = walking.present[std::size_t(i)] && on_foot(i);
    const MechParty mechs = active_mechs();
    const auto tree = walk_tree();
    const auto distance = [&](const formats::MapPosition& p) {
        return std::max(std::abs(int(target.x) - int(p.x)), std::abs(int(target.y) - int(p.y)));
    };
    // Each place reached, and each step from one into the next area (the
    // step check reads the next area's terrain as the game does from there).
    struct Candidate {
        int distance;
        std::size_t steps;
        std::uint32_t from;
        std::optional<Step> across;
        formats::MapPosition at;
    };
    std::optional<Candidate> best;
    const auto consider = [&](const Candidate& c) {
        if (!best || c.distance < best->distance || (c.distance == best->distance && c.steps < best->steps)) best = c;
    };
    for (const auto& [k, from] : tree.came_from) {
        const formats::MapPosition p{std::uint16_t(k >> 16), std::uint16_t(k & 0xffff)};
        const std::size_t length = tree.steps_to(k).size();
        consider({distance(p), length, k, std::nullopt, p});
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const Step step{dx, dy};
                const auto r = take_step(map_, map_number_, p, walking, step, no, tables_, terrain_, mechs, p, blocking_);
                if (r.event_tile || r.outcome != StepOutcome::Moved) continue;
                if (!(((r.position.x ^ p.x) | (r.position.y ^ p.y)) & 0xff00)) continue;
                consider({distance(r.position), length + 1, k, step, r.position});
            }
    }
    auto steps = tree.steps_to(best->from);
    if (best->across) steps.push_back(*best->across);
    return {0, best->at, std::move(steps)};
}

// BTECH.EXE 287f:1314 (Ghidra FUN_287f_1314)
// The cell window built at the party's place, over whatever the fight or a
// map load left there.
void Town::build_window() {
    window_at_ = position_;
    terrain_.release_window();
    if (window_stale_) {
        terrain_.map_changed();
        window_stale_ = false;
    }
}

}  // namespace chr::game
