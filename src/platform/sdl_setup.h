// The program's side of the setup (setup/setup.h is the recipe): the
// launcher contract's --status and --setup, the first start's search and
// screen, and the player's folder settled under its family name.
#pragma once

#include <string>

namespace chr::platform {

// No player at this run: a hidden or scripted one (CHR_HIDDEN, an offscreen
// or dummy video driver, or `scripted`: --script or --screenshot). No picker,
// message box or window that takes focus opens then, and on macOS the
// program runs as a background app.
bool setup_unattended(bool scripted = false);

// --status: one JSON line on stdout, exit 0; no window, nothing written.
// `given`: the game folder the run names (--game-dir, CHR_GAME_DIR), else
// "" for the setup's; `builtin`: the build's own default, may be "";
// `data_dir`: --data-dir, else "" for the player's folder.
int print_setup_status(const std::string& given, const std::string& builtin, const std::string& data_dir);

// The player's folder for this start (setup::settle_user_folder) made and
// returned, ending in a separator: SDL's per-user folder for the family's
// names, with an earlier name's folder renamed to it once. Logged on stderr.
// "" when SDL has none.
std::string settled_user_folder();

// The game folder a start plays from, into `folder`: the one remembered in
// `user`'s settings.ini or `builtin` (a build's own default, may be "") while
// still the game, the player's own install, else the setup (a find offered,
// or the player picks a copy, its folder or the archive it came in, which is
// copied into the player's folder) and remembered in settings.ini.
// `setup_only` (--setup): the screen whatever is found. Returns the launcher
// contract's exit: 0 ready, 1 the player cancelled, 2 failed (with --setup
// the reason goes to stdout as {"error": ...}).
int resolve_game(bool setup_only, bool scripted, const std::string& builtin, const std::string& user,
                 std::string* folder);

}  // namespace chr::platform
