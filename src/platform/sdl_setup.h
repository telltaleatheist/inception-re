// The program's side of the setup (setup/setup.h is the recipe): the
// launcher contract's --status and --setup, the first start's search and
// screen, and the player's folder settled under its family name.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "setup/old_names.h"

#include <rekit/sdl/game_menu.h>
#include <rekit/sdl/setup_screen.h>

namespace chr::platform {

// This game's recipe for the kit's setup, as the screen and the launcher's served setup use it, for the
// player's folder `user` (tests/setup_download_test.cpp).
rekit::sdl::Game setup_kit_game(const std::string& user);

// No player at this run: a hidden or scripted one (CHR_HIDDEN, an offscreen
// or dummy video driver, or `scripted`: --script or --screenshot). No picker,
// message box or window that takes focus opens then, and on macOS the
// program runs as a background app.
bool setup_unattended(bool scripted = false);

// What the player must see though a start has no console: `text` on stderr, and in a message box
// unless the run is unattended.
void tell_player(const std::string& text, bool unattended);

// What shows the box; a test puts its own there.
std::function<void(const std::string& text)>& message_box();

// `work`'s answer; when it throws, the exit for a start that failed (1, or with `setup_only` the
// launcher contract's 2 and its line), with why told to the player.
int guarded(const std::function<int()>& work, const std::function<bool()>& setup_only,
            const std::function<bool()>& unattended);

// Where 1.0.0 left saved games under a name it misread (setup::left_saves), told to the player: in
// the message box too while nothing is set up in `user`. Nothing is moved or changed.
void tell_left_saves(const std::string& user, const setup::PageText& page, bool unattended);

// --status: one JSON line on stdout, exit 0; no window, nothing written.
// `given`: the game folder the run names (--game-dir, CHR_GAME_DIR), else
// "" for the setup's; `builtin`: the build's own default, may be "";
// `data_dir`: --data-dir, else "" for the player's folder.
int print_setup_status(const std::string& given, const std::string& builtin, const std::string& data_dir);

// `--setup --describe | --search | --copies | --look <pick> | --install <pick> [--folder <dir>]`:
// the setup for the launcher's own screens (the kit's rekit/sdl/setup_serve.h; --copies lists
// every copy on this computer, the one in use marked, setup::copies). JSON lines on stdout and
// nothing else there, the last one `done`; no window and no picker. A "cancel" line on stdin stops
// a look or an install. served_ask: whether `arg`, after --setup, is one of these. serve_game_setup
// answers: `args` are the ask and its arguments, `data_dir` is --data-dir, else "" for the
// player's folder, `builtin` the build's own game folder (as --status takes it). What it installs is remembered as after the setup screen. Returns the
// contract's exit; wrong arguments get a last line saying so and a failed exit.
bool served_ask(const std::string& arg);
int serve_game_setup(const std::vector<std::string>& args, const std::string& data_dir, const std::string& builtin);

// The player's folder for this start (setup::settle_user_folder) made and
// returned, ending in a separator: SDL's per-user folder for the family's
// names, with an earlier name's folder renamed to it once. Logged on stderr.
// "" when SDL has none.
std::string settled_user_folder();

// Whether this run may go on to the setup; why not is said as resolve_game says it. The setup's
// test hooks (CHR_SETUP_PICK, REKIT_SETUP_GO) copy a game into the player's folder with no player at the run, so they need
// CHR_DATA_DIR to name that folder: HOME doesn't move it on every system (on macOS SDL asks the
// system, not HOME). To be asked before settled_user_folder, which makes the folder.
bool setup_hooks_allowed(bool setup_only);

// How a start comes to its game folder.
enum class SetupMode {
    play,        // a start: the setup only when no copy is found
    setup_only,  // --setup: the setup whatever is found, nothing played (the launcher's)
    then_play,   // --setup --then-play: the setup whatever is found, then the game (the menu's CHANGE)
};

// The game folder a start plays from, into `folder`: the one remembered in
// `user`'s settings.ini or `builtin` (a build's own default, may be "") while
// still the game, the player's own install, else the setup (a find offered,
// or the player picks a copy, its folder or the archive it came in, which is
// copied into the player's folder) and remembered in settings.ini. With
// `mode` setup_only or then_play the screen whatever is found. Returns the
// launcher contract's exit: 0 ready, 1 the player cancelled, 2 failed (with
// --setup the reason goes to stdout as {"error": ...}).
int resolve_game(SetupMode mode, bool scripted, const std::string& builtin, const std::string& user,
                 std::string* folder);

// What the in-game menu (platform/menu_layer.h) says and does for this game, playing `game` with the
// player's folder `user` (the data folder: --data-dir, CHR_DATA_DIR, else the settled one), the saves in
// `saves` and the log in `log` ("" none): its words, the file check, the paths and UPDATES. The host
// fills in the window, the drawing, the events, the pause and the ways out.
rekit::sdl::MenuHooks menu_hooks(const std::string& game, const std::string& user, const std::string& saves,
                                 const std::string& log);
// What UPDATES backs up before an update, as it is at the moment it is asked (the menu's keep_now): the saved
// games Game1 to Game6 in `saves` that exist and settings.ini in `user`.
std::vector<std::string> update_keep(const std::string& user, const std::string& saves);
// The menu's look: the setup screen's.
rekit::sdl::Theme menu_theme();

}  // namespace chr::platform
