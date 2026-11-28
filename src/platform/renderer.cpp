#include "platform/renderer.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace chr::platform {

SDL_Rect picture_rect(int width, int height) {
    int w = std::min(width, int(std::lround(double(height) * kScreenWidth / kScreenHeight)));
    int h = std::min(height, int(std::lround(double(width) * kScreenHeight / kScreenWidth)));
    // A window held to 16:10 has a whole number of points at each side, each
    // up to three pixels: what that leaves is filled.
    constexpr int kRounding = 3;
    if (width - w <= kRounding) w = width;
    if (height - h <= kRounding) h = height;
    return {(width - w) / 2, (height - h) / 2, w, h};
}

int frame_scale(const SDL_Rect& picture, int max_side) {
    const int scale = std::max((picture.w + kScreenWidth - 1) / kScreenWidth, (picture.h + kScreenHeight - 1) / kScreenHeight);
    const int most = max_side > 0 ? max_side / kScreenWidth : scale;
    return std::max(1, std::min(scale, most));
}

Renderer::Renderer(SDL_Renderer* sdl, AssetProvider& assets, int fixed_scale)
    : sdl_(sdl), assets_(assets), fixed_scale_(fixed_scale) {
    SDL_SetDefaultTextureScaleMode(sdl_, SDL_SCALEMODE_NEAREST);
}

Renderer::~Renderer() {
    forget_textures();
}

void Renderer::forget_textures() {
    SDL_SetRenderTarget(sdl_, nullptr);
    if (frame_) SDL_DestroyTexture(frame_);
    frame_ = nullptr;
    for (auto& [id, entry] : cache_) {
        if (entry.texture) SDL_DestroyTexture(entry.texture);
        entry.texture = nullptr;
    }
}

void Renderer::handle(const SDL_Event& event) {
    if (event.type != SDL_EVENT_RENDER_DEVICE_RESET) return;
    ++resets_;
    forget_textures();
}

std::size_t Renderer::textures() const {
    return std::size_t(std::count_if(cache_.begin(), cache_.end(), [](const auto& c) { return c.second.texture != nullptr; }));
}

bool Renderer::begin_frame() {
    refused_.clear();
    if (start_frame()) return true;
    // Whatever failed (a lost device, a refused target), the next frame
    // starts again from nothing.
    const std::string error = SDL_GetError();
    SDL_SetRenderTarget(sdl_, nullptr);
    if (frame_) SDL_DestroyTexture(frame_);
    frame_ = nullptr;
    SDL_SetError("%s", error.c_str());
    return false;
}

bool Renderer::start_frame() {
    SDL_SetRenderTarget(sdl_, nullptr);
    int scale = fixed_scale_;
    if (scale == 0) {
        int width = 0, height = 0;
        if (!SDL_GetCurrentRenderOutputSize(sdl_, &width, &height)) return false;
        // A window with no pixels (minimised) keeps the frame it has.
        scale = std::max(1, scale_);
        if (width > 0 && height > 0) {
            const auto side = SDL_GetNumberProperty(SDL_GetRendererProperties(sdl_), SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
            scale = frame_scale(picture_rect(width, height), int(side));
        }
    }
    if (!frame_ || scale != scale_) {
        if (frame_) SDL_DestroyTexture(frame_);
        scale_ = scale;
        frame_ = SDL_CreateTexture(sdl_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, kScreenWidth * scale,
                                   kScreenHeight * scale);
        if (!frame_) return false;
        SDL_SetTextureScaleMode(frame_, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(frame_, SDL_BLENDMODE_NONE);
    }
    // The scale belongs to the target, so it is set with the frame on.
    if (!SDL_SetRenderTarget(sdl_, frame_) || !SDL_SetRenderScale(sdl_, float(scale_), float(scale_))) return false;
    SDL_SetRenderDrawColor(sdl_, 0, 0, 0, 255);
    return SDL_RenderClear(sdl_);
}

SDL_Texture* Renderer::texture_for(const AssetId& id) {
    if (const auto it = cache_.find(id); it != cache_.end() && (it->second.texture || !it->second.there)) return it->second.texture;
    const auto image = assets_.image(id);
    Entry& entry = cache_[id];
    if (!image) return nullptr;
    entry.there = true;
    entry.logical_width = float(image->logical_width);
    entry.logical_height = float(image->logical_height);
    SDL_Texture* texture = SDL_CreateTexture(sdl_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image->width, image->height);
    if (!texture || !SDL_UpdateTexture(texture, nullptr, image->rgba.data(), image->width * 4)) {
        refused_ = SDL_GetError();
        if (texture) SDL_DestroyTexture(texture);
        return nullptr;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return entry.texture = texture;
}

bool Renderer::draw(const AssetId& id, int x, int y) {
    SDL_Texture* texture = texture_for(id);
    if (!texture) return false;
    const Entry& entry = cache_.at(id);
    const SDL_FRect dest{float(x), float(y), entry.logical_width, entry.logical_height};
    return SDL_RenderTexture(sdl_, texture, nullptr, &dest);
}

bool Renderer::draw_top(const AssetId& id, int x, int y, int visible_height) {
    SDL_Texture* texture = texture_for(id);
    if (!texture) return false;
    const Entry& entry = cache_.at(id);
    if (visible_height >= entry.logical_height) return draw(id, x, y);
    if (visible_height <= 0) return true;
    float tw = 0, th = 0;
    SDL_GetTextureSize(texture, &tw, &th);
    const SDL_FRect src{0, 0, tw, th * float(visible_height) / entry.logical_height};
    const SDL_FRect dest{float(x), float(y), entry.logical_width, float(visible_height)};
    return SDL_RenderTexture(sdl_, texture, &src, &dest);
}

bool Renderer::draw_tinted(const AssetId& id, int x, int y, std::uint8_t r, std::uint8_t g,
                           std::uint8_t b) {
    SDL_Texture* texture = texture_for(id);
    if (!texture) return false;
    SDL_SetTextureColorMod(texture, r, g, b);
    const bool ok = draw(id, x, y);
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    return ok;
}

void Renderer::fill(int x, int y, int width, int height, std::uint8_t r, std::uint8_t g,
                    std::uint8_t b) {
    SDL_SetRenderDrawColor(sdl_, r, g, b, 255);
    const SDL_FRect rect{float(x), float(y), float(width), float(height)};
    SDL_RenderFillRect(sdl_, &rect);
}

void Renderer::clip(int x, int y, int width, int height) {
    const SDL_Rect rect{x, y, width, height};
    SDL_SetRenderClipRect(sdl_, &rect);
}

void Renderer::clear_clip() { SDL_SetRenderClipRect(sdl_, nullptr); }

std::optional<std::pair<int, int>> Renderer::logical_size(const AssetId& id) {
    texture_for(id);
    const Entry& entry = cache_.at(id);
    if (!entry.there) return std::nullopt;
    return std::pair{int(entry.logical_width), int(entry.logical_height)};
}

bool Renderer::xor_colours(int x, int y, int width, int height, int colour, const formats::Palette16& palette) {
    const SDL_Rect screen{0, 0, kScreenWidth, kScreenHeight}, wanted{x, y, width, height};
    SDL_Rect box{};
    if (!SDL_GetRectIntersection(&wanted, &screen, &box)) return true;
    const SDL_Rect area{box.x * scale_, box.y * scale_, box.w * scale_, box.h * scale_};
    SDL_Surface* read = SDL_RenderReadPixels(sdl_, &area);
    if (!read) return false;
    SDL_Surface* rgba = SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(read);
    if (!rgba || rgba->w != area.w || rgba->h != area.h) {
        SDL_DestroySurface(rgba);
        return false;
    }
    for (int py = 0; py < area.h; ++py)
        for (int px = 0; px < area.w; ++px) {
            auto* p = static_cast<std::uint8_t*>(rgba->pixels) + py * rgba->pitch + px * 4;
            for (int i = 0; i < 16; ++i)
                if (palette[std::size_t(i)] == formats::Rgb{p[0], p[1], p[2]}) {
                    const auto& c = palette[std::size_t(i ^ (colour & 15))];
                    p[0] = c.r, p[1] = c.g, p[2] = c.b;
                    break;
                }
            p[3] = 255;
        }
    SDL_Texture* t = SDL_CreateTexture(sdl_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, area.w, area.h);
    if (t) {
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
        SDL_UpdateTexture(t, nullptr, rgba->pixels, rgba->pitch);
        const SDL_FRect dst{float(box.x), float(box.y), float(box.w), float(box.h)};
        SDL_RenderTexture(sdl_, t, nullptr, &dst);
        SDL_DestroyTexture(t);
    }
    SDL_DestroySurface(rgba);
    return t != nullptr;
}

const Image* Renderer::source(const AssetId& id) {
    if (auto it = sources_.find(id); it != sources_.end()) return it->second ? &*it->second : nullptr;
    auto& slot = sources_[id];
    slot = assets_.image(id);
    return slot ? &*slot : nullptr;
}

void Renderer::draw_rgba(int x, int y, int width, int height, const std::vector<std::uint8_t>& rgba) {
    SDL_Texture* t = SDL_CreateTexture(sdl_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
    if (!t) return;
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    SDL_UpdateTexture(t, nullptr, rgba.data(), width * 4);
    const SDL_FRect dst{float(x), float(y), float(width), float(height)};
    SDL_RenderTexture(sdl_, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

SDL_Surface* Renderer::read_frame() { return SDL_RenderReadPixels(sdl_, nullptr); }

void Renderer::end_frame(const std::function<void(SDL_Renderer*)>& over) {
    SDL_SetRenderTarget(sdl_, nullptr);
    SDL_SetRenderDrawColor(sdl_, 0, 0, 0, 255);
    SDL_RenderClear(sdl_);
    int width = 0, height = 0;
    SDL_GetCurrentRenderOutputSize(sdl_, &width, &height);
    const SDL_Rect picture = picture_rect(width, height);
    const SDL_FRect dst{float(picture.x), float(picture.y), float(picture.w), float(picture.h)};
    if (frame_ && picture.w > 0 && picture.h > 0) SDL_RenderTexture(sdl_, frame_, nullptr, &dst);
    if (over) over(sdl_);
    SDL_RenderPresent(sdl_);
}

}  // namespace chr::platform
