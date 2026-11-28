#include "platform/menu_layer.h"

#include <cstdio>
#include <utility>

namespace chr::platform {

MenuLayer::MenuLayer(SDL_Window* window, rekit::sdl::MenuHooks hooks, const rekit::sdl::Theme& theme, Game game)
    : game_(std::move(game)) {
    hooks.window = window;
    hooks.draw = [this](const rekit::sdl::MenuImage& image) { draw(image); };
    hooks.deliver = [this](const SDL_Event& event) {
        if (game_.deliver) game_.deliver(event);
    };
    hooks.pause = [this] {
        paused_ = true;
        if (game_.pause) game_.pause();
    };
    hooks.resume = [this] {
        paused_ = false;
        if (game_.resume) game_.resume();
    };
    hooks.quit = [this] {
        if (game_.quit) game_.quit();
    };
    hooks.restart_for_setup = [this] {
        // Started now, while this one is still up: if it can't be, the game goes on rather than leave
        // the player with nothing.
        if (!rekit::sdl::relaunch_for_setup(game_.setup_args)) {
            std::fprintf(stderr, "chr: menu: the setup could not be started; the game goes on\n");
            return;
        }
        std::printf("chr: menu: the game closes for its setup\n");
        std::fflush(stdout);
        closing_for_setup_ = true;
        if (game_.quit) game_.quit();
    };
    menu_ = std::make_unique<rekit::sdl::GameMenu>(std::move(hooks), theme);
}

MenuLayer::~MenuLayer() {
    menu_.reset();
    drop_picture();
}

void MenuLayer::event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_RENDER_DEVICE_RESET) drop_picture();  // gone with the device
    menu_->event(event);
}

void MenuLayer::draw_over(SDL_Renderer* renderer) {
    drawing_on_ = renderer;
    menu_->frame();
    drawing_on_ = nullptr;
}

bool MenuLayer::open() const { return menu_->is_open(); }

void MenuLayer::drop_picture() {
    if (picture_) SDL_DestroyTexture(picture_);
    picture_ = nullptr;
    serial_ = 0;
    width_ = height_ = 0;
}

// The kit's picture over the whole output, uploaded only when it changed.
void MenuLayer::draw(const rekit::sdl::MenuImage& image) {
    if (!drawing_on_ || !image.pixels || image.w <= 0 || image.h <= 0) return;
    if (!picture_ || image.w != width_ || image.h != height_ || SDL_GetRendererFromTexture(picture_) != drawing_on_) {
        drop_picture();
        picture_ = SDL_CreateTexture(drawing_on_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, image.w, image.h);
        if (!picture_ || !SDL_SetTextureBlendMode(picture_, SDL_BLENDMODE_BLEND) ||
            !SDL_SetTextureScaleMode(picture_, SDL_SCALEMODE_NEAREST)) {
            drop_picture();
            return;
        }
        width_ = image.w, height_ = image.h;
    }
    if (image.serial != serial_ || serial_ == 0) {
        if (!SDL_UpdateTexture(picture_, nullptr, image.pixels, image.pitch)) {
            drop_picture();
            return;
        }
        serial_ = image.serial;
    }
    SDL_RenderTexture(drawing_on_, picture_, nullptr, nullptr);
}

}  // namespace chr::platform
