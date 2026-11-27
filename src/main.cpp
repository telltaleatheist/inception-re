// chr: the rebuild's program. It starts as the original does (the Infocom
// screen, the title, the first-time question, intro.first_time) and goes on to the
// new game's town, walking with the arrow keys and Home/PgUp/End/PgDn. With
// --map N it skips the start-up and begins on map N.
//
//   chr --status | --setup
//   chr [--game-dir DIR] [--data-dir DIR] [--import] [--map N] [--script KEYS]
//       [--text FILE] [--screenshot FILE.bmp] [--state] [--show-quiz]
//
// The game's text and font are imported once from the player's BTECH.EXE
// into the data folder (--data-dir, else CHR_DATA_DIR, else the user's
// application-data folder); that happens by itself the first time, or with
// --import (which imports again and quits). --text FILE layers a text file
// of the same form (formats/import.h) over the imported one, e.g. a
// translation.
// --script plays a key sequence first ("up*5 y", "idle:40", see
// game/town.h) and prints what each key did. --screenshot renders one frame
// to the file without opening a window; with --event N, the screen of the
// N-th thing the logic showed (game/shown.h), and --events lists them.
// --step-hold N holds each step the original showed without a wait (a
// missile's, an explosion's) N retraces (default 1). In a window the town runs at the
// original's pace, one main-loop pass per retrace of a VGA card
// (game/clock.h, platform/pace.h). --loop-ms N, a test aid: with --script and
// a headless video driver, the window's loop runs N ms after the script.
//
// The game folder is --game-dir, else the CHR_GAME_DIR environment variable,
// else the setup's (platform/sdl_setup.h): the one remembered in the data
// folder's settings.ini, the folder set when the program was built, the
// player's own install, else the setup screen, which finds or asks for the
// player's copy and copies it into the data folder. --status prints what is
// found as one JSON line and writes nothing; --setup only sets the game up.

#include <algorithm>
#include <cstdio>
#include <deque>
#include <memory>
#include <optional>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include <SDL3/SDL.h>
#if defined(_WIN32)
#include <SDL3/SDL_main.h>  // WinMain for the GUI program a package links (CHR_WINDOWS_GUI)
#endif

#include "formats/font_file.h"
#include "formats/game_files.h"
#include "formats/game_source.h"
#include "formats/import.h"
#include "formats/mtp.h"
#include "formats/open_source.h"
#include "formats/palette.h"
#include "formats/tables.h"
#include "game/asset_id.h"
#include "game/map_view.h"
#include "game/text_provider.h"
#include "game/town.h"
#include "platform/map_layer.h"
#include "platform/original_assets.h"
#include "platform/overhead_layer.h"
#include "platform/pace.h"
#include "platform/panel_layer.h"
#include "platform/quiz_layer.h"
#include "platform/stats_layer.h"
#include "platform/renderer.h"
#include "platform/save_files.h"
#include "platform/sdl_setup.h"
#include "platform/sitting.h"
#include "platform/sound_player.h"
#include "platform/text_layer.h"

#ifndef CHR_VERSION_TAG
#define CHR_VERSION_TAG "dev"
#endif

// The family launcher's marker: its ADD FROM FILE reads the release tag from this string in the
// program file, without running it (docs/RELEASING.md).
extern "C" const char chi_version_tag[] = "MW3-PROGRAM-VERSION:" CHR_VERSION_TAG;

namespace {

struct Options {
    std::filesystem::path game_dir;
    std::string screenshot;  // write one frame here and quit
    std::string script;      // keys to play on the map first
    std::string text_file;   // text overrides
    std::filesystem::path data_dir;  // the imported text and font
    std::filesystem::path save_dir;  // the saved games GameN; the data folder by default
    bool import = false;
    bool help = false;       // --help: the usage on stdout
    bool status = false;     // --status: the launcher contract's answer (platform/sdl_setup.h)
    bool setup = false;      // --setup: the setup screen only
    std::vector<std::string> served;  // what follows --setup when the launcher asks (platform/sdl_setup.h)
    bool state = false;      // print the game state after the script
    bool fight_state = false;  // with it, the fight's tables
    int event = -1;            // --event N
    int step_hold = 1;         // --step-hold N
    bool keep_places = false;  // --keep-places: the script's items one by one, each place kept (platform/sitting.h)
    std::string loop_keys;     // --loop-keys KEYS: keys pressed in the window's loop, one every kLoopKeyFrames
    bool loop_resets = false;  // --loop-resets: with --loop-keys, SDL's event for a device reset between the keys
    int loop_ms = 0;           // --loop-ms N: the window's loop runs N ms after the script, under a headless driver
    std::optional<std::size_t> demo_to;  // --demo-to N: the demo stops at byte N (DemoKeys::stop_at)
    bool list_events = false;
    bool sound_log = false;    // --sound-log: each effect's steps (docs/sound.md)
    bool audio_report = false;  // --audio-report: whether an audio device was ever opened
    std::filesystem::path sound_mod;  // --sound-mod DIR: N.wav for effect N
    bool walks = false;        // --walks: the walks to the tiles the cache dispatches (Town::walks)
    std::vector<chr::formats::MapPosition> walk_to;  // --walk-to X,Y: the walk toward each (Town::walk_toward)
    bool show_quiz = false;  // show the training center's quiz (no check)
    int map = 0;             // 0: the start-up, then map 1
    // A test aid: flags set as the town starts (--set-flag NN=VV, hex).
    std::vector<std::pair<int, int>> set_flags;
    std::vector<chr::formats::MapPosition> windows;  // --window X,Y: print the cell window built there
};

// The usage, under the program's own name (an AppImage starts it as AppRun).
void usage(std::FILE* out, const char* argv0) {
    std::string program = std::filesystem::path(argv0 ? argv0 : "chr").stem().string();
    if (program.empty() || program == "AppRun") program = "chi";
    const char* name = program.c_str();
    std::fprintf(out, "usage: %s --status | --setup\n"
                 "       %s --setup --describe | --search | --copies | --look PICK | --install PICK [--folder DIR]\n"
                 "       %s [--game-dir DIR] [--data-dir DIR] [--save-dir DIR] [--import] [--map N] [--script KEYS]\n"
                 "           [--text FILE] [--screenshot FILE.bmp] [--event N] [--events] [--walks] [--walk-to X,Y] [--step-hold N] [--loop-ms N] [--loop-keys KEYS] [--loop-resets] [--keep-places] [--demo-to N]\n"
                 "           [--state] [--show-quiz]\n"
                 "  --show-quiz: show the training center's quiz (no check: the right part is\n"
                 "           picked and any answer goes on); by default it is skipped\n"
                 "  test aids (docs/reference.md): [--set-flag NN=VV] sets a flag (hex) as the town\n"
                 "           starts; a script's set:NN=VV sets one at that point;\n"
                 "           [--window X,Y] (hex, repeatable) prints the cell window built there\n", name, name, name);
}

bool parse(int argc, char** argv, Options& options) {
    // Without either, the setup finds the game folder (resolve_game).
    if (const char* env = std::getenv("CHR_GAME_DIR"); env && *env)
        options.game_dir = env;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--game-dir" && i + 1 < argc) {
            options.game_dir = argv[++i];
        } else if (arg == "--map" && i + 1 < argc) {
            options.map = std::atoi(argv[++i]);
            if (options.map < 1 || options.map > 15) {
                std::fprintf(stderr, "chr: --map takes 1 to 15\n");
                return false;
            }
        } else if (arg == "--data-dir" && i + 1 < argc) {
            options.data_dir = argv[++i];
        } else if (arg == "--save-dir" && i + 1 < argc) {
            options.save_dir = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--status") {
            options.status = true;
        } else if (arg == "--setup") {
            options.setup = true;
            if (i + 1 < argc && chr::platform::served_ask(argv[i + 1]))
                while (++i < argc) options.served.push_back(argv[i]);  // the rest is the ask's
        } else if (arg == "--import") {
            options.import = true;
        } else if (arg == "--text" && i + 1 < argc) {
            options.text_file = argv[++i];
        } else if (arg == "--script" && i + 1 < argc) {
            options.script = argv[++i];
        } else if (arg == "--screenshot" && i + 1 < argc) {
            options.screenshot = argv[++i];
        } else if (arg == "--state") {
            options.state = true;
        } else if (arg == "--event" && i + 1 < argc) {
            options.event = std::atoi(argv[++i]);
        } else if (arg == "--demo-to" && i + 1 < argc) {
            options.demo_to = std::size_t(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--step-hold" && i + 1 < argc) {
            options.step_hold = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--loop-ms" && i + 1 < argc) {
            options.loop_ms = std::max(0, std::atoi(argv[++i]));
        } else if (arg == "--loop-keys" && i + 1 < argc) {
            options.loop_keys = argv[++i];
        } else if (arg == "--loop-resets") {
            options.loop_resets = true;
        } else if (arg == "--keep-places") {
            options.keep_places = true;
        } else if (arg == "--events") {
            options.list_events = true;
        } else if (arg == "--sound-log") {
            options.sound_log = true;
        } else if (arg == "--audio-report") {
            options.audio_report = true;
        } else if (arg == "--sound-mod" && i + 1 < argc) {
            options.sound_mod = argv[++i];
        } else if (arg == "--walks") {
            options.walks = true;
        } else if (arg == "--walk-to" && i + 1 < argc) {
            unsigned x = 0, y = 0;
            if (std::sscanf(argv[++i], "%x,%x", &x, &y) != 2 || x > 0xffff || y > 0xffff) {
                std::fprintf(stderr, "chr: --walk-to takes X,Y in hex\n");
                return false;
            }
            options.walk_to.push_back({std::uint16_t(x), std::uint16_t(y)});
        } else if (arg == "--fight-state") {
            options.state = options.fight_state = true;
        } else if (arg == "--show-quiz") {
            options.show_quiz = true;
        } else if (arg == "--window" && i + 1 < argc) {
            unsigned x = 0, y = 0;
            if (std::sscanf(argv[++i], "%x,%x", &x, &y) != 2 || x > 0xffff || y > 0xffff) {
                std::fprintf(stderr, "chr: --window takes X,Y in hex\n");
                return false;
            }
            options.windows.push_back({std::uint16_t(x), std::uint16_t(y)});
        } else if (arg == "--set-flag" && i + 1 < argc) {
            // Parsed like the script's set:NN=VV, which checks it.
            try {
                const auto items = chr::game::parse_key_script("set:" + std::string(argv[++i]));
                options.set_flags.emplace_back(items.front().set_flag, items.front().set_value);
            } catch (const std::invalid_argument& e) {
                std::fprintf(stderr, "chr: --set-flag: %s\n", e.what());
                return false;
            }
        } else {
            usage(stderr, argv[0]);
            return false;
        }
    }
    return true;
}

// --state: what tools/reference/check.py compares with the original's memory.
// The cell window at `at`: the one the game holds if it was last built
// there (a map cell changed since shows only after the next build, as in
// the original), else built from the terrain as it is now. Its nine blocks'
// values (P:07a4), then its cells a row a line (P:07ad).
void print_window(const chr::game::Town& town, chr::formats::MapPosition at) {
    using chr::game::Terrain;
    const auto window = town.terrain().held_window(at);
    std::printf("window %04x %04x blocks ", at.x, at.y);
    for (const auto b : window.blocks) std::printf("%02x", b);
    for (int r = 0; r < Terrain::kWindowCells; ++r) {
        std::printf("\nwindow %04x %04x row %2d ", at.x, at.y, r);
        for (int c = 0; c < Terrain::kWindowCells; ++c)
            std::printf("%02x", window.cells[std::size_t(r * Terrain::kWindowCells + c)]);
    }
    std::printf("\n");
    // P:09f3..0c1c, what follows the window's corner words up to the block
    // store (window.py).
    std::printf("window %04x %04x after ", at.x, at.y);
    for (int i = 0x09f3 - 0x09ed; i < 0x0c1d - 0x09ed; ++i)
        std::printf("%02x", unsigned(town.terrain().window_cell(at, Terrain::kWindowCells * Terrain::kWindowCells + i).value_or(0)));
    std::printf("\n");
}

void print_state(const chr::game::Town& town) {
    const auto& r = town.random().state();
    std::printf("state random %02x %02x %02x\n", r[0], r[1], r[2]);
    std::printf("state cbills %d\n", int(town.money().cbills));
    if (const auto pos = town.demo_position()) std::printf("state demo %zu\n", *pos);
    std::printf("state counters %d %d\n", town.town_ticks(), town.mission_ticks());
    std::printf("state wander %d %d\n", town.townspeople().wander_count(), town.townspeople().wander_pattern());
    int i = 0;
    const auto& figs = town.fight_figures();
    for (const auto& w : town.townspeople().walkers()) {
        // In a fight the original's walker places and headings are figures
        // 16-23's.
        const bool away = town.townspeople_away();
        const auto f = std::size_t(16 + i);
        std::printf("state walker %d at %04x %04x target %04x %04x route %02x wait %02x heading %d\n", i++,
                    away ? figs.x[f] : w.at.x, away ? figs.y[f] : w.at.y, w.target.x, w.target.y, w.route, w.wait,
                    away ? int(figs.heading[f]) : int(w.heading));
    }
    std::printf("state party %04x %04x\n", town.position().x, town.position().y);
    std::printf("state flags ");
    for (const auto f : town.flags()) std::printf("%02x", f);
    std::printf("\nstate skills ");
    for (int k = 0; k < chr::game::character::kSkillCount; ++k)
        std::printf("%02x", town.characters()[0][std::size_t(chr::game::character::kSkills + k)]);
    for (std::size_t c = 0; c < town.characters().size(); ++c) {
        std::printf("\nstate character %zu ", c);
        for (const auto b : town.characters()[c]) std::printf("%02x", b);
    }
    for (std::size_t m = 0; m < town.mechs().size(); ++m) {
        std::printf("\nstate mech %zu ", m);
        for (const auto b : town.mechs()[m]) std::printf("%02x", b);
    }
    std::printf("\nstate menus");
    for (const auto last : town.menu_last()) std::printf(" %d", last);
    std::printf("\nstate seen ");
    for (const auto b : town.seen()) std::printf("%02x", b);
    std::printf("\n");
    std::printf("state noise ");
    for (const auto n : chr::game::Terrain::start_up_noise()) std::printf("%02x", n);
    std::printf("\n");
    const auto& grids = town.terrain().grids();
    for (int k = 0; k < chr::game::Terrain::kGrids; ++k) {
        std::printf("state grid %d ", k);
        for (int b = 0; b < chr::game::Terrain::kGridBlocks; ++b)
            std::printf("%02x", grids[std::size_t(k * chr::game::Terrain::kGridBlocks + b)]);
        std::printf("\n");
    }
}

// The fight's tables, as tools/reference/state.py reads them (docs/combat.md).
void print_fight_state(const chr::game::Town& town) {
    const auto& f = town.fight_figures();
    const auto& a = town.figure_animation();
    const auto& c = town.combat();
    for (int i = 0; i < chr::game::FightFigures::kCount; ++i) {
        const auto n = std::size_t(i);
        std::printf("state figure %d at %04x %04x active %d destroyed %d heading %02x facing %02x frame %02x\n", i,
                    f.x[n], f.y[n], f.active[n] & 0xffff, int(f.destroyed[n]), f.heading[n] & 0xff,
                    a.direction(i) & 0xff, a.frame(i) & 0xff);
        std::printf("state orders %d ", i);
        for (const auto b : c.orders[n]) std::printf("%02x", b);
        std::printf("\nstate targets %d ", i);
        for (const auto b : c.targets[n]) std::printf("%02x", b);
        std::printf("\nstate path %d ", i);
        for (const auto b : c.paths[n]) std::printf("%02x", b & 0xff);
        std::printf("\nstate drawn %d on %d terrain %02x tile %02x above %02x half %02x at %d %d parked %02x script %04x\n",
                    i, int(c.on_screen[n]), c.terrain[n] & 0xff, c.tile[n], c.standing_on[n], c.sink[n],
                    c.screen_x[n] & 0xffff, c.screen_y[n] & 0xffff, c.parked_facing[n] & 0xff, a.script_position(i) & 0xffff);
    }
    std::printf("state heat");
    for (const auto h : c.heat) std::printf(" %02x", h & 0xff);
    std::printf("\nstate burning");
    for (const auto b : c.burning) std::printf(" %02x", b);
    std::printf("\nstate fight_on %d\n", int(c.fight_on));
    const auto& m = town.marks();
    std::printf("state marks next %02x", m.next);
    for (const auto& k : m.marks) std::printf(" %02x%02x%02x%02x", k.code, k.area, k.x, k.y);
    std::printf("\n");
}

// A screenshot's pixels for each screen unit: 960x600, whatever the display.
constexpr int kScreenshotScale = 3;
// Passes of the main loop with no key before the place is kept (platform/sitting.h): ten seconds.
constexpr long kKeepPlacePasses = 600;
constexpr int kLoopKeyFrames = 4;  // --loop-keys

bool save_screenshot(chr::platform::Renderer& renderer, const std::string& path) {
    SDL_Surface* shot = renderer.read_frame();
    if (!shot) return false;
    const bool ok = SDL_SaveBMP(shot, path.c_str());
    SDL_DestroySurface(shot);
    return ok;
}

// Map `number` and where the party starts on it: the new-game start for
// map 1, the map's middle for the others.
bool load_map(const chr::formats::GameSource& source, int number, chr::formats::MtpMap& map,
              chr::formats::MapPosition& start) {
    const std::string name = "MAP" + std::to_string(number) + ".MTP";
    try {
        const auto bytes = source.map(number);
        if (!bytes) {
            std::fprintf(stderr, "chr: %s not found in %s\n", name.c_str(), source.describe().c_str());
            return false;
        }
        map = chr::formats::decode_mtp(*bytes);
        start = chr::game::new_game_position();
        if (number != 1)
            start = {std::uint16_t(map.block_x * 16 + map.width),
                     std::uint16_t(map.block_y * 16 + map.height)};
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "chr: %s: %s\n", name.c_str(), e.what());
        return false;
    }
}

// The original's codes for the keys the town understands, or 0.
int key_code(SDL_Keycode key) {
    namespace k = chr::game::key;
    switch (key) {
    case SDLK_UP: case SDLK_KP_8: return k::kUp;
    case SDLK_DOWN: case SDLK_KP_2: return k::kDown;
    case SDLK_LEFT: case SDLK_KP_4: return k::kLeft;
    case SDLK_RIGHT: case SDLK_KP_6: return k::kRight;
    case SDLK_HOME: case SDLK_KP_7: return k::kHome;
    case SDLK_PAGEUP: case SDLK_KP_9: return k::kPageUp;
    case SDLK_END: case SDLK_KP_1: return k::kEnd;
    case SDLK_PAGEDOWN: case SDLK_KP_3: return k::kPageDown;
    case SDLK_RETURN: case SDLK_KP_ENTER: return k::kEnter;
    case SDLK_SPACE: return k::kSpace;
    default: break;
    }
    if (key >= SDLK_A && key <= SDLK_Z) return int('a' + (key - SDLK_A));
    return 0;
}

// What begin() answers for a start that goes on to the game.
constexpr int kGoOn = -1;

// A run that asks for an answer, or that no player is at: nothing is shown to say why it ended.
bool no_player(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--script" || arg == "--screenshot" || arg == "--status" || arg == "--import" ||
            (arg == "--setup" && i + 1 < argc))
            return true;
    }
    return chr::platform::setup_unattended();
}

// The start up to the folders it uses: options' game_dir and data_dir. The program's exit when it ends there.
int begin(int argc, char** argv, Options& options) {
    if (!parse(argc, argv, options)) return 2;
    if (options.help) {
        usage(stdout, argv[0]);
        return 0;
    }

    // A run no player is at (the rig, the tests, a hidden setup check) is a
    // background app on macOS, before any SDL_Init (the setup screen's too):
    // it never takes the foreground, whatever video driver it gets.
    const bool scripted = !options.screenshot.empty() || !options.script.empty();
    if (chr::platform::setup_unattended(scripted)) SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    // The launcher contract: --status answers before anything is made or written.
    if (options.status)
        return chr::platform::print_setup_status(options.game_dir.string(), CHR_DEFAULT_GAME_DIR,
                                                 options.data_dir.string());
    // So does an ask about the setup, on stdout alone: the launcher reads it through a pipe.
    if (!options.served.empty()) return chr::platform::serve_game_setup(options.served, options.data_dir.string(), CHR_DEFAULT_GAME_DIR);

    if (options.setup || options.game_dir.empty()) {
        if (!chr::platform::setup_hooks_allowed(options.setup)) return options.setup ? 2 : 1;
    }
    if (options.data_dir.empty()) {
        if (const char* env = std::getenv("CHR_DATA_DIR"); env && *env) {
            options.data_dir = env;
        } else if (const auto folder = chr::platform::settled_user_folder(); !folder.empty()) {
            options.data_dir = folder;
        } else {
            chr::platform::tell_player(std::string("no data folder (") + SDL_GetError() + "); use --data-dir",
                                       no_player(argc, argv));
            return 1;
        }
    }
    // Started with nothing on the command line (a shortcut, a double-click,
    // the launcher): no console, so the log goes beside the saves.
    if (argc == 1) {
        const auto log = options.data_dir / "chi.log";
        // Both append, so neither writes over what the other wrote.
        if (std::freopen(log.string().c_str(), "w", stdout) && std::freopen(log.string().c_str(), "a", stdout) &&
            std::freopen(log.string().c_str(), "a", stderr))
            std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
    if (options.save_dir.empty())
        chr::platform::tell_left_saves(options.data_dir.string(), chr::setup::old_page(), no_player(argc, argv));
    if (options.setup || options.game_dir.empty()) {
        std::string folder;
        const int ready = chr::platform::resolve_game(options.setup, scripted, CHR_DEFAULT_GAME_DIR,
                                                      options.data_dir.string(), &folder);
        if (options.setup) return ready;
        if (ready != 0) return ready == 1 ? 0 : 1;
        options.game_dir = folder;
    }
    return kGoOn;
}

}  // namespace

int main(int argc, char** argv) {
    // A read the compiler must make: no linker drops the marker as unreferenced.
    { const volatile char* marker = chi_version_tag; (void)marker[0]; }
    Options options;
    const int ended = chr::platform::guarded([&] { return begin(argc, argv, options); },
                                             [&] { return options.setup; }, [&] { return no_player(argc, argv); });
    if (ended != kGoOn) return ended;
    std::shared_ptr<const chr::formats::GameSource> source;
    try {
        source = chr::formats::open_game_source(options.game_dir);
        if (options.import) {
            chr::formats::import_original(*source, options.data_dir);
            std::printf("imported the game's data (%s) into %s\n", source->program_name().c_str(), options.data_dir.string().c_str());
            return 0;
        }
        if (chr::formats::ensure_imported(*source, options.data_dir))
            std::fprintf(stderr, "chr: imported the game's data (%s) into %s\n", source->program_name().c_str(),
                         options.data_dir.string().c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "chr: %s\n", e.what());
        return 1;
    }

    // A screenshot run never shows a window: it renders with SDL's dummy
    // video driver and the software renderer (unless SDL_VIDEO_DRIVER or
    // SDL_RENDER_DRIVER names others) into a hidden window, so it is safe to
    // run while the screen is being shared.
    const bool headless = !options.screenshot.empty();
    if (headless && !std::getenv("SDL_VIDEO_DRIVER"))
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (headless && !std::getenv("SDL_RENDER_DRIVER"))
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "chr: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* sdl_renderer = nullptr;
    const SDL_WindowFlags window_flags =
        headless ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!SDL_CreateWindowAndRenderer("Inception Rebuild", 960, 600, window_flags,
                                     &window, &sdl_renderer)) {
        std::fprintf(stderr, "chr: can't open a window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    // Held to the screen's shape while it is resized (docs/window.md). A
    // system that can't leaves bars at two of the sides.
    if (!headless) {
        constexpr float kShape = float(chr::platform::kScreenWidth) / float(chr::platform::kScreenHeight);
        SDL_SetWindowAspectRatio(window, kShape, kShape);
        SDL_SetWindowMinimumSize(window, chr::platform::kScreenWidth, chr::platform::kScreenHeight);
    }

    int status = 0;
    bool audio_opened = false;  // --audio-report
    chr::formats::MtpMap map;
    chr::formats::MapPosition start;
    std::deque<chr::game::ScriptItem> keys;  // the script's
    // Without --map the program starts up as the original does, and the new
    // game begins on map 1.
    const bool start_up = options.map == 0;
    if (start_up) options.map = 1;
    if (!load_map(*source, options.map, map, start)) status = 1;
    chr::game::TextTable overrides;
    if (status == 0 && !options.text_file.empty()) {
        try {
            overrides = chr::game::TextTable::load(options.text_file);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "chr: --text %s: %s\n", options.text_file.c_str(), e.what());
            status = 2;
        }
    }
    if (status == 0) {
        try {
            keys = chr::game::parse_key_script(options.script);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "chr: --script: %s\n", e.what());
            status = 2;
        }
    }
    // The program's small tables (formats/tables.h), from the import.
    chr::formats::Tables tables;
    if (status == 0) {
        try {
            tables = chr::formats::read_tables(options.data_dir);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "chr: %s\n", e.what());
            status = 1;
        }
    }
    if (status == 0) {
        const auto original_palette = tables.palette;
        chr::platform::OriginalAssets assets(source, options.data_dir, original_palette,
                                             {{"ENDMECH.CMP", tables.endmech_palette}});
        const auto palette = chr::platform::screen_colours(assets, original_palette);
        if (const char* base = SDL_GetBasePath())
            assets.set_own_font(std::filesystem::path(base) / chr::formats::kOwnFontFile);
        chr::platform::Renderer renderer(sdl_renderer, assets, headless ? kScreenshotScale : 0);
        const auto& title = chr::assets::kTitleScreen;

        // The town takes keys and idle passes as they come. A question asks
        // for its answer in the middle of a turn; when it is left waiting,
        // the town is played again with the answer (platform/sitting.h),
        // which keeps the logic a plain sequence.
        chr::game::TextTable imported;
        try {
            imported = chr::game::TextTable::load(chr::formats::imported_text_path(options.data_dir));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "chr: %s\n", e.what());
            status = 1;
        }
        // The repo's default interface texts (game/text_provider.h), next to
        // the program, under the import.
        chr::game::TextTable interface_default;
        if (const char* base = SDL_GetBasePath()) {
            const auto path = std::filesystem::path(base) / chr::game::kInterfaceTextFile;
            std::error_code ec;
            if (std::filesystem::exists(path, ec)) {
                try {
                    interface_default = chr::game::TextTable::load(path);
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "chr: %s: %s\n", path.string().c_str(), e.what());
                    status = 1;
                }
            }
        }
        // The rule tables, for a fight and the quiz scene.
        std::optional<chr::formats::Rules> rules;
        try {
            rules = chr::formats::read_rules(options.data_dir);
        } catch (const std::exception&) {
        }
        // Every map's names, not only the starting map's: a map is loaded
        // into the grids as the party nears it.
        chr::game::MapText map_text(map, options.map);
        for (int n = 1; n <= 15; ++n) {
            if (n == options.map) continue;
            const auto bytes = source->map(n);
            if (!bytes) continue;
            try {
                map_text.add(chr::formats::decode_mtp(*bytes), n);
            } catch (const std::exception&) {
            }
        }
        const chr::game::LayeredText texts({&overrides, &imported, &map_text, &interface_default});
        // The saves: what a replay reads and writes is done once.
        chr::platform::SaveFiles save_files(options.save_dir.empty() ? options.data_dir : options.save_dir);
        chr::platform::SaveJournal saves(save_files);
        bool strayed = false;  // a replay that asked for other saves has been reported
        chr::platform::Sitting sitting([&] {
            auto begun = std::make_unique<chr::game::Town>(map, options.map, start, texts, tables);
            begun->set_source(source);
            begun->set_saves(&saves);
            if (rules) begun->set_rules(&*rules);
            begun->set_show_quiz(options.show_quiz);
            if (options.demo_to) begun->set_demo_stop(*options.demo_to);
            if (start_up) begun->start_up();
            else begun->draw_panel(true);
            for (const auto& [f, v] : options.set_flags) begun->set_flag_when_town_starts(f, v);
            return begun;
        }, saves);
        const auto& town = sitting.town();
        // What can't be gone on with: said, and the title is shown.
        const auto tried = [&](const auto& what) -> bool {
            try {
                what();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "chr: %s\n", e.what());
                return false;
            }
            if (saves.strayed() && !strayed) {
                strayed = true;
                std::fprintf(stderr, "chr: played again, the game asked for other saves than it had; they were left alone\n");
            }
            return true;
        };
        const auto replay = [&] { return tried([&] { sitting.play(); }); };
        // What the script showed and logged.
        std::vector<chr::game::ShownEvent> shown;
        std::vector<std::string> log;
        const auto list_shown = [&] {
            shown.insert(shown.end(), town->events().begin(), town->events().end());
            log.insert(log.end(), town->log().begin(), town->log().end());
        };
        const auto play_script = [&] {
            if (!options.keep_places) {
                for (const auto& item : keys) sitting.add(item);
                if (!replay()) return false;
                list_shown();
                return true;
            }
            int places = 0;
            const bool done = tried([&] {
                sitting.play();
                for (const auto& item : keys) {
                    if (town->at_main_loop()) {
                        list_shown();
                        places += sitting.keep_place();
                    }
                    sitting.take(item);
                }
            });
            if (done) list_shown();
            std::fprintf(stderr, "chr: %d places kept\n", places);
            return done;
        };
        if (options.map != 0 && status == 0) {
            if (!play_script()) {
                status = 1;
            } else if (!options.script.empty()) {
                for (const auto& line : log) std::printf("%s\n", line.c_str());
                if (town->question_open()) std::printf("(waiting for an answer)\n");
                if (town->place_open()) std::printf("(waiting in a place)\n");
                if (town->poll_open()) std::printf("(polling for a key)\n");
                if (town->start_up_open()) std::printf("(waiting in the start-up)\n");
            }
            if (options.state) print_state(*town);
            if (options.fight_state) print_fight_state(*town);
            for (const auto at : options.windows) print_window(*town, at);
            // A walk in the script's own key names, a run of the same key as KEY*N.
            const auto walk_keys = [](const std::vector<chr::game::Step>& steps) {
                static const char* const kNames[3][3] = {{"home", "up", "pgup"}, {"left", "", "right"}, {"end", "down", "pgdn"}};
                std::string keys;
                for (std::size_t i = 0; i < steps.size();) {
                    std::size_t j = i;
                    while (j < steps.size() && steps[j].dx == steps[i].dx && steps[j].dy == steps[i].dy) ++j;
                    if (!keys.empty()) keys += ' ';
                    keys += kNames[steps[i].dy + 1][steps[i].dx + 1];
                    if (j - i > 1) keys += "*" + std::to_string(j - i);
                    i = j;
                }
                return keys;
            };
            if (options.walks)
                for (const auto& w : town->walks())
                    std::printf("walk %02x %04x,%04x %zu %s\n", w.tile, w.at.x, w.at.y, w.steps.size(), walk_keys(w.steps).c_str());
            for (const auto target : options.walk_to) {
                const auto w = town->walk_toward(target);
                std::printf("walk-to %04x,%04x %04x,%04x %zu %s\n", target.x, target.y, w.at.x, w.at.y, w.steps.size(),
                            walk_keys(w.steps).c_str());
            }
            if (options.sound_log) {
                // The title's tune, then each effect played, in the
                // original's terms, for the rig (tools/reference/sound.py).
                std::printf("music-timer %d %d\n", chr::game::kMusicTimerCount, chr::game::kMusicTicksPerStep);
                for (const auto& m : town->title_tune()) {
                    if (m.rest) std::printf("music-step rest %d\n", m.divisor);
                    else std::printf("music-step tone %d\n", m.divisor);
                }
                for (const auto& e : shown) {
                    if (e.screen || e.wait_ms <= 0) continue;
                    std::printf("sound %d\n", e.sound);
                    for (const auto& s : town->sound_steps(e.sound)) {
                        using Kind = chr::game::SoundStep::Kind;
                        if (s.kind == Kind::tone) std::printf("sound-step tone %d %d %d\n", s.divisor, s.units, s.times);
                        else if (s.kind == Kind::rest) std::printf("sound-step rest %d %d\n", s.units, s.times);
                        else std::printf("sound-step burst %d %d\n", s.flips, s.count);
                    }
                }
            }
            if (options.list_events) {
                for (const auto& e : shown) {
                    if (!e.screen) std::printf("event sound %d\n", e.sound);
                    else std::printf("event screen %d retraces, %zu sprites, %zu lines%s\n", e.retraces,
                                     e.screen->sprites.size(), e.screen->lines.size(), e.screen->picture ? ", a picture" : "");
                }
            }
        }

        // What the logic showed while it ran on (game/shown.h) is played
        // when it stops: `played` events are done. Keys pressed meanwhile
        // wait until the end; then the logic resumes with them. The script's
        // own are not played.
        std::size_t played = town ? town->events().size() : 0;
        if (town && options.event >= 0 && std::size_t(options.event) < played) played = std::size_t(options.event);
        bool keys_meanwhile = false;
        // Sound only for a player at the keyboard: a run with a script or a
        // screenshot (the rig, the tests) never opens an audio device.
        std::unique_ptr<chr::platform::WavSounds> sound_mods;
        std::unique_ptr<chr::platform::WavSounds> copy_sounds;
        chr::platform::SpeakerSounds speaker(tables.sound_lists);
        std::unique_ptr<chr::platform::SoundPlayer> sound_player;
        if (options.script.empty() && options.screenshot.empty()) {
            if (!options.sound_mod.empty()) sound_mods = std::make_unique<chr::platform::WavSounds>(options.sound_mod);
            const auto imported_sounds = chr::formats::imported_sounds_dir(options.data_dir);
            if (std::error_code ec; std::filesystem::exists(imported_sounds, ec))
                copy_sounds = std::make_unique<chr::platform::WavSounds>(imported_sounds);
            sound_player = std::make_unique<chr::platform::SoundPlayer>(sound_mods.get(), copy_sounds.get(), speaker);
        }
        audio_opened = audio_opened || SDL_WasInit(SDL_INIT_AUDIO) != 0;
        std::size_t sounded = played;  // the events before it have had their sound started
        bool music_on = false;  // the title's tune has been started
        const auto playing = [&] { return town && played < town->events().size(); };
        // What the loop played and started, and what there was to: --loop-keys reports them.
        // The script's own are not the loop's to play.
        const auto sounds_in = [&](std::size_t n) {
            const auto first = town->events().begin();
            return std::size_t(std::count_if(first, first + std::ptrdiff_t(n), [](const auto& e) { return !e.screen && e.wait_ms > 0; }));
        };
        struct {
            std::size_t shown = 0, sounds = 0, played = 0, started = 0, places = 0;
        } totals;
        totals.played = played, totals.started = town ? sounds_in(played) : 0;
        // At the main loop what was shown has all been played, and a replay starts here from now on.
        const auto keep_place = [&] {
            if (playing()) return;
            const auto& events = town->events();
            const std::size_t all = events.size(), sounds = sounds_in(events.size());
            if (!sitting.keep_place()) return;
            totals.shown += all, totals.sounds += sounds, ++totals.places;
            played = sounded = 0;
        };
        // What the window's time goes to.
        enum class Doing {
            Waiting,  // for a key, however long
            Events,   // what the logic showed
            Poll,     // the logic asks whether a key is waiting
            Demo,
            Title,    // its tune
            Infocom,
            Town,     // the main loop's passes
        };
        const auto doing = [&] {
            if (!town || !options.screenshot.empty()) return Doing::Waiting;
            if (playing()) return Doing::Events;
            if (town->poll_open()) return Doing::Poll;
            if (town->demo_playing()) return Doing::Demo;
            if (town->title_tune_playing()) return Doing::Title;
            if (town->start_up_open() && town->full_screen() == chr::assets::kInfocomLogo) return Doing::Infocom;
            if (town->question_open() || town->place_open() || town->start_up_open()) return Doing::Waiting;
            return Doing::Town;
        };
        chr::platform::Pace pace;
        // Uses the time there is.
        const auto go_on = [&] {
            switch (doing()) {
            case Doing::Waiting:
                pace.restart();
                break;
            case Doing::Events:
                while (played < town->events().size()) {
                    // The game waits while a sound plays (docs/sound.md).
                    const auto& e = town->events()[played];
                    if (played >= sounded) {
                        if (!e.screen && e.wait_ms > 0) {
                            ++totals.started;
                            if (sound_player) sound_player->play(e.sound);
                        }
                        sounded = played + 1;
                    }
                    if (!pace.take(chr::platform::event_ms(e, options.step_hold))) return;
                    ++played, ++totals.played;
                }
                if (keys_meanwhile) {
                    keys_meanwhile = false;
                    replay();
                } else if (town->poll_open() && !town->poll_waits()) {
                    // 203b:1015 asked whether a key is waiting: none was,
                    // and the asking took no time.
                    sitting.add_time(1);
                    replay();
                }
                break;
            case Doing::Poll:
                // Asked again and again, it is asked once a retrace.
                if (town->poll_waits()) {
                    if (const int n = pace.take_retraces(); n > 0) {
                        sitting.add_time(n);
                        replay();
                    }
                } else if (pace.take(chr::game::kRetraceMs)) {
                    sitting.add_time(1);
                    replay();
                    if (!playing()) pace.take_retraces();
                }
                break;
            case Doing::Demo:     // the demo's keys keep its time (docs/startup.md, "The demo")
            case Doing::Infocom:  // it goes by itself after a while: time is input there too
                if (const int n = pace.take_retraces(); n > 0) {
                    sitting.add_time(n);
                    replay();
                }
                break;
            case Doing::Title:
                // When the tune has played to its end with no key, the demo.
                if (pace.take(town->title_wait_ms())) {
                    chr::game::ScriptItem item;
                    item.tune_end = true;
                    sitting.add(item);
                    replay();
                }
                break;
            case Doing::Town: {
                // A stretch with no key is a replay's too.
                if (sitting.time() >= kKeepPlacePasses) keep_place();
                tried([&] {
                    while (doing() == Doing::Town && pace.take(chr::game::kRetraceMs)) sitting.pass();
                });
                break;
            }
            }
        };

        bool running = status == 0;
        // A script under a headless video driver has no one to press
        // Escape: without a screenshot to take, it ends with the script.
        if (!options.script.empty() && options.screenshot.empty()) {
            const std::string_view driver = SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "";
            if (driver == "dummy" || driver == "offscreen") running = running && options.loop_ms > 0;
        }
        bool reported = false;
        bool skipped = false;  // a frame that couldn't be drawn has been reported
        const auto pressed = [&](int k) {
            if (town && playing()) {
                sitting.add({k, 0});
                keys_meanwhile = true;
            } else if (town) {
                keep_place();
                tried([&] { sitting.key(k); });
            }
        };
        std::deque<chr::game::ScriptItem> loop_keys;
        if (!options.loop_keys.empty()) loop_keys = chr::game::parse_key_script(options.loop_keys);
        int frames = 0;
        Uint64 last = SDL_GetTicksNS();
        const Uint64 loop_started = last;
        while (running) {
            if (options.loop_ms > 0 && last - loop_started >= Uint64(options.loop_ms) * 1000000) break;
            const Doing before = doing();
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                renderer.handle(event);
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type != SDL_EVENT_KEY_DOWN) continue;
                if (event.key.key == SDLK_ESCAPE) running = false;
                if (const int k = key_code(event.key.key); k != 0) pressed(k);
            }
            if (options.loop_resets && frames % kLoopKeyFrames == 1) {
                SDL_Event reset{};
                reset.type = SDL_EVENT_RENDER_DEVICE_RESET;
                SDL_PushEvent(&reset);
            }
            if (!loop_keys.empty() && ++frames % kLoopKeyFrames == 0) {
                const auto item = loop_keys.front();
                loop_keys.pop_front();
                if (item.key != 0) pressed(item.key);
                else if (town) tried([&] { sitting.take(item); });
            } else if (loop_keys.empty() && !options.loop_keys.empty() && !playing() && !keys_meanwhile) {
                running = false;
            }
            const Uint64 now = SDL_GetTicksNS();
            // What a key started has had no time yet.
            if (doing() == before) pace.add(double(now - last) / 1e6);
            else pace.restart();
            last = now;
            go_on();
            // The title's tune plays while the title waits for its key
            // (1000:476d); the key cuts it off.
            if (sound_player) {
                const bool tune = town && town->title_tune_playing();
                if (tune && !music_on) sound_player->play_music(chr::game::music_segments(town->title_tune()));
                if (!tune && music_on) sound_player->stop_music();
                music_on = tune;
            }
            if (!renderer.begin_frame()) {
                // The game goes on without the frame: it may come back.
                if (!skipped) std::fprintf(stderr, "chr: can't draw: %s\n", SDL_GetError());
                skipped = true;
                if (!options.screenshot.empty()) {
                    status = 1;
                    break;
                }
                SDL_Delay(16);
                continue;
            }
            bool drawn = true;
            if (const auto screen = town ? town->full_screen() : std::nullopt) {
                drawn = renderer.draw(*screen, 0, 0);
            } else if (const auto quiz = town ? town->quiz() : std::nullopt) {
                drawn = chr::platform::draw_quiz(renderer, *quiz, palette);
                drawn = chr::platform::draw_text(renderer, town->text(), tables, palette) && drawn;
            } else if (town && town->stats() && !playing()) {
                const auto colours = chr::platform::stats_colours(*town->stats(), tables, palette);
                drawn = chr::platform::draw_stats(renderer, *town->stats(), tables, palette, colours);
                drawn = chr::platform::draw_text(renderer, town->text(), tables, colours) && drawn;
            } else if (town && town->overhead()) {
                drawn = chr::platform::draw_overhead(renderer, *town->overhead(), tables, palette);
                drawn = chr::platform::draw_text(renderer, town->text(), tables, palette) && drawn;
            } else if (playing() && town->events()[played].screen) {
                const auto& shown = *town->events()[played].screen;
                drawn = chr::platform::draw_map(renderer, shown.view);
                drawn = chr::platform::draw_figures(renderer, shown.view, shown.figures) && drawn;
                drawn = chr::platform::draw_shot_marks(renderer, shown.sprites, shown.lines, tables, palette) && drawn;
                drawn = chr::platform::draw_text(renderer, shown.text, tables, palette) && drawn;
                chr::platform::draw_bars(renderer, town->bars(), palette);
                for (const auto& box : shown.boxes)
                    drawn = chr::platform::draw_xor_box(renderer, shown.view, shown.figures, box, palette) && drawn;
                if (shown.picture)
                    drawn = renderer.draw({"O" + std::to_string(shown.picture->number) + ".ANM", 0xffff}, 8, 8) && drawn;
            } else if (town) {
                const auto view = town->view();
                drawn = chr::platform::draw_map(renderer, view);
                drawn = chr::platform::draw_figures(renderer, view, town->figures()) && drawn;
                drawn = chr::platform::draw_shot_marks(renderer, town->sprites(), town->lines(), tables, palette) && drawn;
                drawn = chr::platform::draw_text(renderer, town->text(), tables, palette) && drawn;
                chr::platform::draw_bars(renderer, town->bars(), palette);
                for (const auto& box : town->boxes())
                    drawn = chr::platform::draw_xor_box(renderer, view, town->figures(), box, palette) && drawn;
                // A place's picture, in the upper left box (inside window 4).
                if (const auto picture = town->picture())
                    drawn = renderer.draw({"O" + std::to_string(picture->number) + ".ANM", std::uint16_t(picture->frame)}, 8, 8) && drawn;
            } else {
                drawn = renderer.draw(title, 0, 0);
            }
            if (!drawn && !renderer.refused().empty() && options.screenshot.empty()) {
                // As a frame that can't be drawn: it is asked for again.
                if (!skipped) std::fprintf(stderr, "chr: can't draw: %s\n", renderer.refused().c_str());
                skipped = true;
            } else if (!drawn && !reported) {
                std::fprintf(stderr, "chr: %s\n", (renderer.refused().empty() ? assets.last_error() : renderer.refused()).c_str());
                reported = true;
                status = 1;
                if (!options.screenshot.empty()) running = false;
            }
            if (!options.screenshot.empty() && running) {
                if (!save_screenshot(renderer, options.screenshot)) {
                    std::fprintf(stderr, "chr: screenshot: %s\n", SDL_GetError());
                    status = 1;
                }
                running = false;
            }
            renderer.end_frame();
            if (running) SDL_Delay(16);
        }
        if (!options.loop_keys.empty() && town) {
            totals.shown += town->events().size(), totals.sounds += sounds_in(town->events().size());
            std::printf("loop %zu places, %zu of %zu events played, %zu of %zu sounds started, %zu keys left\n", totals.places, totals.played,
                        totals.shown, totals.started, totals.sounds, loop_keys.size());
            if (options.loop_resets)
                std::printf("loop %d resets, %zu textures now\n", renderer.resets(), renderer.textures());
        }
    }

    audio_opened = audio_opened || SDL_WasInit(SDL_INIT_AUDIO) != 0;
    if (options.audio_report) std::printf("audio opened %d\n", audio_opened ? 1 : 0);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return status;
}
