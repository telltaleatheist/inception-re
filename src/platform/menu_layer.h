// The kit's setup menu over the running game (rekit/sdl/game_menu.h): Shift+Alt+F1 (Cmd+Shift+F1 on the
// Mac) at any moment, Esc back to the game. This is the host's half: the menu's picture drawn over each
// frame, the events handed through it, the game's time stopped while it is open, and the ways out (QUIT,
// and CHANGE's setup started again in its own process). What the menu says and does for this game is
// the recipe (platform/sdl_setup.h, menu_hooks).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <rekit/sdl/game_menu.h>

namespace chr::platform {

class MenuLayer {
public:
    // What the game does for the menu.
    struct Game {
        std::function<void(const SDL_Event&)> deliver;  // an event that is the game's, in order
        std::function<void()> pause, resume;            // beyond the layer's own paused(): the sound
        std::function<void()> quit;                     // the game's clean exit, after the event loop
        std::vector<std::string> setup_args;            // CHANGE: this program started again with them
    };
    // `hooks`: the recipe's; the layer fills in the window, the drawing, the events, the pause and the
    // ways out.
    MenuLayer(SDL_Window* window, rekit::sdl::MenuHooks hooks, const rekit::sdl::Theme& theme, Game game);
    ~MenuLayer();
    MenuLayer(const MenuLayer&) = delete;
    MenuLayer& operator=(const MenuLayer&) = delete;

    // Every SDL event, first: the game's are handed on to Game::deliver.
    void event(const SDL_Event& event);
    // After the game's frame, before it is shown, on the window's renderer at its output size: the menu
    // (or the hint) over the whole of it, when either shows.
    void draw_over(SDL_Renderer* renderer);
    bool open() const;
    // Open, so the game's time stands still: nothing of it is caught up after.
    bool paused() const { return paused_; }
    // CHANGE started the setup: the game closes (Game::quit was called).
    bool closing_for_setup() const { return closing_for_setup_; }

private:
    void draw(const rekit::sdl::MenuImage& image);
    void drop_picture();

    Game game_;
    std::unique_ptr<rekit::sdl::GameMenu> menu_;
    SDL_Renderer* drawing_on_ = nullptr;  // the renderer of the frame being drawn
    SDL_Texture* picture_ = nullptr;      // the menu's last picture on the device
    std::uint64_t serial_ = 0;
    int width_ = 0, height_ = 0;
    bool paused_ = false;
    bool closing_for_setup_ = false;
};

}  // namespace chr::platform
