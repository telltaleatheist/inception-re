// Draws asset IDs on the original's 320x200 screen. A frame is drawn off the
// window at a whole number of pixels for each screen unit, at least as many
// as the window shows, and then scaled once, nearest-neighbour, to fill the
// window as far as 16:10 allows (docs/window.md). So whatever is drawn only
// ever meets a whole scale, and art finer than the original's keeps its
// detail. It gets every picture through an AssetProvider and keeps a texture
// per ID.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "formats/palette.h"
#include "game/asset_id.h"
#include "platform/asset_provider.h"

namespace chr::platform {

inline constexpr int kScreenWidth = 320;
inline constexpr int kScreenHeight = 200;

// Where the picture is in a window of this many pixels: as large as fits at
// 16:10, in the middle.
SDL_Rect picture_rect(int width, int height);
// The pixels for each screen unit of the frame that `picture` is scaled
// from: enough that it is never enlarged, but no frame side above
// `max_side`, the largest texture (0: no limit).
int frame_scale(const SDL_Rect& picture, int max_side);

class Renderer {
public:
    // With `fixed_scale`, every frame has that many pixels for each screen
    // unit, whatever the window is (a screenshot's 960x600).
    Renderer(SDL_Renderer* sdl, AssetProvider& assets, int fixed_scale = 0);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // False (SDL_GetError) if there is nothing to draw the frame on for now:
    // a lost device, a refused target. The frame is skipped, with no other
    // call, and the next one starts again from nothing.
    bool begin_frame();
    // After SDL_EVENT_RENDER_DEVICE_RESET no texture of the renderer's is there any more: each is
    // made again when it is next drawn.
    void handle(const SDL_Event& event);
    // What SDL said when it last refused a picture's texture in this frame, "" when it refused none.
    // The picture is asked for again when next drawn.
    const std::string& refused() const { return refused_; }
    // How many resets have been handled, and the pictures' textures there are now.
    int resets() const { return resets_; }
    std::size_t textures() const;
    // Draws the picture for `id` with its top-left corner at (x, y) in
    // 320x200 screen units. Returns false if no provider has it.
    bool draw(const AssetId& id, int x, int y);
    // Only the top `visible_height` screen units of it (a figure standing
    // in water shows less of itself).
    bool draw_top(const AssetId& id, int x, int y, int visible_height);
    // The same, with every pixel's colour multiplied by (r, g, b): a white
    // glyph drawn this way comes out in that colour.
    bool draw_tinted(const AssetId& id, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b);
    // XORs `colour` into the palette index of every pixel drawn so far in a
    // rectangle (320x200 screen units), going by what each pixel shows: the
    // fallback for new art (map_layer's draw_xor_box works on the original's
    // colour numbers). It works on the frame's own pixels, so finer art keeps
    // its detail. A colour shown by two numbers takes the first, and a colour
    // not in `palette` is left as it is.
    bool xor_colours(int x, int y, int width, int height, int colour, const formats::Palette16& palette);
    // The provider's picture for `id` (kept after the first call), or null.
    const Image* source(const AssetId& id);
    // Draws `rgba` (width x height pixels) over a rectangle of the screen.
    void draw_rgba(int x, int y, int width, int height, const std::vector<std::uint8_t>& rgba);
    // A solid rectangle, in 320x200 screen units.
    void fill(int x, int y, int width, int height, std::uint8_t r, std::uint8_t g, std::uint8_t b);
    // Limits drawing to a rectangle of the 320x200 screen; clear_clip() lifts it.
    void clip(int x, int y, int width, int height);
    void clear_clip();
    // The frame drawn so far, at its own size, or null. The caller frees it.
    SDL_Surface* read_frame();
    // Scales the frame to the window and shows it; `over` (if any) draws on the window's renderer at its
    // output size first, over the picture (the in-game menu, platform/menu_layer.h).
    void end_frame(const std::function<void(SDL_Renderer*)>& over = nullptr);
    // The size `id` covers on the 320x200 screen, or nothing if no provider has it.
    std::optional<std::pair<int, int>> logical_size(const AssetId& id);

private:
    SDL_Texture* texture_for(const AssetId& id);
    bool start_frame();

    SDL_Renderer* sdl_;
    AssetProvider& assets_;
    const int fixed_scale_;
    SDL_Texture* frame_ = nullptr;
    int scale_ = 0;  // frame_'s pixels for each screen unit
    void forget_textures();

    struct Entry {
        bool there = false;              // the provider has it
        SDL_Texture* texture = nullptr;  // null: not made yet, or refused
        float logical_width = 0, logical_height = 0;
    };
    std::map<AssetId, Entry> cache_;
    std::string refused_;
    int resets_ = 0;
    std::map<AssetId, std::optional<Image>> sources_;
};

}  // namespace chr::platform
