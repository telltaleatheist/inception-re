// Inside a place: the entrance routine, the .BLD script interpreter and the
// special actions the Citadel uses (docs/scripts.md). The script's bytes come
// from the player's .BLD at run time; every text it prints is looked up by
// its text ID (bld.<file>.<offset>), so the words come from the import.

#include <cstdio>
#include <stdexcept>

#include "formats/bld.h"
#include "formats/game_files.h"
#include "game/movement.h"
#include "game/town.h"
#include "game/training_quiz.h"

namespace chr::game {

namespace {

constexpr int kPanelPictureWindow = 4;

}  // namespace

std::optional<int> Town::read_key(KeySource& keys) {
    const auto k = keys.next();
    if (!k) place_open_ = true;
    return k;
}

void Town::print_id(const std::string& id) { text_.print(lookup(texts_, id)); }

// The bars are drawn into window 3, the place's picture into window 4:
// clearing the window takes them away too.
void Town::clear_window() {
    text_.clear_window();
    if (text_.window_number() == 3) party_box_shown_ = false;
    if (text_.window_number() == kPanelPictureWindow) picture_.reset();
}

// BTECH.EXE 17dc:0008 (Ghidra FUN_17dc_0008)
// Goes into the place behind entrance `entrance`: runs its script, then puts
// the panel back. The step that offered it goes on afterwards as after "no".
bool Town::visit_place(int entrance, KeySource& keys) {
    flags_[flag::kInPlace] = 1;
    place_number_ = entrance;
    int file = entrance;
    if (entrance != 0x12 && entrance < 0x16) file = map_.place_scripts[std::size_t(entrance)];
    // 1000:28cc asks for the game disk; there are no disks. Script 0 (the
    // entrance is turned into its script first: the training center, not
    // entrance 0 of any map) first runs the quiz, 1c67:08a8, while flag 14 is clear and
    // no demo plays (F:3938, F:458c). It has no check here (docs/rules.md,
    // rule 3; docs/combat.md 7): its draws are made, and with --show-quiz its
    // scene is shown.
    if (file == 0 && flags_[flag::kQuizLock] == 0) {
        if (show_quiz_ && rules_) {
            if (!show_quiz(keys)) return false;
        } else {
            const auto parts = draw_quiz_parts(random_);
            log_.push_back("training quiz: parts " + std::to_string(parts[0]) + " " + std::to_string(parts[1]) +
                           " " + std::to_string(parts[2]) + " (not shown)");
        }
        // 1c67:0b40: the panel, the party box cleared, then the box above it.
        draw_panel(true);
        text_.clear_window();
    }
    if (!run_script(file, keys)) return false;
    // Special 14 after every visit.
    count_members();
    // 1000:7e8f..7f17: the two ways into VIEWDISK (place 0x12), each by
    // this routine again, then area 0x34 becomes map 12 (27e8:0064). After
    // the garage only before flag 32, with flag 30 and a party, and after a
    // text and a key; after BARRACK2 (0x13) whenever there is a party.
    if (flags_[flag::kDiskSeen] == 0 && file == 8 && flags_[flag::kFrobPaid] != 0 && flags_[flag::kMembers] != 0) {
        text_.select_window(6);
        clear_window();
        print_id("place.follow_on.0");
        print_id("place.follow_on.1");  // 14d3:17ea
        if (!read_key(keys)) return false;
        if (!visit_place(0x12, keys)) return false;
        area_maps_[0x34] = 12;
    }
    if (file == 0x13 && flags_[flag::kMembers] != 0) {
        if (!visit_place(0x12, keys)) return false;
        area_maps_[0x34] = 12;
    }
    text_.select_window(kPanelPictureWindow);
    text_.clear_window();
    picture_.reset();
    text_.select_window(3);
    draw_panel();
    // 1000:7f46: leaving ENTRANCE once its question is answered yes (flag
    // 36), into the cache. A column of the seen map is cleared first
    // (docs/cache.md).
    if (file == 0x14 && flags_[flag::kCacheOpen] != 0) {
        for (int r = 0; r < 8; ++r) seen_[std::size_t(0x60c + 0x10 * r)] = 0;
        return enter_cache(keys);
    }
    return true;
}

// BTECH.EXE 17dc:01c0 (Ghidra FUN_17dc_01c0)
// Loads script `file` (17dc:1d30) and runs it from offset 0 to its `end`.
bool Town::run_script(int file, KeySource& keys) {
    const std::string name(formats::kBldFiles[std::size_t(file)]);
    const auto path = formats::find_game_file(*game_dir_, name + ".BLD");
    if (!path) throw std::runtime_error(name + ".BLD is missing from " + game_dir_->string());
    const auto script = formats::read_bld(*path);
    // DGROUP:141a, the picture by script number, was part of BTECH.EXE; it is
    // now imported (tables.json: place_pictures).
    if (const int picture = tables_.place_pictures[std::size_t(file)]; picture != 0) show_picture(picture, 1);
    log_.push_back("script " + name);

    int pc = 0;
    const int size = static_cast<int>(script.size());
    auto sbyte = [&](int at) { return static_cast<int>(static_cast<std::int8_t>(script.at(std::size_t(at)))); };
    while (pc < size) {
        const auto in = formats::decode(script, pc, menus_);
        if (!in.op) {  // not an opcode: stepped over
            pc = in.next;
            continue;
        }
        int next = in.next;
        auto jump_if = [&](bool taken) {
            if (taken) next = in.targets.back();
        };
        switch (*in.op) {
        case formats::Op::End:
            return true;
        case formats::Op::Window:
            text_.select_window(sbyte(pc + 1));
            break;
        case formats::Op::Clear:
            clear_window();
            break;
        case formats::Op::Frame:
            text_.frame(sbyte(pc + 1));
            break;
        case formats::Op::Print:
            print_id(formats::bld_text_id(name, pc));
            break;
        case formats::Op::WaitKey:
            if (!read_key(keys)) return false;
            break;
        case formats::Op::Pause:
            // 50 ticks (273d:0006), then the keyboard is emptied (1000:2a2b):
            // a key typed ahead is lost. Keys here only come when asked for.
            break;
        case formats::Op::Goto:
            next = in.targets.back();
            break;
        case formats::Op::Menu: {
            const auto chosen = menu(sbyte(pc + 1), keys);
            if (!chosen) return false;
            next = in.targets.at(std::size_t(*chosen));
            break;
        }
        case formats::Op::SwitchFlag: {
            // The table is indexed by the flag as a signed byte; the program
            // reads whatever word is there.
            const int value = static_cast<std::int8_t>(flags_[std::size_t(in.bytes[0])]);
            next = script.at(std::size_t(pc + 2 + 2 * value)) | script.at(std::size_t(pc + 3 + 2 * value)) << 8;
            break;
        }
        case formats::Op::IfFlag:
            jump_if(flags_[std::size_t(in.bytes[0])] != 0);
            break;
        case formats::Op::SetFlag:
            flags_[std::size_t(in.bytes[0])] = static_cast<std::uint8_t>(in.bytes[1]);
            break;
        case formats::Op::AddFlag:
            flags_[std::size_t(in.bytes[0])] = static_cast<std::uint8_t>(flags_[std::size_t(in.bytes[0])] + in.bytes[1]);
            break;
        case formats::Op::Cursor:
            text_.set_cursor(sbyte(pc + 1), sbyte(pc + 2));
            break;
        case formats::Op::IfYes: {
            const auto yes = yes_no(true, keys);
            if (!yes) {
                place_open_ = true;
                return false;
            }
            jump_if(*yes);
            break;
        }
        case formats::Op::Special:
            if (!special(in.bytes[0], keys)) return false;
            break;
        case formats::Op::Earn:
            money_.cbills += static_cast<std::int16_t>(in.words[0]);
            draw_cbills();
            break;
        case formats::Op::Pay: {
            const std::int64_t amount = static_cast<std::int16_t>(in.words[0]);
            // Compared as 32-bit unsigned numbers: more than they have takes it to 0.
            const auto have = static_cast<std::uint32_t>(money_.cbills);
            const auto take = static_cast<std::uint32_t>(amount);
            money_.cbills = take > have ? 0 : static_cast<std::int32_t>(have - take);
            draw_cbills();
            break;
        }
        case formats::Op::IfCBills: {
            const auto need = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(in.words[0])));
            jump_if(static_cast<std::uint32_t>(money_.cbills) >= need);
            break;
        }
        case formats::Op::IfFlagD450:
            jump_if(kits_[0] != 0);  // F:d450
            break;
        case formats::Op::IfFlagD451:
            jump_if(kits_[1] != 0);  // F:d451
            break;
        case formats::Op::IfRandom:
            jump_if((random_.next() & sbyte(pc + 1)) != 0);
            break;
        case formats::Op::IfPartyX:
            jump_if(in.words[0] == position_.x);
            break;
        case formats::Op::SetPartyPlace:
            position_ = {static_cast<std::uint16_t>(in.words[0]), static_cast<std::uint16_t>(in.words[1])};
            break;
        case formats::Op::IfSkill: {
            bool any = false;
            for (std::size_t i = 0; i < 8; ++i)
                if (party().present[i] && in.bytes[0] < character::kSkillCount && characters_[i][std::size_t(character::kSkills + in.bytes[0])] >= sbyte(pc + 2)) any = true;
            jump_if(any);
            break;
        }
        case formats::Op::Picture:
            show_picture(sbyte(pc + 1), sbyte(pc + 2));
            break;
        case formats::Op::Sound:
            play_sound(sbyte(pc + 1));  // 1000:19bf
            break;
        case formats::Op::NewMember:
            if (!new_member(in.bytes[0], keys)) return false;
            break;
        }
        pc = next;
    }
    return true;
}

// BTECH.EXE 2656:0b5e (Ghidra FUN_2656_0b5e)
// Menu m in the current window: a bar on the item it last chose, moved by
// the arrows (wrapping), Enter or space to choose.
std::optional<int> Town::menu(int m, KeySource& keys, std::optional<int> start) {
    const auto& rec = menus_.at(std::size_t(m));
    int& last = menu_last_.at(std::size_t(m));
    if (last >= rec.items) last = 0;
    int chosen = start && *start >= 0 && *start < rec.items ? *start : last;
    const auto& w = text_.window();
    const int top = w.y + rec.first_row;
    text_.menu_bar(w.x, top + chosen, rec.width, m);
    for (;;) {
        const auto k = read_key(keys);
        if (!k) return std::nullopt;
        const int a = arrow_key(*k);
        if (a == key::kEnter || a == key::kSpace) break;
        if (a != key::kUp && a != key::kDown) continue;
        text_.menu_bar(w.x, top + chosen, rec.width, m);
        chosen += a == key::kUp ? -1 : 1;
        if (chosen < 0) chosen = rec.items - 1;
        if (chosen >= rec.items) chosen = 0;
        text_.menu_bar(w.x, top + chosen, rec.width, m);
    }
    // (The program would return the old item for Esc without remembering
    // it, but only Enter and space end the loop.)
    last = chosen;
    return chosen;
}

// BTECH.EXE 19b8:0d58 (Ghidra FUN_19b8_0d58)
// Script op E9 s: a new member joins in the first free record, with skill s
// at 3 (docs/scripts.md, "A new member").
bool Town::new_member(int skill, KeySource& keys) {
    int k = 0;
    while (k < 8 && characters_[std::size_t(k)][character::kName] != character::kEmpty) ++k;
    if (k < 8) {
        // 1000:a9de: the generator's state from the name number.
        const std::uint32_t seed = std::uint32_t(next_name_) * 0x187;
        random_ = Random({std::uint8_t(seed), std::uint8_t(seed >> 8), std::uint8_t(seed >> 16)});
        auto& r = characters_[std::size_t(k)];
        r[character::kName] = next_name_++;
        if (next_name_ >= 10) next_name_ = 2;
        r[0x10] = 0;
        r[character::kArmour] = 0;
        r[character::kArmourType] = 0;
        r[character::kWeapon] = 0;
        r[character::kB] = std::uint8_t(two_d6(random_));
        r[character::kD] = std::uint8_t(two_d6(random_));
        r[character::kC] = std::uint8_t(two_d6(random_));
        r[character::kHitPoints] = std::uint8_t(10 * r[character::kB]);
        figs_.sprite_base[std::size_t(4 + k)] = 0;
        r[character::kMech] = character::kOnFoot;
        for (int i = 0; i < character::kSkillCount; ++i) r[std::size_t(character::kSkills + i)] = std::uint8_t(random_.next() & 1);
        r[character::kSkills + 4] = 0;
        if (skill >= 0 && skill < character::kSkillCount) r[std::size_t(character::kSkills + skill)] = 3;
        if (skill == 4) r[character::kHitPoints] = std::uint8_t(r[character::kHitPoints] - 2 * r[character::kB]);
        // A seat: the second seat (byte 0x7a) of the first 'Mech in use with
        // it free.
        for (int m = 0; m < 4; ++m) {
            auto& mech = mechs_[std::size_t(m)];
            if (mech[0] == 0xff || mech[0x7a] != 0xff) continue;
            mech[0x7a] = std::uint8_t(k);
            r[character::kMech] = std::uint8_t(m);
            break;
        }
        print_id("recruit.name_is");
        print_id("character." + std::to_string(r[character::kName]));
        print_id("recruit.will_be");
        if (r[character::kMech] == character::kOnFoot) {
            print_id("recruit.on_foot");
        } else {
            const auto pilot = mechs_[std::size_t(r[character::kMech])][0x79];
            print_id("recruit.passenger");
            print_id("character." + std::to_string(characters_[std::size_t(pilot)][character::kName]));
            print_id("recruit.mech");
        }
        draw_panel(true);  // 1000:4cac(1)
        // 1000:a960: a draw for every poll with no key waiting; the flush
        // (1000:2a2b) eats the key that ends it, and another is read.
        for (;;) {
            const auto waiting = keys.poll();
            if (!waiting) {
                place_open_ = true;
                poll_open_ = true;
                return false;
            }
            if (*waiting) break;
            random_.next();
        }
        if (!read_key(keys)) {
            place_open_ = true;
            return false;
        }
        // The double agent (combat.md 2.9).
        if (flags_[0x26] == 0 && (random_.next() & 1) != 0) {
            flags_[0x25] = std::uint8_t(k);
            flags_[0x26] = 1;
            flags_[0x27] = 1;
            flags_[0x24] = 0x1f;
        }
        log_.push_back("new member: record " + std::to_string(k) + ", name " + std::to_string(r[character::kName]));
    }
    // F:39b4 / F:39d4 at F:4584 (formats/mtp.md).
    position_ = map_.second_positions[std::size_t(place_number_)];
    return true;
}

// BTECH.EXE 1c67:0002 (Ghidra FUN_1c67_0002)
// The crew screen: who pilots and who rides each 'Mech, until Done finds
// every 'Mech piloted (docs/scripts.md, "The crew screen").
bool Town::crew_screen(int start, KeySource& keys) {
    constexpr std::size_t kPilotSeat = 0x79, kRiderSeat = 0x7a;
    const auto in_use = [this](int m) { return mechs_[std::size_t(m)][0] != character::kEmpty; };
    const auto can_pilot = [this](int c) { return characters_[std::size_t(c)][character::kSkills + 4] != 0; };
    const auto stop = [this] {
        place_open_ = true;
        return false;
    };
    if (start == 0) {
        for (int m = 0; m < 4; ++m) {
            if (!in_use(m)) continue;
            for (const auto seat : {kPilotSeat, kRiderSeat}) {
                const auto c = mechs_[std::size_t(m)][seat];
                if (c != character::kEmpty) characters_[c][character::kMech] = std::uint8_t(m);
            }
        }
    } else {
        for (auto& c : characters_) c[character::kMech] = character::kOnFoot;
        for (int m = 0; m < 4; ++m) mechs_[std::size_t(m)][kPilotSeat] = mechs_[std::size_t(m)][kRiderSeat] = character::kEmpty;
    }
    const auto name = [this](int c) { return "character." + std::to_string(characters_[std::size_t(c)][character::kName]); };
    const auto mech_name = [this](int m) {
        if (const auto id = mech_name_id(m)) print_id(*id);
        else log_.push_back("a 'Mech record with no template's name");
    };
    for (;;) {
        bool done = false;
        text_.select_window(6);
        clear_window();
        text_.frame(0);
        text_.set_fg(TextRole::Heading);  // 1000:28a2
        print_id("crew.heading");
        menus_[23].first_row = text_.window().row;  // F:0202
        std::vector<int> people;
        for (int c = 0; c < 8; ++c) {
            const auto& r = characters_[std::size_t(c)];
            if (r[character::kName] == character::kEmpty) continue;
            people.push_back(c);
            print_id(name(c));
            const int m = std::int8_t(r[character::kMech]);
            if (m == character::kOnFoot) {
                print_id("crew.on_foot");
            } else {
                // A space and the 'Mech's name, its trailing spaces cut.
                text_.print(" ");  // 1c67:0795 puts a space before the copy
                if (const auto id = mech_name_id(m)) {
                    std::string n = lookup(texts_, *id);
                    while (!n.empty() && n.back() == ' ') n.pop_back();
                    text_.print(n);
                } else {
                    log_.push_back("a 'Mech record with no template's name");
                }
                print_id(mechs_[std::size_t(m)][kPilotSeat] == c ? "crew.pilot" : "crew.rider");
            }
        }
        print_id("crew.done");
        menus_[23].items = int(people.size()) + 1;  // F:0206
        text_.set_cursor(text_.window().col, 10);  // F:374e
        for (int m = 0; m < 4; ++m) {
            if (!in_use(m)) continue;
            mech_name(m);
            print_id("crew.pilot_label");
            const auto p = mechs_[std::size_t(m)][kPilotSeat];
            print_id(p == character::kEmpty ? "crew.no_pilot" : name(p));
            print_id("crew.rider_label");
            const auto r = mechs_[std::size_t(m)][kRiderSeat];
            print_id(r == character::kEmpty ? "crew.no_rider" : name(r));
            text_.set_fg(TextRole::Text);
            print_id("talk.line_end");  // 24d3:181e
        }
        const auto chosen = menu(23, keys);
        if (!chosen) return stop();
        if (*chosen == int(people.size())) {
            // Done.
            done = true;
            for (int m = 0; m < 4; ++m)
                if (in_use(m) && mechs_[std::size_t(m)][kPilotSeat] == character::kEmpty) done = false;
            if (!done) {
                text_.select_window(4);
                clear_window();
                print_id("crew.not_all");
                int pilots = 0, mechs = 0;
                for (const int c : people) pilots += can_pilot(c) ? 1 : 0;
                for (int m = 0; m < 4; ++m) mechs += in_use(m) ? 1 : 0;
                if (pilots < mechs) {
                    if (!read_key(keys)) return stop();  // 273d:086a, 273d:0259
                    clear_window();
                    print_id("crew.abandon_warning");  // 24d3:17ea
                    if (!read_key(keys)) return stop();
                    clear_window();
                    print_id("crew.abandon_question");  // 24d3:17ea
                    const auto yes = yes_no(true, keys);  // 1000:1a13
                    if (!yes) return stop();
                    if (*yes) {
                        for (int m = 0; m < 4; ++m) {
                            if (!in_use(m) || mechs_[std::size_t(m)][kPilotSeat] != character::kEmpty) continue;
                            mechs_[std::size_t(m)][0] = character::kEmpty;
                            for (auto& c : characters_)
                                if (c[character::kMech] == m) c[character::kMech] = character::kOnFoot;
                        }
                    }
                } else {
                    print_id("crew.assign_each");  // 24d3:17ea
                    if (!read_key(keys)) return stop();
                }
            }
        } else {
            // A character: which 'Mech, and which seat.
            const int c = people[std::size_t(*chosen)];
            const bool pilot = can_pilot(c);
            text_.select_window(4);
            clear_window();
            text_.set_fg(TextRole::Text);
            print_id(name(c));
            print_id("crew.is");
            if (!pilot) {
                text_.set_fg(TextRole::Emphasis);  // 1000:28a2
                print_id("crew.not");
            }
            print_id("crew.qualified");
            text_.select_window(3);
            clear_window();
            print_id("crew.which_vehicle");
            menus_[3].first_row = text_.window().row;  // F:00c2
            std::vector<int> vehicles;
            for (int m = 0; m < 4; ++m) {
                if (!in_use(m)) continue;
                mech_name(m);  // its padding wraps the line
                vehicles.push_back(m);
            }
            print_id("crew.no_vehicle");
            menus_[3].items = int(vehicles.size()) + 1;  // F:00c6
            for (int m = 0; m < 4; ++m) {
                for (const auto seat : {kPilotSeat, kRiderSeat})
                    if (mechs_[std::size_t(m)][seat] == c) mechs_[std::size_t(m)][seat] = character::kEmpty;
            }
            const auto v = menu(3, keys);
            if (!v) return stop();
            if (*v == int(vehicles.size())) {
                characters_[std::size_t(c)][character::kMech] = character::kOnFoot;
            } else {
                const int m = vehicles[std::size_t(*v)];
                auto& mech = mechs_[std::size_t(m)];
                std::size_t seat = kRiderSeat;
                if (pilot) {
                    seat = kPilotSeat;
                    if (mech[kPilotSeat] != character::kEmpty) {
                        clear_window();
                        print_id("crew.as_pilot");
                        const auto yes = yes_no(true, keys);  // 1000:1a13
                        if (!yes) return stop();
                        if (!*yes) seat = kRiderSeat;
                    }
                }
                const auto had = mech[seat];
                if (had != character::kEmpty) {
                    if (seat == kPilotSeat) {
                        if (mech[kRiderSeat] != character::kEmpty) characters_[mech[kRiderSeat]][character::kMech] = character::kOnFoot;
                        mech[kRiderSeat] = had;
                    } else {
                        characters_[had][character::kMech] = character::kOnFoot;
                    }
                }
                // The double agent's tell (flag 27, F:d331).
                if (flags_[0x27] != 0 && flags_[0x25] == c) {
                    open_message_box();  // 24d3:17c6
                    print_id("crew.uneasy");
                    print_id(name(c));
                    print_id("crew.uneasy_end");  // 24d3:17ea
                    if (!read_key(keys)) return stop();
                    agent_found_ = true;
                }
                mech[seat] = std::uint8_t(c);
                characters_[std::size_t(c)][character::kMech] = std::uint8_t(m);
            }
        }
        if (!done) {
            clear_window();  // whichever window is current
            continue;
        }
        for (auto& h : held_mechs_) h = character::kEmpty;
        flags_[0x18] = 0;
        return true;
    }
}

// BTECH.EXE 1000:2c50 (Ghidra FUN_1000_2c50)
// The Space command menu: menu 1 in window 1, one row higher and one item
// longer when any of 'Mech records 0-3 is in use (the crew item). The crew
// item, the settings, the healing, the load, the save and the overhead map
// are rebuilt; the inspect item is logged.
bool Town::command_menu(KeySource& keys) {
    bool mechs = false;
    for (int m = 0; m < 4; ++m) mechs = mechs || mechs_[std::size_t(m)][0] != character::kEmpty;
    text_.place_window(1, mechs ? 12 : 13, mechs ? 8 : 7);
    menus_[1].items = mechs ? 8 : 7;
    text_.select_window(1);
    clear_window();
    text_.frame(1);
    print_id("space.items.0");
    if (mechs) print_id("space.items.allocate");
    print_id("space.items.1");
    print_id("space.items.2");
    const auto chosen = menu(1, keys);
    if (!chosen) {
        place_open_ = true;
        return false;
    }
    // Numbered as with the crew item.
    const int item = !mechs && *chosen >= 2 ? *chosen + 1 : *chosen;
    switch (item) {
    case 0:
        break;
    case 1:
        if (!game_settings(keys)) return false;  // 1000:3bd0
        break;
    case 2:
        if (!allocate_crew(keys)) return false;
        break;
    case 4:
        if (!heal(0, keys)) return false;  // 1c31:000a(0)
        break;
    case 5:
        if (!load_game(keys)) return false;  // 1000:32b3
        break;
    case 6:
        if (!save_game(keys)) return false;  // 1000:35d3
        break;
    case 7:
        if (!overhead_map(keys)) return false;  // 1000:3d40
        break;
    default:
        log_.push_back("the command menu's item " + std::to_string(item) + ": not rebuilt");
        break;
    }
    // 273d:06c3 copies the page's map columns back over the windows; the
    // tick that follows draws the map again (1000:046e).
    return true;
}

// BTECH.EXE 1000:3bd0 (Ghidra FUN_1000_3bd0)
// The command menu's settings, in its window: steps a key, message speed,
// sound, how often a place's picture shows, quit (docs/town_loop.md).
bool Town::game_settings(KeySource& keys) {
    clear_window();  // 2656:0388
    print_id("settings.title");
    print_id(sound_.on ? "settings.sound_on" : "settings.sound_off");
    print_id("settings.items");
    const auto item = menu(0x21, keys);
    if (!item) {
        place_open_ = true;
        return false;
    }
    switch (*item) {
    case 0: {
        clear_window();
        print_id("settings.steps");
        // Menu 0x27's record is written directly: three items, starting a
        // row down, the bar on the current choice (1, 2, 4).
        auto& rec = menus_[0x27];
        rec.items = 3;
        rec.first_row = 1;
        menu_last_[0x27] = steps_per_key_ - 1 - (steps_per_key_ == 4 ? 1 : 0);
        const auto steps = menu(0x27, keys);
        if (!steps) {
            place_open_ = true;
            return false;
        }
        steps_per_key_ = *steps + 1 == 3 ? 4 : *steps + 1;
        rec.first_row = 0;
        if (flags_[flag::kCacheLevel] != 0) steps_per_key_ = 1;
        return true;
    }
    case 1: {
        clear_window();
        print_id("settings.speed");
        const auto speed = menu(0x22, keys);
        if (!speed) {
            place_open_ = true;
            return false;
        }
        message_speed_ = *speed;
        break;
    }
    case 2:
        sound_.on = !sound_.on;  // 1000:3ce4: the low byte flipped
        return true;
    case 3: {
        clear_window();
        print_id("settings.pictures");
        const auto chance = menu(0x26, keys);
        if (!chance) {
            place_open_ = true;
            return false;
        }
        flags_[flag::kPictureChance] = std::uint8_t(*chance);
        break;
    }
    case 4: {
        clear_window();
        print_id("settings.quit");
        print_id("settings.quit_2");
        const auto quit = yes_no(false, keys);  // 1000:1a13(0)
        if (!quit) {
            place_open_ = true;
            return false;
        }
        if (*quit) {
            log_.push_back("the game settings: quit");
            quit_ = true;  // DG:0152
        }
        return true;
    }
    default:
        return true;
    }
    draw_panel(true, false);  // 1000:4cac(1): column 0-12 not blanked first
    return true;
}

// BTECH.EXE 1000:4d57 (Ghidra FUN_1000_4d57)
// The command menu's crew item: the crew screen, unless a map block (bit
// 0x80 of any of the window's nine block values) is near; then a refusal
// in the message box and a key.
bool Town::allocate_crew(KeySource& keys) {
    const auto& blocks = terrain_.held_window(window_at_).blocks;
    bool near_map = false;
    for (const auto b : blocks) near_map = near_map || (b & 0x80) != 0;
    if (near_map) {
        open_message_box();  // 24d3:17c6, 17ea
        print_id("space.no_dismount");
        if (!read_key(keys)) {  // 273d:0259
            place_open_ = true;
            return false;
        }
        return true;
    }
    if (!crew_screen(0, keys)) return false;
    draw_panel(true);  // 1000:4cac(1)
    clear_window();    // 2656:0388
    return true;
}

// BTECH.EXE 24d3:0004 (Ghidra FUN_24d3_0004), the cases the Citadel uses.
bool Town::special(int n, KeySource& keys) {
    switch (n) {
    case 1:
        return walk_in(keys);  // game/fight.cpp
    case 2:
        // 24d3:0187: after the course, by the 'Mech's first letter.
        if (mechs_[0][0] == 'L') {
            print_id("course.result_locust");
        } else {
            print_id(mechs_[0][0] == 'W' ? "course.result_wasp" : "course.result_chameleon");
            print_id("course.result_other");
        }
        return true;
    case 3:
        // 24d3:01c2: the 'Mech is handed back. Jason is on foot outside the
        // training center; unless flag 04 is set, the fires (marks 0x7c and
        // 0x7d) are taken off the map, all but their y.
        mechs_[0][0] = 0xff;
        characters_[0][character::kMech] = character::kOnFoot;
        position_ = {0x0c2e, 0xc076};
        if (flags_[flag::kOutdoors] == 0)
            for (auto& m : marks_.marks)
                if ((m.code & 0x7e) == 0x7c) m.code = m.area = m.x = 0;
        return true;
    case 4:
        // 24d3:0231: the three courses' prices, 125 x Jason's skill + 75,
        // right-aligned to column 39 on rows 5-7, light green.
        for (int i = 0; i < 3; ++i) {
            const std::string price = std::to_string(125 * characters_[0][std::size_t(character::kSkills + i)] + 75);
            text_.put_text(price, 39 - static_cast<int>(price.size()), 5 + i, TextRole::Amount, TextRole::Background);
        }
        return true;
    case 5: {
        // 24d3:02a5: Jason takes the course in flag 08.
        const int course = static_cast<std::int8_t>(flags_[flag::kCourse]);
        const int level = characters_[0].at(std::size_t(character::kSkills + course));
        print_id("course.transcript");
        print_id("course.level." + std::to_string(level));
        print_id("course.subject." + std::to_string(course));
        if (level == 4) {
            print_id("course.maxed");
            flags_[flag::kEnrolled] = 0;
            return true;
        }
        const int price = 125 * level + 75;
        if (static_cast<std::uint32_t>(money_.cbills) >= static_cast<std::uint32_t>(price)) {
            flags_[flag::kEnrolled] = 1;
            ++characters_[0][std::size_t(character::kSkills + course)];
            money_.cbills -= price;
            draw_cbills();  // 24d3:1961, the end of several cases
        } else {
            print_id("course.no_money");
            print_id("course.no_money_end");  // 24d3:1809
            flags_[flag::kEnrolled] = 0;
        }
        return true;
    }
    case 43: {
        // 24d3:157e: Rick Atlas waits in the Lounge. Townsperson 0 stands
        // at the Lounge's entrance (7), inside (wait ff), last in the
        // Lounge and bound for the Training Center (entrance 0), until the
        // talk lets him out (flag 2d, talk()).
        auto& w = people_.walkers()[0];
        w.at = map_.entrances[7];
        w.target = map_.entrances[0];
        w.route = 0x70;
        w.wait = 0xff;
        figs_.x[16] = w.at.x;
        figs_.y[16] = w.at.y;
        return true;
    }
    case 10:
        // 24d3:096f: the money in decimal at the cursor, then shop.cbills_end.
        text_.set_fg(TextRole::Funds);
        text_.print(std::to_string(money_.cbills));
        text_.print(lookup(texts_, "shop.cbills_end"));
        return true;
    case 12:
        return talk(keys);
    case 14:
        count_members();
        return true;
    case 13: {
        // 24d3:0b26: who attends. Special 14 first, then menu 23 (0x17) at
        // the cursor's row with one item for each present character, its
        // name and a line end, the bar on the first; flag 0e = the
        // chosen one's record.
        count_members();
        auto& rec = menus_[0x17];
        rec.first_row = text_.window().row;
        rec.items = flags_[flag::kMembers] + 1;
        menu_last_[0x17] = 0;
        std::vector<int> listed;
        for (int i = 0; i < 8; ++i) {
            if (characters_[std::size_t(i)][character::kName] == character::kEmpty) continue;
            print_id("character." + std::to_string(characters_[std::size_t(i)][character::kName]));
            print_id("talk.line_end");  // 24d3:181e
            listed.push_back(i);
        }
        const auto chosen = menu(0x17, keys);
        if (!chosen) return false;
        flags_[flag::kMembers] = std::uint8_t(listed.at(std::size_t(*chosen)));
        return true;
    }
    case 15:
    case 21: {
        // 24d3:0bfc (REPAIR's Tech course) and 24d3:0c5c (HOSPITAL's
        // seminar): with 500 C-Bills, the character in flag 0e pays them,
        // gains a point of Tech or Medical and is marked (byte 0x10, bit 0
        // or 1); flag 0e = 1 and the C-Bills line drawn again
        // (1e31:1fdf). Short of 500, flag 0e = 0 and nothing else.
        const bool tech = n == 15;
        if (money_.cbills < 500) {
            flags_[flag::kMembers] = 0;
            return true;
        }
        money_.cbills -= 500;
        auto& c = characters_[std::size_t(flags_[flag::kMembers])];
        ++c[std::size_t(character::kSkills + (tech ? 5 : 6))];
        c[character::kCourses] |= tech ? 1 : 2;
        flags_[flag::kMembers] = 1;  // the Tech course's end jumps into the seminar's, 24d3:0c9b
        draw_cbills();
        return true;
    }
    case 16:
    case 22:
        // 24d3:0be1 and 0c46: flag 0f = whether the character in flag 0e
        // has taken that course (byte 0x10, bit 0 or bit 1).
        flags_[0x0f] = characters_[std::size_t(flags_[flag::kMembers])][character::kCourses] & (n == 16 ? 1 : 2);
        return true;
    case 23:
        flags_[0x19] = anyone_hurt() ? 1 : 0;
        return true;
    case 30:
        return alley_fight(keys);  // game/fight.cpp
    case 38:
        // 24d3:13c6: the name the next recruit will get (F:d456).
        print_id("character." + std::to_string(next_name_));
        return true;
    case 39:
        // 24d3:13e6: the crew screen with everyone on foot first.
        return crew_screen(1, keys);
    case 40:
        // 24d3:13ec
        return jail_break(keys);
    case 45:
        // 24d3:15dd: ENDMECH.CMP over the whole screen with its own colours
        // until a key (273d:086a, 0259); then the screen set back for the
        // town (287f:1fbe; STARLEAG.ICN loaded again, the palette back), the
        // panel and the box cleared. CGA's colours are not rebuilt.
        screen_picture_ = assets::kEndMechPicture;
        if (!read_key(keys)) return false;
        screen_picture_.reset();
        draw_panel(true);  // 1000:4cac(1)
        clear_window();    // 2656:0388
        return true;
    case 46:
        // 24d3:0e2c: the held 'Mechs back in their records, then the crew
        // screen (docs/scripts.md).
        for (int i = 0; i < 4; ++i) {
            mechs_[std::size_t(i)][0] = held_mechs_[std::size_t(i)];
            figs_.sprite_base[std::size_t(i)] = mechs_[std::size_t(i)][0] == 'L' ? 0 : 0x92;
        }
        return crew_screen(0, keys);
    case 29: {
        // 24d3:0e9b: flag 10 = how many of F:d452..d455 hold a letter A-Z.
        int held = 0;
        for (const auto b : held_mechs_) held += (b >= 'A' && b <= 'Z') ? 1 : 0;
        flags_[0x10] = std::uint8_t(held);
        return true;
    }
    case 17: {
        // 24d3:0ca4: flag 10 = how many of 'Mech records 0-3 are in use.
        int in_use = 0;
        for (const bool e : mech_records()) in_use += e ? 1 : 0;
        flags_[0x10] = std::uint8_t(in_use);
        return true;
    }
    default:
        log_.push_back("special " + std::to_string(n) + ": not rebuilt");
        return true;
    }
}

// BTECH.EXE 24d3:0cf8, special 23: flag 19 = whether any party member's
// hit points are below 10 x B.
bool Town::anyone_hurt() const {
    for (int c = 0; c < 8; ++c) {
        const auto& r = characters_[std::size_t(c)];
        if (r[character::kName] == character::kEmpty) continue;
        if (std::int8_t(r[character::kHitPoints]) != std::int8_t(r[character::kB]) * 10) return true;
    }
    return false;
}

// BTECH.EXE 24d3:0af2: flag 0e = the members after Jason.
void Town::count_members() {
    int count = 0;
    for (std::size_t i = 1; i < 8; ++i)
        if (party().present[i]) ++count;
    flags_[flag::kMembers] = static_cast<std::uint8_t>(count);
}

// BTECH.EXE 17dc:17b9 (Ghidra FUN_17dc_17b9)
// "Talk to others": the townspeople inside this place (in for some rounds,
// last gone in at its entrance) are listed in a menu; the one chosen says
// where it goes next.
bool Town::talk(KeySource& keys) {
    // The entrance whose script is the one in flag 0d.
    int here = 0;
    while (here < 8 && map_.place_scripts[std::size_t(here)] != flags_[flag::kPlace]) ++here;
    text_.clear_window();
    std::array<bool, 8> inside{};
    int count = 0;
    int last_home = 0;  // the local at 1000:95dd: the last one inside anywhere
    const auto& walkers = people_.walkers();
    for (std::size_t i = 0; i < 8; ++i) {
        if (walkers[i].wait == 0) continue;
        last_home = walkers[i].route >> 4 & 7;
        if (last_home == here) {
            inside[i] = true;
            ++count;
        }
    }
    if (count == 0) {
        print_id("talk.nobody");
        return read_key(keys).has_value();
    }
    print_id("talk.you_see");
    for (std::size_t i = 0; i < 8; ++i) {
        if (!inside[i]) continue;
        print_id("map" + std::to_string(map_number_) + ".person." + std::to_string(i));
        print_id("talk.line_end");  // 24d3:181e
    }
    print_id("talk.done");
    menus_[20].items = count + 1;
    const auto chosen = menu(20, keys);
    if (!chosen) return false;
    if (*chosen == count) return true;
    const int let_out_place = fixes_.fix_talk_let_out ? here : last_home;  // bug 38
    if (flags_[flag::kLetOut] != 0 && let_out_place == 7 && *chosen == 0 && inside[0]) {
        // Someone is let out of jail (the second map's story).
        flags_[flag::kLetOutDone] = 1;
        people_.walkers()[0].wait = 0;
        return true;
    }
    flags_[flag::kLetOutDone] = 0;
    text_.clear_window();
    if (flags_[flag::kOutdoors] != 0) {
        // Outdoors: one of eight rumours (DGROUP:4e5e) at random (docs/scripts.md).
        random_.next();
        log_.push_back("talk outdoors: not rebuilt");
        return read_key(keys).has_value();
    }
    print_id("talk.hello");
    print_id("talk.reason." + std::to_string(static_cast<std::int8_t>(flags_[flag::kPlace])));
    print_id("talk.going_to");
    int left = *chosen;
    for (std::size_t i = 0; i < 8; ++i) {
        if (!inside[i] || --left >= 0) continue;
        print_id(place_text_id(map_number_, walkers[i].route & 7));
        print_id("talk.end");
        return read_key(keys).has_value();
    }
    return true;
}

// BTECH.EXE 1000:48b7 (Ghidra FUN_1000_48b7)
// A place's picture in the upper left box. One random number is always
// drawn; unless the place always shows its picture, it shows only when
// that number AND the chance mask (by flag 4f) is 0.
void Town::show_picture(int number, int mode) {
    const int r = random_.next();
    const int chance = flags_[flag::kPictureChance];
    // DGROUP:0ac4, the chance masks, was part of BTECH.EXE; it is now
    // imported (tables.json: picture_chance).
    const auto& masks = tables_.picture_chance;
    bool show = chance >= 0 && chance < static_cast<int>(masks.size()) && (r & masks[std::size_t(chance)]) == 0;
    if (number != 0x10 && (number == 0 || number > 7 || number == 6 || number == 2)) show = true;
    if (mode == 2) show = true;
    if (!show) return;
    // The frames play with their delays (formats/anm.h); the picture then
    // stays as its last frame shows it. Mode 0 (a story picture) waits 50
    // frames after it (none for picture 8) and clears the box again.
    if (mode == 0) {
        picture_ = PlacePicture{number};
        if (number == 0) play_sound(12);  // 1000:19bf
        hold_screen(number == 8 ? 0 : 50);
        text_.select_window(kPanelPictureWindow);
        text_.clear_window();
        picture_.reset();
        return;
    }
    text_.forget(1, 1, 11, 11);
    picture_ = PlacePicture{number};
}

}  // namespace chr::game
