// The kit's menu over the game, the host's half (platform/menu_layer.h) with this game's recipe
// (platform/sdl_setup.h, menu_hooks): its keys open it and stop the game's time, the game's events go
// through while it is closed, its picture is drawn over the frame, Esc closes it. A hidden window on
// SDL's dummy video driver: nothing on the screen.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "platform/menu_layer.h"
#include "platform/sdl_setup.h"
#include "setup/setup.h"
#include "test.h"

using namespace chr::platform;

namespace {

// Both the process's environment and SDL's copy of it (the kit reads SDL's).
void set_env(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
    SDL_Environment* env = SDL_GetEnvironment();
    if (value) SDL_SetEnvironmentVariable(env, name, value, true);
    else SDL_UnsetEnvironmentVariable(env, name);
}

SDL_Event key(SDL_Window* window, SDL_Keycode code, SDL_Scancode scancode, SDL_Keymod mod) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = code, e.key.scancode = scancode, e.key.mod = mod, e.key.down = true;
    e.key.windowID = SDL_GetWindowID(window);
    return e;
}

// The layer's drawing on a surface of `w` x `h` filled with one colour first: whether any pixel changed.
bool draws(MenuLayer& menu, int w, int h) {
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* r = s ? SDL_CreateSoftwareRenderer(s) : nullptr;
    if (!r) {
        SDL_DestroySurface(s);
        return false;
    }
    SDL_SetRenderDrawColor(r, 0, 255, 0, 255);
    SDL_RenderClear(r);
    menu.draw_over(r);
    SDL_RenderPresent(r);
    bool changed = false;
    const auto* px = static_cast<const std::uint8_t*>(s->pixels);
    for (int y = 0; y < h && !changed; y += 7)
        for (int x = 0; x < w && !changed; x += 7) {
            const std::uint8_t* p = px + y * s->pitch + x * 4;
            changed = p[0] != 0 || p[1] != 255 || p[2] != 0;
        }
    SDL_DestroyRenderer(r);
    SDL_DestroySurface(s);
    return changed;
}

}  // namespace

TEST(menu_layer_over_the_game) {
    const std::filesystem::path user = std::filesystem::path(CHR_TEST_SCRATCH) / "menu_layer";
    std::error_code ec;
    std::filesystem::remove_all(user, ec);
    std::filesystem::create_directories(user, ec);
    // The keys are pressed here, which RE_KIT_TEST turns off; RE_LAUNCHER keeps the hint off instead.
    const char* kit_test = std::getenv("RE_KIT_TEST");
    const std::string kept = kit_test ? kit_test : "";
    set_env("RE_KIT_TEST", nullptr);
    set_env("RE_LAUNCHER", "1");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        chrtest::skip(std::string("no dummy video: ") + SDL_GetError());
        return;
    }
    SDL_Window* window = SDL_CreateWindow("menu layer", 640, 400, SDL_WINDOW_HIDDEN);
    CHECK(window != nullptr);
    if (window) {
        // The recipe: this game's words and its file check; no update source in a build that is no release.
        const auto hooks = menu_hooks((user / "no game here").string(), user.string(), user.string(), "");
        CHECK(hooks.title == chr::setup::kTitle);
        CHECK(hooks.report_url == "https://github.com/telltaleatheist/inception-re/issues");
        CHECK(hooks.user_folder == user.string());
        CHECK(hooks.check_game_files && !hooks.check_game_files().empty());
        CHECK(!hooks.reset_settings);  // no settings of the player's but the game folder

        std::vector<std::uint32_t> delivered;
        int pauses = 0, resumes = 0;
        bool quit = false;
        MenuLayer::Game game;
        game.deliver = [&](const SDL_Event& e) { delivered.push_back(e.type); };
        game.pause = [&] { ++pauses; };
        game.resume = [&] { ++resumes; };
        game.quit = [&] { quit = true; };
        game.setup_args = {"--setup", "--then-play", "folder"};
        MenuLayer menu(window, hooks, menu_theme(), std::move(game));
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);

        CHECK(!menu.open() && !menu.paused());
        CHECK(!draws(menu, w, h));  // closed, the hint off: nothing over the frame
        menu.event(key(window, SDLK_UP, SDL_SCANCODE_UP, SDL_KMOD_NONE));
        CHECK(delivered.size() == 1 && delivered[0] == SDL_EVENT_KEY_DOWN);  // the game's key, handed on

        menu.event(key(window, SDLK_F1, SDL_SCANCODE_F1, rekit::sdl::GameMenu::keys()));
        CHECK(menu.open() && menu.paused() && pauses == 1);
        CHECK(draws(menu, w, h));  // open: drawn over the frame
        const std::size_t before = delivered.size();
        menu.event(key(window, SDLK_DOWN, SDL_SCANCODE_DOWN, SDL_KMOD_NONE));
        CHECK(delivered.size() == before);  // open: the menu's, not the game's

        menu.event(key(window, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, SDL_KMOD_NONE));
        CHECK(!menu.open() && !menu.paused() && resumes == 1);
        CHECK(!quit && !menu.closing_for_setup());
        SDL_DestroyWindow(window);
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    set_env("RE_LAUNCHER", nullptr);
    set_env("RE_KIT_TEST", kit_test ? kept.c_str() : nullptr);
    std::filesystem::remove_all(user, ec);
}
