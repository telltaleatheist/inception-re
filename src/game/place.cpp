// Inside a place: the entrance routine, the .BLD script interpreter and the
// special actions the Citadel uses (docs/scripts.md). The script's bytes come
// from the player's .BLD at run time; every text it prints is looked up by
// its text ID (bld.<file>.<offset>), so the words come from the import.

#include <cstdio>
#include <stdexcept>

#include "formats/anm.h"
#include "formats/bld.h"
#include "formats/game_files.h"
#include "game/movement.h"
#include "game/town.h"
#include "game/training_quiz.h"

namespace chr::game {

namespace {

constexpr int kPanelPictureWindow = 4;
constexpr int kFirstFrameWait = 0x32;  // 1000:4a3d
constexpr int kStoryPictureWait = 0x3c;  // 1000:4a85
constexpr int kRepeatedPicture = 8, kRepeatedPlays = 7;

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
    if (file == 0 && flags_[flag::kQuizLock] == 0 && !demo_) {
        if (show_quiz_ && rules_) {
            if (!show_quiz(keys)) return false;
        } else {
            const auto parts = draw_quiz_parts(random_);
            log_.push_back("training quiz: parts " + std::to_string(parts[0]) + " " + std::to_string(parts[1]) +
                           " " + std::to_string(parts[2]) + " (not shown)");
        }
        // 1c67:0b3b: the screen cleared (287f:1fbe), the panel, the party box
        // cleared, then the box above it.
        text_.clear_all();
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
    draw_panel(true);  // 17dc:017e, 1000:4cac(1)
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
            // Not while the demo plays (17dc:04a8, F:3938).
            if (!demo_) show_picture(sbyte(pc + 1), sbyte(pc + 2));
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
    // 2656:0ba6: while the demo plays (F:3938) the remembered item is set
    // to the first, so the recorded keys find every menu the same.
    if (demo_) last = 0;
    int chosen =start && *start >= 0 && *start < rec.items ? *start : last;
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
        draw_panel(true);  // 1000:4cac(1); window 4 not cleared, before or after
        // 1000:a960: a draw for every poll with no key waiting; the flush
        // (1000:2a2b) eats the key that ends it, and another is read. While
        // the demo plays (19b8:0dd6, F:3938) there is no wait: its poll
        // finds a key at once, and the read takes the demo's next.
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
// item, the settings, the inspect item, the healing, the load, the save and
// the overhead map are rebuilt.
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
    case 3:
        if (!inspect(keys)) return false;  // 1000:378d
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
    draw_panel(true);  // 1000:4cac(1); window 4 not cleared
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

// BTECH.EXE 1000:378d (Ghidra FUN_1000_378d)
// The command menu's inspect item, in window 6: with 'Mechs, whether to
// inspect a 'Mech (its chooser, then the stats screen); otherwise a
// character, chosen when the party has more than Jason, shown on one page:
// name, weapon, armour, condition, the double agent's tell or the armour's
// state, the seven skills and the three kits; then a key.
bool Town::inspect(KeySource& keys) {
    // 1000:2867: text at (col, row) of the current window, in white.
    const auto print_at = [this](const std::string& id, int col, int row) {
        text_.set_cursor(col, row);
        text_.set_fg(TextRole::Text);
        print_id(id);
    };
    text_.select_window(6);
    clear_window();
    text_.frame(0);
    special(17, keys);  // flag 10 = 'Mech records 0-3 in use
    if (flags_[0x10] != 0) {
        print_id("inspect.mech_or_person");
        const auto mech = yes_no(true, keys);  // 1000:1a13(1)
        if (!mech) {
            place_open_ = true;
            return false;
        }
        if (*mech) {
            clear_window();
            print_id("inspect.which_mech");
            const auto m = choose_mech(keys);  // 1c67:0b98, into DG:0068
            if (!m) {
                place_open_ = true;
                return false;
            }
            combat_.heat[std::size_t(*m)] = 0;  // F:006e + m
            if (!mech_stats(*m, keys)) return false;  // 15ab:1afe
            draw_panel(true);  // 1000:4cac(1)
            clear_window();    // window 4, as the stats screen left it
            return true;
        }
    }
    special(14, keys);  // flag 0e = members besides Jason
    if (flags_[flag::kMembers] != 0) {
        clear_window();
        print_id("inspect.which_character");
        if (!special(13, keys)) {  // flag 0e = the chosen character
            place_open_ = true;
            return false;
        }
    }
    const int c = std::int8_t(flags_[flag::kMembers]);
    if (c < 0 || c >= 8) {
        log_.push_back("the inspect screen: a character number past the records: not rebuilt");
        return true;
    }
    const auto& r = characters_[std::size_t(c)];
    clear_window();
    text_.set_fg(TextRole::Label);  // 1000:28a2
    print_id("inspect.labels");
    print_id("inspect.labels_2");
    const std::string name = "character." + std::to_string(r[character::kName]);
    print_at(name, 8, 0);
    print_at(formats::weapon_text_id(std::int8_t(r[character::kWeapon])), 8, 1);
    const int armour = std::int8_t(r[character::kArmourType]);
    const int full = armour >= 0 && armour < int(rules_->combat.personal_armour.size())
                         ? std::int8_t(rules_->combat.personal_armour[std::size_t(armour)])
                         : -1;
    if (full < 0) {
        log_.push_back("the inspect screen: an armour type past the table: not rebuilt");
        return true;
    }
    print_at("armour." + std::to_string(armour), 8, 2);
    // The name and the condition in the window from column 8 on.
    text_.indent_window(8);
    print_at(name, 0, 4);
    const bool condition = print_condition(c);  // 1e31:02e4
    text_.indent_window(-8);
    if (!condition) return true;
    if (flags_[0x27] != 0 && std::int8_t(flags_[0x25]) == c) {
        text_.set_fg(TextRole::Unease);
        text_.set_cursor(0, 8);
        print_id("inspect.uneasy");
        text_.set_fg(TextRole::Text);
        // 273d:086a: the pause and the keyboard flushed.
        agent_found_ = true;  // F:374a
    } else if (armour != 0) {
        print_at("inspect.armour", 0, 8);
        const int points = std::int8_t(r[character::kArmour]);
        if (full == points) {
            print_id("inspect.armour_whole");
        } else {
            print_id("inspect.armour_lost");
            if (points == 0) {
                print_id("inspect.armour_all");
            } else {
                text_.set_fg(TextRole::Amount);  // 1000:28a2
                text_.print(std::to_string(full - points));  // 273d:0053
            }
            text_.set_fg(TextRole::Text);
            print_id("inspect.armour_of");
            text_.print(std::to_string(full));
            print_id("inspect.armour_points");
        }
    }
    for (int i = 0; i < 7; ++i) {
        const int level = std::min<int>(std::int8_t(r[std::size_t(character::kSkills + i)]), 4);
        if (level < 0) {
            log_.push_back("the inspect screen: a skill below the table: not rebuilt");
            return true;
        }
        print_at("inspect.skill_level." + std::to_string(level), 12, 13 + i);
    }
    print_at(kits_[0] != 0 ? "inspect.medkit_yes" : "inspect.medkit_no", 8, 21);
    print_at(flags_[flag::kMapper] != 0 ? "inspect.mapper_yes" : "inspect.mapper_no", 20, 21);
    print_at(kits_[1] != 0 ? "inspect.surgery_kit_yes" : "inspect.surgery_kit_no", 19, 22);
    if (!read_key(keys)) {  // 273d:086a, 273d:0259
        place_open_ = true;
        return false;
    }
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
    case 42:
        // 24d3:1515: the audience leaves the theatre. Each townsperson i
        // stands where the party is (its figure and its record's +0/+2),
        // last in and bound for entrance 7, inside for i rounds; its target
        // (+4/+6) is left as it was. Then the party one half-cell south (a
        // plain add to the y word).
        for (int i = 0; i < 8; ++i) {
            auto& w = people_.walkers()[std::size_t(i)];
            w.at = position_;
            people_.kept()[std::size_t(i)] = position_;
            w.route = 0x77;
            w.wait = std::uint8_t(i);
            figs_.x[std::size_t(16 + i)] = position_.x;
            figs_.y[std::size_t(16 + i)] = position_.y;
        }
        position_.y = std::uint16_t(position_.y + 1);
        return true;
    case 10:
        // 24d3:096f: the money in decimal at the cursor, then shop.cbills_end.
        text_.set_fg(TextRole::Funds);
        text_.print(std::to_string(money_.cbills));
        text_.print(lookup(texts_, "shop.cbills_end"));
        return true;
    case 6:
        comstar_account();
        return true;
    case 7:
    case 8:
        return comstar_move(n == 7, keys);
    case 9:
    case 11:
        return buy(n, keys);
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
    case 25:
    case 26:
        // 24d3:0dc2 and 0dcf: HOSPITAL's MedKit (F:d450) and field surgery
        // kit (F:d451), bought.
        kits_[std::size_t(n - 25)] = 1;
        return true;
    case 28:
        // 24d3:0ddc: GARAGE keeps the party's 'Mechs. Each of records 0-3
        // leaves its name's letter in F:d452 + m and is emptied; characters
        // m and m + 4 are put on foot.
        for (int m = 0; m < 4; ++m) {
            auto& mech = mechs_[std::size_t(m)];
            held_mechs_[std::size_t(m)] = mech[0];
            mech[0] = 0xff;
            characters_[std::size_t(m)][character::kMech] = character::kOnFoot;
            characters_[std::size_t(m + 4)][character::kMech] = character::kOnFoot;
        }
        return true;
    case 18:
        return repair_shop(keys);
    case 44:
        return ammo_shop(keys);
    case 19:
        return upgrade_offer(keys);
    case 20:
        return upgrade_buy(keys);
    case 24:
        return hospital_treatment(keys);
    case 41:
        return weapon_trade(keys);
    case 36: {
        // 24d3:13b2: the name of the character in record F:d331, through
        // special 38's tail (13ce).
        const int who = flags_[0x25];
        if (who >= int(characters_.size())) {
            log_.push_back("special 36: a record past the characters: not rebuilt");
            return true;
        }
        print_id("character." + std::to_string(characters_[std::size_t(who)][character::kName]));
        return true;
    }
    case 37:
        // 24d3:139a: the game is over: DG:014a = 0 and Jason's record
        // emptied. The main loop's next pass shows it (1000:04a4).
        combat_.fight_on = false;
        characters_[0][character::kName] = character::kEmpty;
        return true;
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
        text_.clear_all();  // 287f:1fbe
        draw_panel(true);   // 1000:4cac(1)
        clear_window();    // 2656:0388
        return true;
    case 27:
        // 24d3:0e54: special 46 without its first step (docs/scripts.md).
        for (int m = 0; m < 4; ++m)
            figs_.sprite_base[std::size_t(m)] = mechs_[std::size_t(m)][0] == 'L' ? 0 : 0x92;
        return crew_screen(0, keys);
    case 33:
        arena_rent();
        return true;
    case 34:
        return arena_own(keys);
    case 35:
        return arena_fight(keys);
    case 47: {
        // 24d3:174e: Jason loses 4 hit points if he has more than 5 (a
        // signed byte compare); the panel is not redrawn.
        auto& hits = characters_[0][character::kHitPoints];
        if (std::int8_t(hits) > 5) hits = std::uint8_t(hits - 4);
        return true;
    }
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
    case 31:
        // 24d3:0eda: flag 1f = the armour type of the character in flag 0e.
        flags_[0x1f] = characters_[std::size_t(flags_[flag::kMembers])][character::kArmourType];
        return true;
    case 32:
        return top_up_armour(keys);
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

namespace {
constexpr std::size_t kMechPilot = 0x79, kMechRider = 0x7a, kMechSprites = 0x7b;
}  // namespace

// 17dc:1c9b and 17dc:1a26's common part: characters 1-7 out of the fight,
// their first bytes kept (F:3fe9 + c), and every 'Mech byte of 0-7 set to 3.
void Town::arena_members_out() {
    for (int c = 1; c < 8; ++c) {
        characters_[std::size_t(c)][character::kMech] = 3;
        characters_[std::size_t(c - 1)][character::kMech] = 3;
        arena_kept_first_[std::size_t(c)] = characters_[std::size_t(c)][character::kName];
        characters_[std::size_t(c)][character::kName] = character::kEmpty;
    }
}

// BTECH.EXE 17dc:1c9b (Ghidra FUN_17dc_1c9b), special 33
// The Arena's 'Mech: record 0 kept (F:3780) and made a LOCUST (27e8:02f0),
// the others out, and the spectator on (F:e48e).
void Town::arena_rent() {
    arena_kept_record_ = mechs_[0];
    mechs_[0] = rules_->mechs[0].record;
    arena_members_out();
    combat_.spectator = true;
}

// BTECH.EXE 17dc:1a26 (Ghidra FUN_17dc_1a26), special 34
// The party's own 'Mech: every seat kept, the 'Mech chosen, record 0 kept
// and made a copy of it named by its held letter, the others out, Jason
// alone in it, and no spectator.
bool Town::arena_own(KeySource& keys) {
    for (std::size_t m = 0; m < 4; ++m) {
        arena_kept_pilot_[m] = mechs_[m][kMechPilot];  // F:430e
        arena_kept_rider_[m] = mechs_[m][kMechRider];  // F:3ffa
    }
    clear_window();  // 2656:0388
    print_id("arena.which_mech");
    const auto chosen = choose_mech(keys);
    if (!chosen) return false;
    arena_choice_ = *chosen;
    arena_kept_record_ = mechs_[0];
    mechs_[0] = mechs_[std::size_t(arena_choice_)];
    mechs_[0][0] = held_mechs_[std::size_t(arena_choice_)];
    arena_members_out();
    mechs_[0][kMechPilot] = 0;
    mechs_[0][kMechRider] = character::kEmpty;
    combat_.spectator = false;
    return true;
}

// BTECH.EXE 1c67:0b98 (Ghidra FUN_1c67_0b98)
// A 'Mech chosen from records 0-3 in use, or with flag 18 those with a held
// letter: one menu line each, the pilot's name, a separator and the 'Mech's
// name. The pilot's name is read by the record's number as a name number
// (DG:01ca + 4n), as the original does.
std::optional<int> Town::choose_mech(KeySource& keys) {
    const bool held = flags_[0x18] != 0;
    std::vector<int> listed;  // F:d558
    for (int m = 0; m < 4; ++m)
        if ((held ? held_mechs_[std::size_t(m)] : mechs_[std::size_t(m)][0]) != 0xff) listed.push_back(m);
    auto& rec = menus_[0x17];
    rec.first_row = text_.window().row;  // DG:0202 = F:374e
    for (const int m : listed) {
        const auto& r = mechs_[std::size_t(m)];
        const int pilot = r[kMechPilot];
        if (pilot >= 8) {  // character.0-7
            log_.push_back("the 'Mech chooser: a pilot number past the names: not rebuilt");
            return std::nullopt;
        }
        print_id("character." + std::to_string(pilot));
        print_id("arena.name_sep");
        auto named = r;
        if (held) named[0] = held_mechs_[std::size_t(m)];
        if (const auto id = template_name_id(named)) print_id(*id);
        else log_.push_back("a 'Mech record with no template's name");
        print_id("talk.line_end");  // 24d3:181e
    }
    rec.items = int(listed.size());  // DG:0206
    menu_last_[0x17] = 0;            // DG:0208
    const auto chosen = menu(0x17, keys);
    rec.first_row = 1;
    if (!chosen) return std::nullopt;
    return listed.at(std::size_t(*chosen));
}

// BTECH.EXE 19b8:137f and 19b8:1441 (Ghidra FUN_19b8_137f, FUN_19b8_1441)
// The Arena's wall patch (DG:0010, drawn by `build`): its map-store cells,
// built as a wall, or afterwards as floor (fixed ends, one draw a middle cell).
void Town::arena_patch(bool build) {
    struct Patch {
        std::size_t first, last;
        std::vector<std::size_t> middle;
        int wall_first, wall_middle, floor_first, floor_last;
    };
    static const std::array<Patch, 3> kPatches = {{
        {0xc72, 0xcb0, {0xc73, 0xc74, 0xc75, 0xc76, 0xc77}, 0x6f, 0x6a, 0x43, 0x42},
        {0xa5b, 0xa9e, {0xa5c, 0xa5d, 0xa5e, 0xa5f, 0xa98, 0xa99, 0xa9a, 0xa9b, 0xa9c, 0xa9d}, 0x6f, 0x6a, 0x41, 0x42},
        {0xabc, 0xcbc, {0xc84, 0xc8c, 0xc94, 0xc9c, 0xca4, 0xcac, 0xcb4}, 0x71, 0x6b, 0x40, 0x41},
    }};
    if (build) arena_patch_ = random_.next() & 3;
    if (arena_patch_ == 0) return;
    const auto& p = kPatches[std::size_t(arena_patch_ - 1)];
    const auto put = [this](std::size_t cell, int tile) {
        if (cell < map_.blocks.size()) set_map_cell(cell, tile, false);
    };
    put(p.first, build ? p.wall_first : p.floor_first);
    for (const auto cell : p.middle) put(cell, build ? p.wall_middle : (random_.next() & 3) + 0x40);
    put(p.last, build ? 0x70 : p.floor_last);
}

// BTECH.EXE 24d3:1062, special 35 (part of Ghidra FUN_24d3_0004)
// The Arena fight, then either the 'Mechs put back (17dc:1b41) or, when the
// spectator fell and the exit zone was reached, the theft: the party
// outside the town to the west (docs/scripts.md, "The Arena").
bool Town::arena_fight(KeySource& keys) {
    figs_.sprite_base[0] = mechs_[0][kMechSprites] != 0 ? 0x92 : 0;  // F:d55e
    characters_[0][character::kMech] = 0;
    mechs_[0][kMechPilot] = 0;
    show_picture(0, 0);  // 1000:48b7(0, 0)
    position_ = {0x0a2b, 0x8057};
    animation_.start_script(0, FigureAnimation::mech_script(6));  // P:01f6 = 27e8:02a0
    animation_.advance(0);                                        // DG:409a = 1000:1732(0)
    arena_patch(true);
    if (!run_mission(8, keys)) return false;  // 17dc:0629(8)
    // The fires (marks 0x7c and 0x7d) in the Arena's area lose all but y.
    for (auto& m : marks_.marks)
        if ((m.code & 0x7e) == 0x7c && m.area == 0x8a) m.code = m.area = m.x = 0;
    if (!combat_.fight_on) flags_[0x28] = 1;  // DG:014a = 0: the leader fell
    if (flags_[0x23] == 0) {
        // The cell window keeps the wall until 287f:1314 next builds it.
        terrain_.hold_window();
        arena_patch(false);
        arena_put_back();
        position_ = {0x0a39, 0x805d};
    } else {
        arena_theft();
    }
    combat_.spectator = false;  // F:e48e
    return true;
}

// BTECH.EXE 17dc:1b41 (Ghidra FUN_17dc_1b41)
// After an Arena fight with no theft: the rented 'Mech handed back, or the
// party's own copied back to its record, which is then marked empty (its
// held letter kept unless it was destroyed), and the seats and members back.
void Town::arena_put_back() {
    if (combat_.spectator) {
        mechs_[0] = arena_kept_record_;
    } else {
        const auto m = std::size_t(arena_choice_);
        mechs_[m] = mechs_[0];
        if (mechs_[0][0] == 0xff) held_mechs_[m] = 0xff;
        mechs_[m][0] = 0xff;
        if (m != 0) mechs_[0] = arena_kept_record_;
        for (std::size_t k = 0; k < 4; ++k) {
            mechs_[k][kMechPilot] = arena_kept_pilot_[k];
            mechs_[k][kMechRider] = arena_kept_rider_[k];
        }
    }
    for (int c = 1; c < 8; ++c) {
        characters_[std::size_t(c)][character::kMech] = character::kOnFoot;
        characters_[std::size_t(c - 1)][character::kMech] = character::kOnFoot;
        characters_[std::size_t(c)][character::kName] = arena_kept_first_[std::size_t(c)];
    }
}

// 24d3:1062's theft branch (24d3:111b..1352)
void Town::arena_theft() {
    position_.x = 0x0970;
    position_.y = figs_.active[0] != 0 ? figs_.y[0] : figs_.y[4];  // F:4036, or F:403e
    for (int c = 1; c < 8; ++c) characters_[std::size_t(c)][character::kName] = arena_kept_first_[std::size_t(c)];
    bool all_free = true;
    for (const auto letter : held_mechs_) all_free = all_free && letter == 0xff;
    if (!all_free) {
        bool placed = false;
        for (std::size_t i = 1; i < 4 && !placed; ++i) {
            if (held_mechs_[i] != 0xff) continue;
            placed = true;
            for (std::size_t j = 1; j < 4; ++j) mechs_[j][0] = held_mechs_[j];
            mechs_[i] = arena_kept_record_;
            mechs_[i][0] = held_mechs_[0];
        }
        if (!placed) {
            mechs_[0] = arena_kept_record_;
            for (std::size_t m = 0; m < 4; ++m) mechs_[m][0] = held_mechs_[m];
        }
    }
    // The nine areas around area 0x89, west of the town (287f:104e, 1000:2da8, 287f:1da8).
    constexpr int kArea = 0x89;
    terrain_.hold_window();  // the cell window stays until 287f:1314 next builds it
    terrain_.generate(kArea);
    for (int k = 0; k < 9; ++k) {
        const int a = kArea - 0x11 + 16 * (k / 3) + k % 3;
        if (area_maps_[std::size_t(a)] != 0) load_town_map(area_maps_[std::size_t(a)], k);
    }
    terrain_.link_blocks();
    arena_wall(false);  // 1d43:0c72(0)
    for (std::size_t m = 0; m < 4; ++m) {
        held_mechs_[m] = 0xff;
        mechs_[m][kMechPilot] = mechs_[m][kMechRider] = character::kEmpty;
    }
    for (int c = 0; c < 8; ++c) characters_[std::size_t(c)][character::kMech] = character::kOnFoot;
}

// A right-aligned amount, ending in screen column 38 (273d:00d5 at column
// 39 less its length), in the money's colour on 0.
void Town::put_amount(std::uint32_t amount, int row) {
    const auto digits = std::to_string(amount);  // 287f:3bd2
    text_.put_text(digits, 39 - int(digits.size()), row, TextRole::Funds, TextRole::Background);
}

// BTECH.EXE 24d3:037a, special 6 (part of Ghidra FUN_24d3_0004)
// ComStar's account: the money on row 1, then each investment that is not 0,
// its name and amount; F:e482 counts them (docs/scripts.md, "ComStar").
void Town::comstar_account() {
    put_amount(std::uint32_t(money_.cbills), 1);
    investments_shown_ = 0;  // F:e482
    for (int i = 0; i < 3; ++i) {
        if (money_.invested[std::size_t(i)] == 0) continue;
        text_.set_fg(TextRole::Text);  // F:37fe = 15
        print_id("comstar.investment." + std::to_string(i));
        put_amount(std::uint32_t(money_.invested[std::size_t(i)]), text_.window().row);  // a window row as a screen row
        ++investments_shown_;
    }
    if (investments_shown_ == 0) print_id("comstar.none");
}

// BTECH.EXE 24d3:0725 and 24d3:04ad, specials 7 and 8 (part of Ghidra
// FUN_24d3_0004)
// ComStar's investing (from the money into an investment) and withdrawing
// (back), the amount typed in the number field (docs/scripts.md, "ComStar").
bool Town::comstar_move(bool invest, KeySource& keys) {
    const std::string w = invest ? "comstar.invest_" : "comstar.withdraw_";
    if (invest ? money_.cbills == 0 : investments_shown_ == 0) {
        print_id(invest ? "comstar.no_money" : "comstar.nothing_invested");
        return true;
    }
    clear_window();  // 2656:0388
    print_id(w + "title");
    for (int i = 0; i < 3; ++i) print_id("comstar.investment." + std::to_string(i));
    const auto chosen = menu(7, keys);
    if (!chosen) {
        place_open_ = true;
        return false;
    }
    const int i = *chosen;
    if (i < 0 || i > 2) {
        log_.push_back("special " + std::to_string(invest ? 7 : 8) + ": item " + std::to_string(i) + " past the investments");
        return true;
    }
    auto& investment = money_.invested[std::size_t(i)];
    if (!invest && investment == 0) {
        print_id("comstar.withdraw_empty");
        return true;
    }
    const std::uint32_t most = std::uint32_t(invest ? money_.cbills : investment);
    print_id(w + "how_much");
    text_.put_text(lookup(texts_, invest ? "comstar.label_cbills" : "comstar.label_invested"), text_.window().x, 23,
                   TextRole::Text, TextRole::Background);
    put_amount(most, 23);
    const auto typed = number_field(keys);
    if (!typed) return false;
    std::uint32_t amount = *typed;
    if (amount > most) {
        amount = most;
        print_id(w + "capped");
    }
    if (amount == 0) {
        print_id(w + "none");
        print_id("comstar.cancelled");  // 17dc:19e1
        return true;
    }
    print_id(w + "done");
    text_.set_fg(TextRole::Funds);
    text_.print(std::to_string(amount));
    print_id(invest ? "comstar.invest_in" : "comstar.withdraw_from");
    print_id("comstar.investment." + std::to_string(i));
    print_id(invest ? "comstar.invest_total" : "comstar.withdraw_left");
    if (invest) {
        investment = std::int32_t(std::uint32_t(investment) + amount);
        money_.cbills = std::int32_t(std::uint32_t(money_.cbills) - amount);
    } else {
        investment = std::int32_t(std::uint32_t(investment) - amount);
        money_.cbills = std::int32_t(std::uint32_t(money_.cbills) + amount);
    }
    text_.set_fg(TextRole::Funds);
    text_.print(std::to_string(std::uint32_t(investment)));
    print_id(w + "end");
    draw_cbills();  // 1e31:1fdf
    return true;
}

// BTECH.EXE 1d43:0cde (Ghidra FUN_1d43_0cde)
// A whole number typed at the cursor, up to 7 digits, until Enter
// (docs/scripts.md, "The number field").
std::optional<std::uint32_t> Town::number_field(KeySource& keys) {
    std::string digits = "0";
    const int col = text_.window().col, row = text_.window().row;
    for (;;) {
        text_.set_cursor(col, row);
        text_.set_fg(TextRole::Funds);  // F:37fe = 2
        text_.print(digits);
        const auto& win = text_.window();
        if (win.col < win.width)  // 273d:01fb: the rest of the row
            text_.fill(win.x + win.col, win.y + win.row, win.width - win.col, 1, TextRole::Background);
        const auto k = read_key(keys);  // 273d:0259
        if (!k) return std::nullopt;
        if (*k == key::kEnter) break;
        if (*k >= '0' && *k <= '9') {
            if (digits.size() < 7) {
                while (!digits.empty() && digits[0] == '0') digits.erase(0, 1);
                digits += char(*k);
            }
        } else if (*k == key::kEscape) {
            digits = "0";
        } else if (*k == key::kBackspace) {
            if (!digits.empty()) digits.pop_back();
        }
    }
    std::uint32_t value = 0;
    for (const char c : digits) value = value * 10 + std::uint32_t(c - '0');
    text_.set_fg(TextRole::Text);  // F:37fe = 15
    return value;
}

// BTECH.EXE 24d3:09c7 and 24d3:0a4b, specials 9 and 11 (part of Ghidra
// FUN_24d3_0004)
// ARMOR's and WEAPON's purchase: the choice from the menus' remembered
// items, paid for if the money covers it (flag 0b), then offered round the
// party (docs/scripts.md, "The shops").
bool Town::buy(int n, KeySource& keys) {
    if (!rules_) {
        log_.push_back("special " + std::to_string(n) + ": no rules");
        return true;
    }
    const auto& shops = rules_->shops;
    const bool armour = n == 9;
    int choice;
    if (armour) {
        choice = menu_last_[0x0e];
        if (flags_[flag::kInPlace] != 0) choice = menu_last_[0x0d];
    } else {
        choice = menu_last_[0x10];
        if (flags_[0x0c] == 6) choice = menu_last_[0x11] + 6;
        if (flags_[0x0c] == 9) choice = menu_last_[0x12] + 9;
        --choice;  // the price and the weapon by number - 1
    }
    flags_[0x0b] = 0;
    const std::size_t count = armour ? shops.armour.size() : shops.weapon.size();
    if (choice < 0 || std::size_t(choice) >= count) {
        log_.push_back("special " + std::to_string(n) + ": choice " + std::to_string(choice) + " past the table");
        return true;
    }
    const std::uint32_t price = armour ? shops.armour[std::size_t(choice)] : shops.weapon[std::size_t(choice)];
    if (static_cast<std::uint32_t>(money_.cbills) < price) return true;
    money_.cbills = std::int32_t(static_cast<std::uint32_t>(money_.cbills) - price);
    flags_[0x0b] = 1;
    draw_cbills();  // 1e31:1fdf
    if (armour) return offer_armour(choice, std::int8_t(rules_->combat.personal_armour[std::size_t(choice)]), keys);
    return offer_weapon(choice + 1, keys);
}

// BTECH.EXE 19b8:0002 (no Ghidra function), special 18
// REPAIR's repair shop for one 'Mech (docs/scripts.md, "The repair shop"):
// armour at 4 C-Bills a point, internal structure at 9, heat sinks at 800,
// weapons at 300 a type from a menu, the hip actuators at 200; each offered
// with its price and bought while the money lasts.
bool Town::repair_shop(KeySource& keys) {
    const auto quit = [this] {
        place_open_ = true;
        return false;
    };
    const auto number = [this](int n) { text_.print(std::to_string(n)); };  // 273d:0053
    const auto can_pay = [this](std::uint32_t price) { return std::uint32_t(money_.cbills) >= price; };
    const auto pay = [this](std::uint32_t price) {
        money_.cbills = std::int32_t(std::uint32_t(money_.cbills) - price);
        draw_cbills();  // 1e31:1fdf
    };
    const auto ask = [&](const char* question) -> std::optional<bool> {
        special(10, keys);  // the money
        print_id(question);
        return yes_no(true, keys);  // 1000:1a13(1)
    };
    const auto note = [&](const char* id, bool clear) {
        if (clear) clear_window();
        print_id(id);
        return read_key(keys).has_value();  // 273d:0259
    };

    clear_window();  // 2656:0388
    print_id("repair.which");
    const auto chosen = choose_mech(keys);  // 1c67:0b98, into DG:0068
    if (!chosen) return quit();
    arena_choice_ = *chosen;
    auto& r = mechs_[std::size_t(*chosen)];
    constexpr int kFull = 0x45;  // each armour and structure byte's full value is 0x45 bytes on
    const auto missing = [&](int b) { return int(r[std::size_t(b + kFull)]) - int(r[std::size_t(b)]); };

    // What needs mending.
    int armour = 0, structure = 0, sinks = 0, actuators = 0, weapons = 0;
    std::array<int, 0x11> broken{};  // destroyed weapons by slot code 0x10-0x20
    for (int b = 0x11; b <= 0x1b; ++b) armour += missing(b);
    for (int b = 0x1c; b <= 0x23; ++b) structure += missing(b);
    for (int b = 0x33; b <= 0x55; ++b) {
        const int v = r[std::size_t(b)];
        if (!(v & 0x80)) continue;
        if ((v & 0x7f) == 0x22) ++sinks;
        if ((v & 0x7f) >= 0x10 && (v & 0x7f) <= 0x20) {
            ++broken[std::size_t((v & 0x7f) - 0x10)];
            weapons = 1;
        }
    }
    if (r[0x24] != r[0x69]) actuators = 1;
    if (r[0x25] != r[0x6a]) actuators |= 2;

    if (std::int16_t(weapons + structure + armour + sinks + actuators) == 0) {
        // Only the first damaged system (bytes 0x75-0x77) is named.
        for (int b = 0x75; b <= 0x77; ++b) {
            if (r[std::size_t(b)] == 0) continue;
            print_id("repair.systems");
            print_id("repair.system." + std::to_string(b - 0x75));  // DG:1b3e's pointers
            return read_key(keys) ? true : quit();
        }
        print_id("repair.nothing");
        return read_key(keys) ? true : quit();
    }
    if (money_.cbills == 0) {
        print_id("repair.no_money");
        print_id("course.no_money_end");  // 24d3:1809
        return read_key(keys) ? true : quit();
    }

    // A point at a time over bytes first..last while the money lasts; a
    // byte it cannot pay for is left, and the next byte is tried.
    const auto mend_points = [&](int first, int last, std::uint32_t price, int& left) {
        for (int b = first; b <= last; ++b)
            for (auto n = std::int16_t(missing(b)); n != 0; n = std::int16_t(n - 1)) {
                if (!can_pay(price)) break;
                --left;
                ++r[std::size_t(b)];
                pay(price);
            }
    };
    if (armour != 0) {
        clear_window();
        print_id("repair.armour");
        number(armour);
        print_id("repair.armour_cost");
        number(4 * armour);
        print_id("repair.armour_cbills");
        const auto yes = ask("repair.armour_question");
        if (!yes) return quit();
        if (*yes) {
            mend_points(0x11, 0x1b, 4, armour);
            if (armour != 0 && !note("repair.armour_short", true)) return quit();
        }
    }
    if (structure != 0) {
        clear_window();
        print_id("repair.structure");
        print_id("repair.structure_2");
        print_id("repair.structure_cost");
        number(9 * structure);
        print_id("repair.structure_cbills");
        const auto yes = ask("repair.structure_question");
        if (!yes) return quit();
        if (*yes) {
            mend_points(0x1c, 0x23, 9, structure);
            if (structure != 0 && !note("repair.structure_short", true)) return quit();
        }
    }
    if (sinks != 0) {
        clear_window();
        print_id("repair.sinks");
        number(sinks);
        print_id("repair.sinks_word");
        if (sinks > 1) print_id("repair.sinks_plural");
        print_id("repair.sinks_cost");
        const auto yes = ask("repair.sinks_question");
        if (!yes) return quit();
        if (*yes) {
            for (int b = 0x33; b <= 0x55 && sinks != 0; ++b) {
                if (r[std::size_t(b)] != 0xa2 || !can_pay(800)) continue;
                --sinks;
                r[std::size_t(b)] = 0x22;
                pay(800);
            }
            if (sinks != 0 && !note("repair.sinks_short", true)) return quit();
        }
    }
    // The weapons: a menu of the destroyed types until Done, the money runs
    // short, or none is left. One payment mends every destroyed weapon of
    // the type, but its count goes down by one only, so a type with two
    // destroyed stays listed, and paying for it again mends nothing.
    while (weapons != 0) {
        clear_window();
        print_id("repair.weapons");
        std::vector<int> listed;
        for (int i = 0; i <= 0x10; ++i) {
            if (broken[std::size_t(i)] == 0) continue;
            print_id("talk.line_end");  // 24d3:181e
            number(broken[std::size_t(i)]);
            text_.set_cursor(text_.window().col + 1, text_.window().row);  // F:3748 + 1
            print_id(formats::weapon_text_id(0x10 + i - 1));  // slot code t names weapon t - 1
            listed.push_back(i);
        }
        auto& rec = menus_[0x17];
        rec.first_row = 2;
        rec.items = text_.window().row;  // F:374e: the listed types and Done
        menu_last_[0x17] = 0;
        print_id("repair.weapons_done");
        text_.set_cursor(0, 0x16);
        print_id("repair.weapons_cbills");
        special(10, keys);
        const auto k = menu(0x17, keys);  // 1656:0b5e(0x17)
        if (!k) return quit();
        if (*k == rec.items - 1) break;  // Done
        if (!can_pay(300)) {
            if (!note("repair.weapons_short", false)) return quit();
            break;
        }
        pay(300);
        const int i = listed.at(std::size_t(*k));
        broken[std::size_t(i)] = fixes_.fix_repair_type_count ? 0 : broken[std::size_t(i)] - 1;
        for (int b = 0x33; b <= 0x55; ++b)
            if (r[std::size_t(b)] == 0x90 + i) r[std::size_t(b)] &= 0x7f;
        // Any left: types 0x10-0x1f only, not 0x20 (bug 41).
        const auto checked = broken.begin() + (fixes_.fix_repair_last_type ? std::ptrdiff_t(broken.size()) : 0x10);
        weapons = std::any_of(broken.begin(), checked, [](int c) { return c != 0; }) ? 1 : 0;
        if (weapons == 0) {
            text_.set_cursor(0, 0x12);
            if (!note("repair.weapons_all", false)) return quit();
        }
    }
    if (actuators != 0) {
        clear_window();
        print_id("repair.actuators");
        if (!can_pay(200)) return note("repair.actuators_short", false) ? true : quit();
        print_id("repair.actuators_cost");
        print_id("repair.actuators_question");
        const auto yes = yes_no(true, keys);  // 1000:1a13(1)
        if (!yes) return quit();
        if (*yes) {
            print_id("repair.actuators_done");
            if (!read_key(keys)) return quit();
            r[0x25] = r[0x6a];
            r[0x24] = r[0x69];
            pay(200);
        }
    }
    return true;
}

// BTECH.EXE 19b8:1762 (Ghidra FUN_19b8_1762), special 44
// REPAIR's ammunition for one 'Mech: for each of its first ten weapons that
// takes ammunition, the shots missing and their price, then a number typed
// and that many shots (at most the missing ones) bought one at a time while
// the money lasts (docs/scripts.md, "The repair shop").
bool Town::ammo_shop(KeySource& keys) {
    const auto quit = [this] {
        place_open_ = true;
        return false;
    };
    clear_window();  // 2656:0388
    print_id("ammo.which");
    const auto chosen = choose_mech(keys);  // 1c67:0b98, into DG:0068
    if (!chosen) return quit();
    arena_choice_ = *chosen;
    auto& r = mechs_[std::size_t(*chosen)];

    // The weapon items in slot order, as stored (bit 0x80: destroyed).
    std::array<std::uint8_t, 10> w{};
    for (int b = 0x33, i = 0; b <= 0x55 && i < 10; ++b)
        if ((r[std::size_t(b)] & 0x7f) >= 0x10 && (r[std::size_t(b)] & 0x7f) <= 0x20) w[std::size_t(i++)] = r[std::size_t(b)];
    // Signed compares: a destroyed weapon never takes ammunition here.
    const auto takes_ammo = [](std::uint8_t v) { return v == 0x18 || std::int8_t(v) >= 0x1a; };
    const bool any = std::any_of(w.begin(), w.end(), takes_ammo);
    const bool destroyed = std::any_of(w.begin(), w.end(), [](std::uint8_t v) { return (v & 0x80) != 0; });

    if (!any) {
        print_id("ammo.none");  // 24d3:17ea
        if (!destroyed && !read_key(keys)) return quit();
    }
    for (int i = 0; any && i < 10; ++i) {
        const auto v = w[std::size_t(i)];
        if (!takes_ammo(v)) continue;
        clear_window();
        print_id("ammo.weapon");
        print_id(formats::weapon_text_id(v - 1));  // slot code t names weapon t - 1
        auto& shots = r[std::size_t(0x27 + i)];
        const auto full = r[std::size_t(0x6b + i)];
        if (shots == full) {
            print_id("ammo.full");  // 24d3:17ea
            if (!read_key(keys)) return quit();
            continue;
        }
        print_id("ammo.missing");
        int want = int(full) - int(shots);
        text_.set_fg(TextRole::Amount);  // 1000:28a2
        text_.print(std::to_string(want));
        print_id("ammo.price");
        text_.set_fg(TextRole::Amount);  // 1000:28a2
        const int price = v == 0x18 ? 2 : tables_.ammo_price[std::size_t(v - 0x1a)];
        text_.print(std::to_string(price));
        print_id("ammo.how_many");
        const auto typed = number_field(keys);  // 1d43:0cde
        if (!typed) return quit();
        if (*typed == 0) continue;
        // Compared as 32-bit signed numbers.
        if (std::int64_t(want) < std::int64_t(std::int32_t(*typed))) {
            print_id("ammo.too_many");  // 24d3:17ea
            if (!read_key(keys)) return quit();
        } else {
            want = int(*typed);
        }
        const auto cost = std::uint32_t(std::int32_t(std::int16_t(price)));
        for (; want != 0; want = std::int16_t(want - 1)) {
            if (std::uint32_t(money_.cbills) < cost) break;
            ++shots;
            money_.cbills = std::int32_t(std::uint32_t(money_.cbills) - cost);
            draw_cbills();  // 1e31:1fdf
        }
        if (want != 0) {
            print_id("ammo.short");  // 24d3:17ea
            if (!read_key(keys)) return quit();
        }
        draw_cbills();
    }
    if (destroyed) {
        print_id("ammo.destroyed");  // 24d3:17ea
        if (!read_key(keys)) return quit();
    }
    return true;
}

// BTECH.EXE 19b8:080a (Ghidra FUN_19b8_080a), special 19
// REPAIR's speed shop: a 'Mech chosen; with no upgrade left (byte 0x7c is
// 3) flag 12 = 0. Otherwise flag 12 = 1, the money shown, and flag 11 = the
// upgrade's case: the model (byte 0x7b, 0-3; any other is 8, none), + 4 for
// the second upgrade (byte 0x7c is 1). Its price is kept for special 20
// (F:0076, tables.upgrade_price). The script describes the case itself.
bool Town::upgrade_offer(KeySource& keys) {
    clear_window();  // 2656:0388
    print_id("upgrade.which");
    const auto chosen = choose_mech(keys);  // 1c67:0b98, into DG:0068
    if (!chosen) {
        place_open_ = true;
        return false;
    }
    arena_choice_ = *chosen;
    const auto& r = mechs_[std::size_t(*chosen)];
    if (r[0x7c] == 3) {
        clear_window();
        print_id("upgrade.none_left");
        flags_[0x12] = 0;
        return true;
    }
    flags_[0x12] = 1;
    clear_window();
    text_.set_cursor(0, 0x16);  // F:3748, F:374e
    print_id("upgrade.cbills");
    special(10, keys);  // the money
    text_.set_cursor(0, 0);
    int which = r[0x7b];
    if (which > 3) {
        which = 8;
    } else if (r[0x7c] == 1) {
        which += 4;
    }
    if (which > 7) which = 8;
    flags_[0x11] = std::uint8_t(which);
    upgrade_price_ = std::int16_t(tables_.upgrade_price[std::size_t(which)]);
    return true;
}

// BTECH.EXE 19b8:0925 (Ghidra FUN_19b8_0925), special 20
// The upgrade bought: its price asked and paid, then the case in flag 11
// writes its fixed item slots, ammunition bytes (ff: none) and, for some,
// armour; byte 0x7c becomes 1 after a first upgrade and gains 2 after a
// second (docs/combat.md 2.6).
bool Town::upgrade_buy(KeySource& keys) {
    const auto quit = [this] {
        place_open_ = true;
        return false;
    };
    print_id("upgrade.price");
    text_.print(std::to_string(upgrade_price_));  // 273d:0053
    print_id("upgrade.question");
    const auto yes = yes_no(true, keys);  // 1000:1a13(1)
    if (!yes) return quit();
    if (!*yes) return true;
    const auto price = std::uint32_t(std::int32_t(upgrade_price_));
    if (price > std::uint32_t(money_.cbills)) {
        print_id("talk.line_end");        // 24d3:181e
        print_id("talk.line_end");        // 24d3:181e
        print_id("course.no_money_end");  // 24d3:1809
        return read_key(keys) ? true : quit();
    }
    money_.cbills = std::int32_t(std::uint32_t(money_.cbills) - price);
    draw_cbills();  // 1e31:1fdf
    auto& r = mechs_[std::size_t(arena_choice_)];  // DG:0068
    const auto set = [&](std::initializer_list<int> bytes, std::uint8_t v) {
        for (const int b : bytes) r[std::size_t(b)] = v;
    };
    const auto no_ammo = [&](int first, int count) {  // weapons first.. take none
        for (int k = 0; k < count; ++k) r[std::size_t(0x6b + first + k)] = r[std::size_t(0x27 + first + k)] = 0xff;
    };
    const auto& locust = rules_ ? rules_->mechs[0].record : formats::MechRecord{};  // 37e8:02f0
    switch (flags_[0x11]) {
    case 0:
        set({0x41, 0x33}, 0x11);
        no_ammo(0, 2);
        r[0x7c] = 1;
        break;
    case 1:
        r[0x4f] = 0x11;
        no_ammo(1, 1);
        for (int b = 0x11; b <= 0x1b; ++b) {
            r[std::size_t(b)] = locust[std::size_t(b)];
            r[std::size_t(b + 0x45)] = locust[std::size_t(b + 0x45)];
        }
        r[0x7c] = 1;
        break;
    case 2:
        set({0x43, 0x42, 0x34, 0x33}, 0x10);
        no_ammo(0, 5);
        r[0x7c] = 1;
        break;
    case 3:
        set({0x4a, 0x41}, 0x11);
        r[0x2a] = r[0x29];
        r[0x6e] = r[0x6d];
        no_ammo(1, 2);
        for (int b = 0x11; b <= 0x1b; ++b)
            r[std::size_t(b + 0x45)] = r[std::size_t(b)] = std::uint8_t(tables_.upgrade_armour[std::size_t(b - 0x11)]);
        r[0x7c] = 1;
        break;
    case 4:
        r[0x31] = 7;
        r[0x75] = 0;
        set({0x49, 0x3b}, 0x10);
        no_ammo(3, 2);
        r[0x7c] |= 2;
        break;
    case 5:
        r[0x32] = 0;
        set({0x53, 0x33, 0x55}, 0x11);
        no_ammo(2, 3);
        r[0x7c] |= 2;
        break;
    case 6:
        r[0x32] = 0;
        for (int b = 0x11; b <= 0x1b; ++b) r[std::size_t(b + 0x45)] = r[std::size_t(b)] = locust[std::size_t(b)];
        set({0x54, 0x53}, 0x11);
        no_ammo(5, 2);
        r[0x7c] |= 2;
        break;
    case 7:
        for (int b = 0x4f; b < 0x55; ++b) r[std::size_t(b)] = 0x10;
        r[0x55] = 0x11;
        no_ammo(0, 10);
        r[0x7c] |= 2;
        break;
    default:
        break;  // no case: paid for, nothing applied (the script never gets here)
    }
    print_id("upgrade.done");
    return read_key(keys) ? true : quit();
}

// BTECH.EXE 24d3:0d44, special 24 (part of Ghidra FUN_24d3_0004)
// HOSPITAL's treatment: flag 1a is the care bought (1-7), priced by
// tables.treatment_cost; then the healing with that doctor's level. With no
// care chosen, the healing as after a fight.
bool Town::hospital_treatment(KeySource& keys) {
    const int level = flags_[0x1a];
    if (level == 0) return heal(0, keys);  // 1c31:000a(0)
    const auto& costs = tables_.treatment_cost;
    if (level >= int(costs.size())) {
        log_.push_back("special 24: a treatment past the table: not rebuilt");
        return true;
    }
    // The price sign-extended, compared unsigned with the C-Bills.
    const auto cost = std::uint32_t(std::int32_t(std::int16_t(costs[std::size_t(level)])));
    if (std::uint32_t(money_.cbills) >= cost) {
        money_.cbills = std::int32_t(std::uint32_t(money_.cbills) - cost);
        if (!heal(level, keys)) return false;  // 1c31:000a(level)
        draw_cbills();                         // 24d3:0961: 1e31:1fdf
        return true;
    }
    print_id("talk.line_end");        // 24d3:181e
    print_id("course.no_money_end");  // 24d3:1809
    if (!read_key(keys)) {            // 273d:086a, 273d:0259
        place_open_ = true;
        return false;
    }
    money_.cbills += 25;
    draw_cbills();
    return true;
}

// BTECH.EXE 24d3:140d, special 41 (part of Ghidra FUN_24d3_0004)
// WEAPON2: with members besides Jason (flag 0e), weapon 13 offered once
// and, for each member, weapon 13 or 12 by a random bit; alone, Jason is
// asked to trade his own weapon for weapon 13. Then every character in
// use has his hit points capped at B x (6 or 7), a random bit each.
bool Town::weapon_trade(KeySource& keys) {
    const int members = flags_[flag::kMembers];
    if (members != 0) {
        for (int i = 0; i <= members; ++i) {
            int weapon = 13;
            if (i != 0) weapon -= int(random_.next() & 1);
            if (!offer_weapon(weapon, keys)) return false;  // 17dc:15e6
        }
    } else {
        clear_window();  // 2656:0388
        print_id("weapon2.trade");
        print_id(formats::weapon_text_id(std::int8_t(characters_[0][character::kWeapon])));
        print_id("weapon2.trade_end");
        const auto yes = yes_no(true, keys);  // 1000:1a13(1)
        if (!yes) {
            place_open_ = true;
            return false;
        }
        if (*yes) characters_[0][character::kWeapon] = 13;
    }
    for (int i = 0; i < 8; ++i) {
        auto& c = characters_[std::size_t(i)];
        if (c[character::kName] == character::kEmpty) continue;
        const int cap = std::int8_t(c[character::kB]) * (6 + int(random_.next() & 1));
        if (std::int8_t(c[character::kHitPoints]) > cap) c[character::kHitPoints] = std::uint8_t(cap);
    }
    return true;
}

// BTECH.EXE 24d3:0ef3, special 32 (part of Ghidra FUN_24d3_0004)
// ARMOR's top-up: the character in flag 0e's armour back to full, a point
// at a time while the money lasts (docs/scripts.md, "The shops").
bool Town::top_up_armour(KeySource& keys) {
    if (!rules_) {
        log_.push_back("special 32: no rules");
        return true;
    }
    auto& c = characters_[std::size_t(flags_[flag::kMembers])];
    const int index = c[character::kArmourType] - 1;
    if (index < 0 || std::size_t(index) >= rules_->shops.armour_point.size()) {
        log_.push_back("special 32: armour type " + std::to_string(c[character::kArmourType]) + " past the table");
        return true;
    }
    const int points = std::int8_t(c[character::kArmour]);
    const int full = std::int8_t(rules_->combat.personal_armour[std::size_t(index + 1)]);  // DG:4ddc + t - 1
    const int point_price = std::int8_t(rules_->shops.armour_point[std::size_t(index)]);
    const auto end_with = [&](const char* id) {
        print_id(id);
        if (!read_key(keys)) {  // 273d:0259
            place_open_ = true;
            return false;
        }
        return true;
    };
    if (points == full) return end_with("shop.topup_full");
    print_id("shop.topup_lacks");
    int missing = full - points;
    if (points == 0) {
        print_id("shop.topup_none");
    } else {
        text_.print(std::to_string(missing));  // 273d:0053
        print_id("shop.topup_points");
    }
    print_id("shop.topup_cost");
    text_.print(std::to_string(std::int16_t(point_price * missing)));  // a 16-bit product
    print_id("shop.topup_cbills");
    if (!special(10, keys)) return false;  // the specials call themselves
    print_id("shop.topup_ask");
    const auto yes = yes_no(true, keys);  // 1000:1a13(1)
    if (!yes) {
        place_open_ = true;
        return false;
    }
    if (!*yes) return true;
    bool bought = false;
    while (missing != 0) {
        if (static_cast<std::uint32_t>(point_price) > static_cast<std::uint32_t>(money_.cbills)) break;
        bought = true;
        --missing;
        money_.cbills = std::int32_t(static_cast<std::uint32_t>(money_.cbills) - std::uint32_t(point_price));
        ++c[character::kArmour];
        draw_cbills();  // 1e31:1fdf
    }
    if (missing == 0) return end_with("shop.topup_done");
    print_id("shop.topup_short");
    return end_with(bought ? "shop.topup_some" : "shop.topup_nothing");
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
        // 17dc:19b2: outdoors, one of the eight rumours at DGROUP:4e5e, by a
        // random number's low 3 bits (287f:0bc0), and a key.
        print_id("talk.rumour." + std::to_string(random_.next() & 7));
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
    if (mode != 0) text_.forget(1, 1, 11, 11);
    play_picture(number);
    // Mode 0 (a story picture) waits and clears the box again; the others
    // leave the last frame up.
    if (mode == 0) {
        picture_ = PlacePicture{number};
        if (number == 0) play_sound(12);  // 1000:19bf
        hold_screen(kStoryPictureWait);
        text_.select_window(kPanelPictureWindow);
        text_.clear_window();
        picture_.reset();
        return;
    }
    picture_ = PlacePicture{number};
}

// 1000:48b7's frames (1000:1afd each, the frame and then its delay): the
// first, a wait unless the picture is 8, then the rest; picture 8 plays 7
// times. Each frame is an event held for its wait (273d:0006 counts a 16-bit
// word down to 0).
void Town::play_picture(int number) {
    if (!game_dir_) return;
    const auto path = formats::find_game_file(*game_dir_, "O" + std::to_string(number) + ".ANM");
    if (!path) return;
    const auto anm = formats::read_anm(*path);
    const auto hold = [this](int frames) {
        if (const int n = frames & 0xffff; n != 0) hold_screen(n);
    };
    const int plays = number == kRepeatedPicture ? kRepeatedPlays : 1;
    for (int play = 0; play < plays; ++play)
        for (std::size_t k = 0; k < anm.delays.size(); ++k) {
            picture_ = PlacePicture{number, int(k)};
            hold(anm.delays[k]);
            if (k == 0 && number != kRepeatedPicture) hold(kFirstFrameWait);
        }
}

}  // namespace chr::game
