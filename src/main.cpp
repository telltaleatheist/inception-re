// chr: the rebuild's program. It starts as the original does (the Infocom
// screen, the title, "Is this your first time playing?") and goes on to the
// new game's town, walking with the arrow keys and Home/PgUp/End/PgDn. With
// --map N it skips the start-up and begins on map N.
//
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
// missile's, an explosion's) N display frames (default 1). In a window the town runs at the
// original's pace, one main-loop pass per display frame of a VGA card
// (game/clock.h).
//
// The game folder is --game-dir, else the CHR_GAME_DIR environment variable,
// else the folder set when the program was built.

#include <algorithm>
#include <cmath>
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

#include "formats/game_files.h"
#include "formats/import.h"
#include "formats/mtp.h"
#include "formats/palette.h"
#include "formats/tables.h"
#include "game/asset_id.h"
#include "game/clock.h"
#include "game/map_view.h"
#include "game/text_provider.h"
#include "game/town.h"
#include "platform/map_layer.h"
#include "platform/original_assets.h"
#include "platform/overhead_layer.h"
#include "platform/panel_layer.h"
#include "platform/quiz_layer.h"
#include "platform/stats_layer.h"
#include "platform/renderer.h"
#include "platform/sound_player.h"
#include "platform/text_layer.h"

namespace {

struct Options {
    std::filesystem::path game_dir;
    std::string screenshot;  // write one frame here and quit
    std::string script;      // keys to play on the map first
    std::string text_file;   // text overrides
    std::filesystem::path data_dir;  // the imported text and font
    std::filesystem::path save_dir;  // the saved games GameN; the data folder by default
    bool import = false;
    bool state = false;      // print the game state after the script
    bool fight_state = false;  // with it, the fight's tables
    int event = -1;            // --event N
    int step_hold = 1;         // --step-hold N
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

bool parse(int argc, char** argv, Options& options) {
    if (const char* env = std::getenv("CHR_GAME_DIR"); env && *env)
        options.game_dir = env;
    else
        options.game_dir = CHR_DEFAULT_GAME_DIR;
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
        } else if (arg == "--step-hold" && i + 1 < argc) {
            options.step_hold = std::max(1, std::atoi(argv[++i]));
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
            std::fprintf(stderr, "usage: chr [--game-dir DIR] [--data-dir DIR] [--save-dir DIR] [--import] [--map N] [--script KEYS]\n"
                         "           [--text FILE] [--screenshot FILE.bmp] [--event N] [--events] [--walks] [--walk-to X,Y] [--step-hold N]\n"
                         "           [--state] [--show-quiz]\n"
                         "  --show-quiz: show the training center's quiz (no check: the right part is\n"
                         "           picked and any answer goes on); by default it is skipped\n"
                         "  test aids (docs/reference.md): [--set-flag NN=VV] sets a flag (hex) as the town\n"
                         "           starts; a script's set:NN=VV sets one at that point;\n"
                         "           [--window X,Y] (hex, repeatable) prints the cell window built there\n");
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

bool save_screenshot(SDL_Renderer* renderer, const std::string& path) {
    SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
    if (!shot) return false;
    const bool ok = SDL_SaveBMP(shot, path.c_str());
    SDL_DestroySurface(shot);
    return ok;
}

// Map `number` and where the party starts on it: the new-game start for
// map 1, the map's middle for the others.
bool load_map(const std::filesystem::path& game_dir, int number, chr::formats::MtpMap& map,
              chr::formats::MapPosition& start) {
    const std::string name = "MAP" + std::to_string(number) + ".MTP";
    try {
        const auto path = chr::formats::find_game_file(game_dir, name);
        if (!path) {
            std::fprintf(stderr, "chr: %s not found in %s\n", name.c_str(), game_dir.string().c_str());
            return false;
        }
        map = chr::formats::decode_mtp(chr::formats::read_file(*path));
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

// The user's data folder, "inception-re" in the application-data folder.
// Its earlier name was "crescent-hawks-rebuild": if that one exists and the
// new one is empty, the old one is moved to the new name once, so its import
// and saves carry over; if the move fails, the old folder is used in place.
std::optional<std::filesystem::path> data_folder() {
    namespace fs = std::filesystem;
    char* pref = SDL_GetPrefPath("", "inception-re");  // creates it if missing
    if (!pref) return std::nullopt;
    const fs::path folder = fs::path(pref).parent_path();
    SDL_free(pref);
    const fs::path old = folder.parent_path() / "crescent-hawks-rebuild";
    std::error_code ec;
    if (!fs::is_directory(old, ec) || !fs::is_empty(folder, ec) || ec) return folder;
    fs::remove(folder, ec);
    if (!ec) fs::rename(old, folder, ec);
    if (ec) {
        std::fprintf(stderr, "chr: couldn't rename %s to %s (%s); using the old folder\n",
                     old.string().c_str(), folder.string().c_str(), ec.message().c_str());
        return old;
    }
    std::fprintf(stderr, "chr: renamed the data folder %s to %s\n", old.string().c_str(), folder.string().c_str());
    return folder;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse(argc, argv, options)) return 2;

    if (options.data_dir.empty()) {
        if (const char* env = std::getenv("CHR_DATA_DIR"); env && *env) {
            options.data_dir = env;
        } else if (auto folder = data_folder()) {
            options.data_dir = *folder;
        } else {
            std::fprintf(stderr, "chr: no data folder (%s); use --data-dir\n", SDL_GetError());
            return 1;
        }
    }
    try {
        if (options.import) {
            chr::formats::import_original(options.game_dir, options.data_dir);
            std::printf("imported the text and font into %s\n", options.data_dir.string().c_str());
            return 0;
        }
        if (chr::formats::ensure_imported(options.game_dir, options.data_dir))
            std::fprintf(stderr, "chr: imported the text and font into %s\n", options.data_dir.string().c_str());
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
    // A run with a script or a screenshot (the rig, the tests) never takes
    // the foreground on macOS, whatever video driver it gets.
    if (headless || !options.script.empty())
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "chr: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* sdl_renderer = nullptr;
    const SDL_WindowFlags window_flags = headless ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE;
    if (!SDL_CreateWindowAndRenderer("Inception RE", 960, 600, window_flags,
                                     &window, &sdl_renderer)) {
        std::fprintf(stderr, "chr: can't open a window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int status = 0;
    bool audio_opened = false;  // --audio-report
    chr::formats::MtpMap map;
    chr::formats::MapPosition start;
    std::deque<chr::game::ScriptItem> keys;  // every key and idle stretch so far, the script's first
    // Without --map the program starts up as the original does, and the new
    // game begins on map 1.
    const bool start_up = options.map == 0;
    if (start_up) options.map = 1;
    if (!load_map(options.game_dir, options.map, map, start)) status = 1;
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
        const auto palette = chr::formats::standard_palette_ega(tables);
        chr::platform::OriginalAssets assets(options.game_dir, options.data_dir, palette,
                                             {{"ENDMECH.CMP", chr::formats::palette_ega(tables.endmech_palette)}});
        chr::platform::Renderer renderer(sdl_renderer, assets);
        const auto& title = chr::assets::kTitleScreen;

        // The town takes keys and idle passes as they come. A question asks
        // for its answer in the middle of a turn; when it is left waiting,
        // the town is played again from the start with the answer (replay),
        // which keeps the logic a plain sequence.
        std::unique_ptr<chr::game::Town> town;
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
            const auto path = chr::formats::find_game_file(options.game_dir, "MAP" + std::to_string(n) + ".MTP");
            if (!path) continue;
            try {
                map_text.add(chr::formats::decode_mtp(chr::formats::read_file(*path)), n);
            } catch (const std::exception&) {
            }
        }
        const chr::game::LayeredText texts({&overrides, &imported, &map_text, &interface_default});
        auto replay = [&]() -> bool {
            try {
                town = std::make_unique<chr::game::Town>(map, options.map, start, texts, tables);
                town->set_game_dir(options.game_dir);
                town->set_save_dir(options.save_dir.empty() ? options.data_dir : options.save_dir);
                if (rules) town->set_rules(&*rules);
                town->set_show_quiz(options.show_quiz);
                if (start_up) town->start_up();
                else town->draw_panel();
                for (const auto& [f, v] : options.set_flags) town->set_flag_when_town_starts(f, v);
                chr::game::ScriptKeys source(keys);
                source.set_poke([&](bool character, int r, int b, int v) {
                    if (r == -4) town->poke_position(b, v);
                    else if (r == -3) town->poke_heat(b, v);
                    else if (r == -2) town->poke_random(v);
                    else if (r < 0) town->poke_cbills(v);
                    else if (character) town->poke_character(r, b, v);
                    else town->poke_mech(r, b, v);
                });
                town->run(source);
                return true;
            } catch (const std::exception& e) {
                std::fprintf(stderr, "chr: %s\n", e.what());
                town.reset();
                return false;
            }
        };
        if (options.map != 0 && status == 0) {
            if (!replay()) {
                status = 1;
            } else if (!options.script.empty()) {
                for (const auto& line : town->log()) std::printf("%s\n", line.c_str());
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
                for (const auto& e : town->events()) {
                    if (e.screen || e.segments.empty()) continue;
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
                for (const auto& e : town->events()) {
                    if (!e.screen) std::printf("event sound %d\n", e.sound);
                    else std::printf("event screen %d frames, %zu sprites, %zu lines%s\n", e.frames,
                                     e.screen->sprites.size(), e.screen->lines.size(), e.screen->picture ? ", a picture" : "");
                }
            }
        }

        // What the logic showed while it ran on (game/shown.h) is played
        // when it stops: `played` events are done, the current one has been
        // up `held` frames. Keys pressed meanwhile wait until the end; then
        // the logic resumes with them. The script's own are not played.
        std::size_t played = town ? town->events().size() : 0;
        if (town && options.event >= 0 && std::size_t(options.event) < played) played = std::size_t(options.event);
        int held = 0;
        bool keys_meanwhile = false;
        // Sound only for a player at the keyboard: a run with a script or a
        // screenshot (the rig, the tests) never opens an audio device.
        std::unique_ptr<chr::platform::WavSounds> sound_mods;
        std::unique_ptr<chr::platform::SoundPlayer> sound_player;
        if (options.script.empty() && options.screenshot.empty()) {
            if (!options.sound_mod.empty()) sound_mods = std::make_unique<chr::platform::WavSounds>(options.sound_mod);
            sound_player = std::make_unique<chr::platform::SoundPlayer>(sound_mods.get());
        }
        audio_opened = audio_opened || SDL_WasInit(SDL_INIT_AUDIO) != 0;
        std::size_t sounded = played;  // the events before it have had their sound started
        bool music_on = false;  // the title's tune has been started
        const auto playing = [&] { return town && played < town->events().size(); };
        const auto resume = [&] {
            if (keys_meanwhile) {
                keys_meanwhile = false;
                replay();
            } else if (town && town->poll_open()) {
                // 203b:1015 asked whether a key is waiting: none was.
                if (!keys.empty() && keys.back().idle > 0) ++keys.back().idle;
                else keys.push_back({0, 1});
                replay();
            }
        };

        bool running = status == 0;
        // A script under a headless video driver has no one to press
        // Escape: without a screenshot to take, it ends with the script.
        if (!options.script.empty() && options.screenshot.empty()) {
            const std::string_view driver = SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "";
            if (driver == "dummy" || driver == "offscreen") running = false;
        }
        bool reported = false;
        chr::game::FrameClock clock;
        Uint64 last = SDL_GetTicksNS();
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type != SDL_EVENT_KEY_DOWN) continue;
                if (event.key.key == SDLK_ESCAPE) running = false;
                if (const int k = key_code(event.key.key); k != 0 && town && playing()) {
                    keys.push_back({k, 0});
                    keys_meanwhile = true;
                } else if (k != 0 && town) {
                    keys.push_back({k, 0});
                    if (town->question_open() || town->place_open() || town->start_up_open()) {
                        replay();
                    } else {
                        chr::game::ScriptKeys none({});
                        town->key(k, none);
                    }
                }
            }
            const Uint64 now = SDL_GetTicksNS();
            const int frames = clock.advance(double(now - last) / 1e9);
            last = now;
            if (playing() && options.screenshot.empty()) {
                held += frames;
                const auto& events = town->events();
                while (played < events.size()) {
                    // The game waits while a sound plays (docs/sound.md). A
                    // screen the original showed without a wait went by as
                    // fast as the machine drew the next: the platform holds
                    // it `step_hold` frames.
                    const auto& e = events[played];
                    if (played >= sounded) {
                        if (sound_player && !e.screen && !e.segments.empty()) sound_player->play(e.sound, e.segments);
                        sounded = played + 1;
                    }
                    const int cost = !e.screen ? int(std::ceil(e.sound_ms() * chr::game::kVgaRefreshHz / 1000.0))
                                     : e.frames > 0 ? e.frames
                                                    : options.step_hold;
                    if (held < cost) break;
                    held -= cost;
                    ++played;
                }
                if (played == events.size()) {
                    held = 0;
                    resume();
                }
            } else if (town && options.screenshot.empty() && town->poll_open()) {
                resume();
            } else if (town && frames > 0 && town->start_up_open() && town->full_screen() == chr::assets::kInfocomLogo &&
                options.screenshot.empty()) {
                // The Infocom screen goes by itself after a while: time is
                // input there too.
                if (!keys.empty() && keys.back().idle > 0) keys.back().idle += frames;
                else keys.push_back({0, frames});
                replay();
            } else if (town && frames > 0 && !town->question_open() && !town->place_open() &&
                       !town->start_up_open() && options.screenshot.empty()) {
                for (int i = 0; i < frames; ++i) town->idle_pass();
                if (!keys.empty() && keys.back().idle > 0) keys.back().idle += frames;
                else keys.push_back({0, frames});
            }
            // The title's tune plays while the title waits for its key
            // (1000:476d); the key cuts it off.
            if (sound_player) {
                const bool tune = town && town->title_tune_playing();
                if (tune && !music_on) sound_player->play_music(chr::game::music_segments(town->title_tune()));
                if (!tune && music_on) sound_player->stop_music();
                music_on = tune;
            }
            renderer.begin_frame();
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
            if (!drawn && !reported) {
                std::fprintf(stderr, "chr: %s\n", assets.last_error().c_str());
                reported = true;
                status = 1;
                if (!options.screenshot.empty()) running = false;
            }
            if (!options.screenshot.empty() && running) {
                if (!save_screenshot(sdl_renderer, options.screenshot)) {
                    std::fprintf(stderr, "chr: screenshot: %s\n", SDL_GetError());
                    status = 1;
                }
                running = false;
            }
            renderer.end_frame();
            if (running) SDL_Delay(16);
        }
    }

    audio_opened = audio_opened || SDL_WasInit(SDL_INIT_AUDIO) != 0;
    if (options.audio_report) std::printf("audio opened %d\n", audio_opened ? 1 : 0);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return status;
}
