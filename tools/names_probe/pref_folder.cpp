// chr's own main.cpp with a player folder the probe names: its settled_user_folder() is this one (a macro renames it
// for main.cpp alone, CMakeLists.txt). PROBE_PREF is the bytes SDL_GetPrefPath would have given, in hex; the rest is
// what platform/sdl_setup.cpp does with them.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "setup/setup.h"

namespace chr::platform {

std::string probe_user_folder() {
    const char* given = std::getenv("PROBE_PREF");
    std::string folder;
    for (const char* p = given ? given : ""; p[0] && p[1]; p += 2) folder += char(std::stoi(std::string(p, 2), nullptr, 16));
    const setup::Settled s = setup::settle_user_folder(folder);
    return s.folder;
}

}  // namespace chr::platform
