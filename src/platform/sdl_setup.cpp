#include "platform/sdl_setup.h"

#include "setup/setup.h"
#include "setup/versions.h"

#include <rekit/archive.h>
#include <rekit/sdl/pickers.h>
#include <rekit/sdl/setup_screen.h>
#include <rekit/sdl/setup_serve.h>
#include <rekit/sources.h>
#include <rekit/status.h>

#include <SDL3/SDL.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>

#ifndef CHR_VERSION
#define CHR_VERSION "dev"
#endif

namespace chr::platform {
namespace {

std::string program_dir() {
    const char* base = SDL_GetBasePath();
    return base ? base : "";
}

std::string json_text(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') {
            o += '\\';
            o += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", static_cast<unsigned char>(c));
            o += b;
        } else {
            o += c;
        }
    }
    return o;
}

void say_error(bool setup_only, const std::string& why) {
    std::fprintf(stderr, "setup: %s\n", why.c_str());
    if (setup_only) {
        std::printf("{\"error\": \"%s\"}\n", json_text(why).c_str());
        std::fflush(stdout);
    }
}

std::string describe(const rekit::Found& f) {
    if (f.label == setup::kAmigaDisk)
        return "The Amiga version 2.3 of " + std::string(setup::kTitle) + ", on a disk image." +
               (f.detail.empty() ? "" : " " + f.detail);
    const std::string what = f.version.empty() ? " (another version than " + std::string(setup::kVersion) + " or " +
                                                     setup::kAmigaVersion + ")"
                                               : " (" + f.version + ")";
    return std::string(f.on_disk ? "An installed copy of " : "A download of ") + setup::kTitle + what + ".";
}

// The rebuild's look on the kit's screen: colours only, in the system's
// font. Nothing in it is the original game's.
rekit::sdl::Theme theme() {
    rekit::sdl::Theme t;
    t.header = "INCEPTION REBUILD";
    t.bg = {8, 10, 16, 255}, t.band_colour = {20, 24, 34, 255}, t.rule = {85, 255, 255, 255};
    t.text = {236, 238, 240, 255}, t.dim = {150, 158, 168, 255}, t.accent = {85, 255, 255, 255};
    t.plate_colour = {44, 52, 66, 255}, t.plate_off = {28, 32, 40, 255};
    t.good = {85, 255, 255, 255}, t.bad = {255, 150, 140, 255};
    return t;
}

// This game's recipe for the kit's setup, on its screen or on the launcher's.
rekit::sdl::Game kit_game(const std::string& user) {
    rekit::sdl::Game g;
    g.id = setup::kId, g.title = setup::kTitle, g.folder_key = setup::kFolderKey, g.folder_name = setup::kFolderName;
    g.program = "BTECH.EXE";
    g.rules = setup::rules(), g.hints = setup::hints();
    const std::string pdir = program_dir();
    g.search = [user, pdir] {
        bool s = false;
        return setup::find_install({}, user, pdir, &s);
    };
    g.describe = describe;
    // The Amiga's disk image, which the kit's walk can't read: offered like any copy.
    g.find = [](const std::string& path, rekit::Found* out) {
        *out = setup::find_amiga(path);
        return out->kind != rekit::Found::kNothing;
    };
    const std::string zip = rekit::archives_supported() ? "a download, " : "";
    g.pick_words = zip + "an Amiga disk image (.adf), or BTECH.EXE";
    g.find_sub = rekit::archives_supported() ? "A download (.zip), an Amiga disk (.adf) or BTECH.EXE"
                                             : "An Amiga disk (.adf), or BTECH.EXE in an installed copy";
    g.find_show = std::string(rekit::archives_supported() ? "a download (.zip, .7z), " : "") +
                  "an Amiga disk image (.adf) or a folder where it's installed";
    // What can't be offered: named, in place of the kit's "No copy".
    g.recognise = [](const std::string& path) {
        try {
            std::string note;
            if (setup::find_amiga(path, &note).kind == rekit::Found::kNothing && !note.empty()) return note;
            setup::OtherCopy other;
            if (setup::find_other_copy(path, &other)) return other.note();
        } catch (...) {
        }
        return std::string();
    };
    g.install = [](const rekit::Found& f, const std::string& out, const std::string&,
                   const rekit::sdl::ProgressFn& progress) {
        const setup::Installed r = setup::install(f, out, [&](double at, const std::string& file) {
            return progress(rekit::sdl::Progress{"Copying the game files", at, file});
        });
        rekit::sdl::InstallResult o;
        o.ok = r.ok, o.cancelled = r.cancelled, o.install_dir = r.dir;
        o.summary = r.ok          ? std::string(setup::kTitle) + " is set up: " + std::to_string(r.files) +
                                        " files copied into " + r.dir + "."
                    : r.cancelled ? "Stopped. Nothing was changed."
                                  : "Nothing was copied: " + r.error;
        return o;
    };
    return g;
}

// The kit's screen with this game's recipe: the find offered or looked for,
// copied in; the game's folder, or "".
std::string run_screen(bool setup_only, bool scripted, const std::string& found, bool searched,
                       const std::string& user, int* exit) {
    const rekit::sdl::Game g = kit_game(user);
    rekit::sdl::SetupOptions o;
    o.hidden = setup_unattended(scripted);
    o.user_folder = user, o.default_out = setup::default_install(user), o.program_dir = program_dir();
    if (searched) o.look_first = found;  // found by the search: offered, to copy in or pass over
    else if (!found.empty()) o.in_use = found, o.play_button = !setup_only;
    const rekit::sdl::SetupResult r = rekit::sdl::run_setup(g, theme(), o);
    *exit = r.exit;
    if (r.exit == rekit::kSetupFailed) say_error(setup_only, r.error);
    return r.exit == rekit::kSetupReady ? r.install_dir : std::string();
}

// What a test names as the player's pick (CHR_SETUP_PICK), walked by the kit
// and copied into the player's folder. "" with `why` when not.
std::string take_copy(const std::string& picked, const std::string& user, std::string* why) {
    std::string note;
    const rekit::Found f = setup::find_game(picked, &note);
    std::printf("setup: looked at %s: %s\n", picked.c_str(),
                f.kind == rekit::Found::kNothing ? "nothing" : f.version.empty() ? "another version" : f.version.c_str());
    if (!f.detail.empty() && note.empty()) std::fprintf(stderr, "%s", f.detail.c_str());
    if (!note.empty()) std::printf("setup: %s\n", note.c_str());
    if (f.kind != rekit::Found::kInstall) {
        setup::OtherCopy other;
        if (!note.empty()) *why = note;
        else if (setup::find_other_copy(picked, &other)) *why = other.note();
        else *why = "No copy of " + std::string(setup::kTitle) + " was found in " + picked + ".";
        return "";
    }
    const setup::Installed r = setup::install(f, setup::default_install(user));
    if (!r.ok) {
        *why = r.error;
        return "";
    }
    std::printf("setup: copied %d files into %s\n", r.files, r.dir.c_str());
    return r.dir;
}

// A test hook without the test's own player folder: why it is refused, else "".
std::string hook_without_folder() {
    const char* own = std::getenv("CHR_DATA_DIR");
    if (own && *own) return "";
    for (const char* hook : {"CHR_SETUP_PICK", "REKIT_SETUP_GO"})
        if (const char* v = std::getenv(hook); v && *v)
            return std::string(hook) + " is a test's: it copies a game into the player's folder, so it "
                                       "needs CHR_DATA_DIR to name the test's own (docs/setup.md). Nothing was done.";
    return "";
}

// What is wrong with a served ask's arguments, else "". The kit takes an ask with arguments it
// doesn't know, and a --folder without its folder would install into the default one.
std::string wrong_ask(const std::vector<std::string>& args) {
    const std::string& ask = args[0];
    const std::size_t n = args.size();
    if (ask == "--describe" || ask == "--search") return n == 1 ? "" : "--setup " + ask + " takes nothing after it.";
    if (n < 2 || args[1].empty()) return "--setup " + ask + " needs the pick.";
    if (ask == "--look") return n == 2 ? "" : "--setup --look takes the pick and nothing after it.";
    if (n == 2 || (n == 4 && args[2] == "--folder" && !args[3].empty())) return "";
    return "--setup --install takes the pick, then --folder and a folder or nothing.";
}

// A served ask that isn't answered: its last line and exit.
int refuse_ask(const std::string& why) {
    rekit::SetupMessage m;
    m.done = true, m.summary = why;
    std::printf("%s\n", rekit::setup_message_json(m).c_str());
    std::fflush(stdout);
    std::fprintf(stderr, "setup: %s\n", why.c_str());
    return rekit::kSetupFailed;
}

}  // namespace

bool setup_unattended(bool scripted) {
    if (scripted || std::getenv("CHR_HIDDEN")) return true;
    const char* driver = std::getenv("SDL_VIDEO_DRIVER");
    return driver && (!std::strcmp(driver, "offscreen") || !std::strcmp(driver, "dummy"));
}

std::function<void(const std::string& text)>& message_box() {
    static std::function<void(const std::string&)> box = [](const std::string& text) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, theme().header.c_str(), text.c_str(), nullptr);
    };
    return box;
}

void tell_player(const std::string& text, bool unattended) {
    std::fprintf(stderr, "chr: %s\n", text.c_str());
    if (!unattended) message_box()(text);
}

int guarded(const std::function<int()>& work, const std::function<bool()>& setup_only,
            const std::function<bool()>& unattended) {
    try {
        return work();
    } catch (const std::exception& e) {
        const std::string why = std::string("The game can't start: ") + e.what();
        if (!setup_only()) {
            tell_player(why, unattended());
            return 1;
        }
        say_error(true, why);
        if (!unattended()) message_box()(why);
        return rekit::kSetupFailed;
    }
}

void tell_left_saves(const std::string& user, const setup::PageText& page, bool unattended) {
    const std::string left = setup::left_saves(user, page);
    if (left.empty()) return;
    std::error_code ec;
    const bool set_up = std::filesystem::exists(std::filesystem::path(user) / setup::kSettingsFile, ec);
    tell_player("Saved games from version 1.0.0 are in " + left + ". This version keeps them in " + user +
                    ". Nothing was moved. To go on with them, copy Game1 to Game6 from the first folder to the second.",
                unattended || set_up);
}

bool setup_hooks_allowed(bool setup_only) {
    const std::string why = hook_without_folder();
    if (!why.empty()) say_error(setup_only, why);
    return why.empty();
}

int print_setup_status(const std::string& given, const std::string& builtin, const std::string& data_dir) {
    // Anything else printed while answering goes to stderr: stdout carries the one JSON line.
    std::fflush(stdout);
    rekit::Status st;
    try {
        const std::string user = data_dir.empty() ? setup::user_folder() : data_dir;
        const std::vector<std::string> known =
            given.empty() ? std::vector<std::string>{setup::remembered_folder(user), builtin} : std::vector<std::string>{given};
        st = setup::status(known, user, program_dir(), CHR_VERSION);
    } catch (const std::exception& e) {  // a name nothing can be made of: answered all the same
        st.game = setup::kId, st.version = CHR_VERSION, st.note = std::string("the folders can't be read: ") + e.what();
    }
    st.setup = rekit::kServedSetup;  // serve_game_setup answers the launcher
    std::printf("%s\n", rekit::status_json(st).c_str());
    std::fflush(stdout);
    return 0;
}

std::string settled_user_folder() {
    char* pref = SDL_GetPrefPath(setup::kOrganisation, setup::kApplication);  // made if missing
    if (!pref) return "";
    const std::string folder = pref;
    SDL_free(pref);
    const setup::Settled s = setup::settle_user_folder(folder);
    if (s.kind == setup::Settled::kMoved)
        std::fprintf(stderr, "chr: moved the data folder %s to %s\n", s.from.c_str(), s.folder.c_str());
    else if (s.kind == setup::Settled::kMoveFailed)
        std::fprintf(stderr, "chr: %s; this start uses %s, the move is tried again at the next\n", s.error.c_str(),
                     s.folder.c_str());
    return s.folder;
}

bool served_ask(const std::string& arg) {
    return arg == "--describe" || arg == "--search" || arg == "--look" || arg == "--install";
}

int serve_game_setup(const std::vector<std::string>& args, const std::string& data_dir) {
#if !defined(_WIN32)
    std::signal(SIGPIPE, SIG_IGN);  // a launcher that has gone: the work goes on
#endif
    if (const std::string why = wrong_ask(args); !why.empty()) return refuse_ask(why);
    if (const std::string why = hook_without_folder(); !why.empty()) return refuse_ask(why);
    try {
        std::string user = data_dir;
        if (const char* env = std::getenv("CHR_DATA_DIR"); user.empty() && env) user = env;
        if (user.empty()) user = settled_user_folder();
        if (user.empty())
            return refuse_ask(std::string("There is no folder for the player's files (") + SDL_GetError() + ").");
        rekit::sdl::SetupOptions o;
        o.hidden = true, o.user_folder = user, o.default_out = setup::default_install(user), o.program_dir = program_dir();
        const rekit::sdl::SetupResult r = rekit::sdl::serve_setup(kit_game(user), o, args);
        if (!r.install_dir.empty() && setup::remembered_folder(user) != r.install_dir &&
            !setup::remember_folder(user, r.install_dir))
            std::fprintf(stderr, "setup: couldn't save the game folder in %s\n", setup::kSettingsFile);
        return r.exit;
    } catch (const std::exception& e) {
        return refuse_ask(std::string("The setup failed: ") + e.what());
    }
}

int resolve_game(bool setup_only, bool scripted, const std::string& builtin, const std::string& user,
                 std::string* folder) {
    const std::vector<std::string> known{setup::remembered_folder(user), builtin};
    rekit::sdl::unattended() = [scripted] { return setup_unattended(scripted); };
    std::string game;
    bool searched = false;
    const std::string found = setup::find_install(known, user, program_dir(), &searched);
    if (!setup_only && !found.empty() && !searched) game = found;
    const bool chosen = game.empty();  // by the setup this start: remembered
    // A test's stand-in for the player's pick: walked and copied in with no screen.
    if (const char* pick = std::getenv("CHR_SETUP_PICK"); game.empty() && pick && *pick) {
        std::string why;
        game = take_copy(pick, user, &why);
        if (game.empty()) return say_error(setup_only, why), rekit::kSetupFailed;
    }
    // A check's hidden screen (REKIT_SETUP_FRAMES, the kit's hooks driving it;
    // still no picker), else none unattended.
    const bool checked = std::getenv("REKIT_SETUP_FRAMES") != nullptr;
    if (game.empty() && setup_unattended(scripted) && !checked) {
        say_error(setup_only, "No copy of " + std::string(setup::kTitle) + " is set up, and no player is at this run.");
        return setup_only ? rekit::kSetupFailed : rekit::kSetupCancelled;
    }
    if (game.empty()) {
        int exit = rekit::kSetupCancelled;
        game = run_screen(setup_only, scripted, found, searched, user, &exit);
        if (game.empty()) {
            if (exit == rekit::kSetupCancelled) std::printf("setup: cancelled\n");
            return exit;
        }
    }
    if (chosen && setup::remembered_folder(user) != game && !setup::remember_folder(user, game))
        std::fprintf(stderr, "setup: couldn't save the game folder in %s\n", setup::kSettingsFile);
    std::printf("setup: game files %s\n", game.c_str());
    std::fflush(stdout);
    *folder = game;
    return rekit::kSetupReady;
}

}  // namespace chr::platform
