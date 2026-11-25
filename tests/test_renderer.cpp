// The picture at any window size (docs/window.md): drawn with SDL's software
// renderer on a surface, so no window and no video driver.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "platform/renderer.h"
#include "test.h"

using namespace chr;
using namespace chr::platform;

namespace {

constexpr int kTile = 16;
constexpr int kColumns = kScreenWidth / kTile, kRows = (kScreenHeight + kTile - 1) / kTile;
const AssetId kFine2{"FINE2", 0}, kFine4{"FINE4", 0}, kGlyph{"GLYPH", 0}, kFigure{"FIGURE", 0}, kNew{"NEW", 0};

// Colours by number, every one different, none of them a tile's.
formats::Palette16 made_up_palette() {
    formats::Palette16 p{};
    for (int i = 0; i < 16; ++i)
        p[std::size_t(i)] = {std::uint8_t(255 - i), std::uint8_t(210 + i), std::uint8_t(128 + 7 * i)};
    return p;
}

// A colour for each point of a picture, all different.
void put(Image& image, int x, int y, std::uint32_t n, std::uint8_t alpha = 255) {
    auto* p = &image.rgba[std::size_t(4 * (y * image.width + x))];
    p[0] = std::uint8_t(n), p[1] = std::uint8_t(n >> 8), p[2] = std::uint8_t(n >> 16), p[3] = alpha;
}

Image blank(int width, int height, int logical_width, int logical_height) {
    return {width, height, logical_width, logical_height, std::vector<std::uint8_t>(std::size_t(width * height * 4)), {}};
}

struct MadeUp : AssetProvider {
    std::optional<Image> image(const AssetId& id) override {
        if (id.file == "TILE") {  // tile n of the screen cut in 16x16s
            Image t = blank(kTile, kTile, kTile, kTile);
            const int left = id.index % kColumns * kTile, top = id.index / kColumns * kTile;
            for (int y = 0; y < kTile; ++y)
                for (int x = 0; x < kTile; ++x) put(t, x, y, std::uint32_t((top + y) * kScreenWidth + left + x));
            return t;
        }
        if (id.file == "FINE2" || id.file == "FINE4") {  // a whole screen, two and four times as fine
            const int f = id.file == "FINE2" ? 2 : 4;
            Image t = blank(kScreenWidth * f, kScreenHeight * f, kScreenWidth, kScreenHeight);
            for (int y = 0; y < t.height; ++y)
                for (int x = 0; x < t.width; ++x) put(t, x, y, std::uint32_t(y * t.width + x));
            return t;
        }
        if (id.file == "GLYPH") {  // white with see-through points
            Image t = blank(8, 8, 8, 8);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) put(t, x, y, 0xffffff, (x ^ y) & 1 ? 255 : 0);
            return t;
        }
        if (id.file == "FIGURE") {
            Image t = blank(16, 16, 16, 16);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) put(t, x, y, std::uint32_t(0xa00000 + 16 * y + x), x < y ? 0 : 255);
            return t;
        }
        if (id.file == "NEW") {  // in the palette's colours, twice as fine, with no numbers
            const auto palette = made_up_palette();
            Image t = blank(32, 32, 16, 16);
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) {
                    const auto& c = palette[std::size_t((x + 3 * y) & 15)];
                    put(t, x, y, std::uint32_t(c.r | c.g << 8 | c.b << 16));
                }
            return t;
        }
        return std::nullopt;
    }
};

// What a surface shows, as 0xRRGGBB.
std::vector<std::uint32_t> colours(SDL_Surface* s) {
    std::vector<std::uint32_t> out(std::size_t(s->w * s->h));
    SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_XRGB8888);
    for (int y = 0; y < s->h; ++y)
        for (int x = 0; x < s->w; ++x)
            out[std::size_t(y * s->w + x)] = 0xffffff & reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(c->pixels) + y * c->pitch)[x];
    SDL_DestroySurface(c);
    return out;
}

struct Drawn {
    std::vector<std::uint32_t> output, frame;  // the window's pixels; the frame's
    int frame_width = 0, frame_height = 0;
    bool ok = true;
};

template <class Scene>
Drawn draw_at(int width, int height, int fixed, Scene scene) {
    Drawn d;
    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_XRGB8888);
    SDL_Renderer* sdl = SDL_CreateSoftwareRenderer(surface);
    {
        MadeUp assets;
        Renderer renderer(sdl, assets, fixed);
        d.ok = renderer.begin_frame();
        d.ok = d.ok && scene(renderer);
        if (SDL_Surface* frame = renderer.read_frame()) {
            d.frame = colours(frame);
            d.frame_width = frame->w, d.frame_height = frame->h;
            SDL_DestroySurface(frame);
        }
        renderer.end_frame();
        d.output = colours(surface);
    }
    SDL_DestroyRenderer(sdl);
    SDL_DestroySurface(surface);
    return d;
}

// Everything the layers do: tiles edge to edge, a clip, see-through art, a
// figure partly shown, bars and lines a unit thick, text cells, a picture
// made on the spot, and the XOR box over new art and over what isn't in the
// palette.
bool everything(Renderer& r) {
    const auto palette = made_up_palette();
    bool ok = true;
    for (int n = 0; n < kColumns * kRows; ++n)
        ok = r.draw({"TILE", std::uint16_t(n)}, n % kColumns * kTile, n / kColumns * kTile) && ok;
    r.clip(104, 3, 211, 90);
    ok = r.draw({"TILE", 5}, 97, -4) && ok;
    ok = r.draw({"TILE", 6}, 306, 85) && ok;
    r.clear_clip();
    ok = r.draw(kFigure, 121, 33) && ok;
    ok = r.draw_top(kFigure, 141, 33, 9) && ok;
    for (int i = 0; i < 40; ++i) r.fill(150 + i, 40 + i / 3, 1, 1, 9, 8, 7);
    r.fill(128, 0, 1, 193, 3, 2, 1);
    r.fill(3, 120, 1, 15, 200, 100, 50);
    r.fill(0, 199, 320, 1, 60, 61, 62);
    r.fill(319, 0, 1, 200, 63, 64, 65);
    for (int i = 0; i < 12; ++i) {
        r.fill(8 * i, 152, 8, 8, std::uint8_t(i), 90, 91);
        ok = r.draw_tinted(kGlyph, 8 * i, 152, 250, std::uint8_t(200 + i), 4) && ok;
    }
    std::vector<std::uint8_t> rgba;
    for (int i = 0; i < 64; ++i) rgba.insert(rgba.end(), {std::uint8_t(i), 70, 71, 255});
    r.draw_rgba(201, 111, 8, 8, rgba);
    ok = r.draw(kNew, 211, 57) && ok;
    for (int i = 0; i < 16; ++i) {
        const auto& c = palette[std::size_t(i)];
        r.fill(230 + 2 * i, 60, 2, 9, c.r, c.g, c.b);
    }
    return r.xor_colours(208, 56, 64, 16, 14, palette) && ok;
}

struct Size {
    int width, height;
};
// The sizes asked about (a window on an ordinary display and one twice as
// dense), some in between, and shapes that aren't 16:10.
const std::vector<Size> kSizes = {{320, 200},   {960, 600},   {1000, 625},  {1884, 1098}, {1884, 1178}, {1920, 1200},
                                  {2000, 1250}, {3768, 2196}, {1920, 1080}, {1366, 768},  {1024, 768},  {700, 1000},
                                  {961, 601},   {1883, 1177}, {2560, 1440}, {333, 208},   {641, 401},   {3840, 2400}};

// The source pixel whose part of the output holds the middle of output pixel
// `at`, when `from` pixels are scaled to `to`; and how far, in output
// pixels, that middle is from the nearest edge between two of them.
int right_pixel(int at, int from, int to) { return int((2LL * at + 1) * from / (2LL * to)); }
double to_edge(int at, int from, int to) {
    const double in_source = (at + 0.5) * from / to;
    return std::abs(in_source - std::round(in_source)) * to / from;
}
// How close to an edge a scaler may round the other way.
constexpr double kOnTheEdge = 0.05;

// Whether the output's picture is `source` scaled, every pixel from the
// right one, with black round it. Returns the pixels that aren't.
int wrong_pixels(const std::vector<std::uint32_t>& output, Size size, const std::vector<std::uint32_t>& source, int from_w,
                 int from_h) {
    const SDL_Rect p = picture_rect(size.width, size.height);
    int wrong = 0;
    const auto choices = [](int at, int from, int to) {
        const int n = right_pixel(at, from, to);
        std::vector<int> v{n};
        if (to_edge(at, from, to) < kOnTheEdge)
            for (const int other : {n - 1, n + 1})
                if (other >= 0 && other < from) v.push_back(other);
        return v;
    };
    std::vector<std::vector<int>> columns, rows;
    for (int x = 0; x < p.w; ++x) columns.push_back(choices(x, from_w, p.w));
    for (int y = 0; y < p.h; ++y) rows.push_back(choices(y, from_h, p.h));
    for (int y = 0; y < size.height; ++y)
        for (int x = 0; x < size.width; ++x) {
            const auto shown = output[std::size_t(y * size.width + x)];
            bool right = false;
            if (x < p.x || y < p.y || x >= p.x + p.w || y >= p.y + p.h) {
                right = shown == 0;
            } else {
                for (const int sy : rows[std::size_t(y - p.y)])
                    for (const int sx : columns[std::size_t(x - p.x)]) right = right || source[std::size_t(sy * from_w + sx)] == shown;
            }
            wrong += !right;
        }
    return wrong;
}

std::string named(const char* what, Size s, int wrong) {
    return std::string(what) + " at " + std::to_string(s.width) + "x" + std::to_string(s.height) + ": " +
           std::to_string(wrong) + " pixels wrong";
}

}  // namespace

// As large as fits at 16:10, in the middle, with bars at no more than two sides.
TEST(renderer_picture_rect) {
    const auto is = [](Size s, int x, int y, int w, int h) {
        const auto r = picture_rect(s.width, s.height);
        return r.x == x && r.y == y && r.w == w && r.h == h;
    };
    CHECK(is({960, 600}, 0, 0, 960, 600));
    CHECK(is({1000, 625}, 0, 0, 1000, 625));
    CHECK(is({1884, 1098}, 63, 0, 1757, 1098));
    CHECK(is({3768, 2196}, 127, 0, 3514, 2196));
    CHECK(is({1920, 1080}, 96, 0, 1728, 1080));
    CHECK(is({1024, 768}, 0, 64, 1024, 640));
    CHECK(is({700, 1000}, 0, 281, 700, 438));
    // A window held to the shape, rounded to whole points: no bars at all,
    // whatever the display's density.
    int barred = 0;
    for (int w = 320; w <= 4000; ++w)
        for (int density = 1; density <= 3; ++density)
            for (const int h : {int(std::floor(w / 1.6)), int(std::lround(w / 1.6)), int(std::ceil(w / 1.6))})
                if (std::abs(h - w / 1.6) <= 0.5 && !is({w * density, h * density}, 0, 0, w * density, h * density)) ++barred;
    CHECK(barred == 0);
    for (const auto& s : kSizes) {
        const auto r = picture_rect(s.width, s.height);
        CHECK(r.w == s.width || r.h == s.height);
        CHECK(std::abs(r.w - 1.6 * r.h) <= 4 && r.w <= s.width && r.h <= s.height);
    }
}

// Never enlarged by the last scaling, and never past the largest texture.
TEST(renderer_frame_scale) {
    for (const auto& s : kSizes) {
        const auto r = picture_rect(s.width, s.height);
        const int k = frame_scale(r, 0);
        CHECK(k * kScreenWidth >= r.w && k * kScreenHeight >= r.h);
        CHECK(k == 1 || (k - 1) * kScreenWidth < r.w || (k - 1) * kScreenHeight < r.h);
    }
    CHECK(frame_scale(picture_rect(960, 600), 0) == 3);
    CHECK(frame_scale(picture_rect(3768, 2196), 2048) == 6);
    CHECK(frame_scale(picture_rect(100, 60), 0) == 1);
    CHECK(frame_scale(SDL_Rect{}, 0) == 1);
}

// Every pixel of the window from the right pixel of the 320x200 frame, the
// XOR box's too, with no seam: so no pixel of anything that isn't there. The
// box's new art is finer than the 320x200 frame and is compared below.
TEST(renderer_every_pixel_from_the_right_one) {
    const auto original = [](Renderer& r) {
        return everything(r) && r.draw({"TILE", 7}, 211, 57);
    };
    const auto one = draw_at(320, 200, 1, original);
    CHECK(one.ok && one.frame_width == 320 && one.frame_height == 200);
    CHECK(one.output == one.frame);
    for (const auto& s : kSizes) {
        const auto d = draw_at(s.width, s.height, 0, original);
        CHECK(d.ok);
        if (const int wrong = wrong_pixels(d.output, s, one.frame, 320, 200))
            chrtest::fail(__FILE__, __LINE__, named("the 1x frame scaled", s, wrong));
        // And every one of the 64000 is there, where none is scaled away.
        const auto r = picture_rect(s.width, s.height);
        std::set<int> xs, ys;
        for (int x = 0; x < r.w; ++x) xs.insert(right_pixel(x, 320, r.w));
        for (int y = 0; y < r.h; ++y) ys.insert(right_pixel(y, 200, r.h));
        CHECK(xs.size() == 320 && ys.size() == 200);
    }
}

// The XOR box does what it did at 1x: each colour to the one whose number is
// XORed, a colour not in the palette left alone.
TEST(renderer_xor_box) {
    const auto palette = made_up_palette();
    const auto scene = [&](Renderer& r) {
        for (int i = 0; i < 16; ++i) {
            const auto& c = palette[std::size_t(i)];
            r.fill(5 * i, 7, 5, 11, c.r, c.g, c.b);
        }
        r.fill(80, 7, 9, 11, 1, 2, 3);
        return r.xor_colours(0, 7, 89, 11, 9, palette) && r.xor_colours(-8, 190, 16, 20, 9, palette) &&
               r.xor_colours(312, -8, 24, 16, 9, palette) && r.xor_colours(400, 0, 8, 8, 9, palette);
    };
    const auto one = draw_at(320, 200, 1, scene);
    CHECK(one.ok);
    for (int x = 0; x < 89; ++x) {
        formats::Rgb want{1, 2, 3};
        if (x < 80) want = palette[std::size_t((x / 5) ^ 9)];
        for (int y = 7; y < 18; ++y)
            CHECK(one.frame[std::size_t(y * 320 + x)] == std::uint32_t(want.r << 16 | want.g << 8 | want.b));
    }
    for (const auto& s : kSizes) {
        const auto d = draw_at(s.width, s.height, 0, scene);
        CHECK(d.ok);
        if (const int wrong = wrong_pixels(d.output, s, one.frame, 320, 200))
            chrtest::fail(__FILE__, __LINE__, named("the XOR box", s, wrong));
    }
}

// New art keeps its own resolution: every point of a screen two and four
// times as fine shows where the window has the pixels for it, and the XOR
// box doesn't coarsen it.
TEST(renderer_fine_art) {
    for (const int f : {2, 4}) {
        const AssetId id = f == 2 ? kFine2 : kFine4;
        const auto scene = [&](Renderer& r) { return r.draw(id, 0, 0); };
        const auto whole = draw_at(320 * f, 200 * f, 0, scene);
        CHECK(whole.ok);
        CHECK(std::set<std::uint32_t>(whole.output.begin(), whole.output.end()).size() == std::size_t(64000 * f * f));
        // Scales where the frame's is a multiple of the art's.
        for (const Size s : {Size{1884, 1098}, Size{1920, 1200}, Size{3768, 2196}, Size{3840, 2400}}) {
            if (frame_scale(picture_rect(s.width, s.height), 0) % f != 0) continue;
            const auto d = draw_at(s.width, s.height, 0, scene);
            if (const int wrong = wrong_pixels(d.output, s, whole.output, 320 * f, 200 * f))
                chrtest::fail(__FILE__, __LINE__, named("fine art", s, wrong));
        }
    }
    const auto palette = made_up_palette();
    const auto d = draw_at(640, 400, 0, [&](Renderer& r) { return r.draw(kNew, 8, 8) && r.xor_colours(8, 8, 16, 16, 5, palette); });
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            const auto& c = palette[std::size_t(((x + 3 * y) & 15) ^ 5)];
            CHECK(d.output[std::size_t((16 + y) * 640 + 16 + x)] == std::uint32_t(c.r << 16 | c.g << 8 | c.b));
        }
}

// A screenshot's frame is 960x600 and the same whatever the window is, and
// is what a 960x600 window shows.
TEST(renderer_screenshot_size) {
    const auto window = draw_at(960, 600, 0, everything);
    for (const Size s : {Size{960, 600}, Size{1920, 1200}, Size{1884, 1098}, Size{100, 700}}) {
        const auto d = draw_at(s.width, s.height, 3, everything);
        CHECK(d.ok && d.frame_width == 960 && d.frame_height == 600);
        CHECK(d.frame == window.output);
    }
}

// A frame that can't be drawn (here the renderer refuses a texture of the
// frame's size, as a lost device refuses any) is skipped, and drawing goes
// on as before once it can be.
TEST(renderer_frame_refused_and_back) {
    SDL_Surface* surface = SDL_CreateSurface(1000, 625, SDL_PIXELFORMAT_XRGB8888);
    SDL_Renderer* sdl = SDL_CreateSoftwareRenderer(surface);
    const auto largest = [&](int side) {
        SDL_SetNumberProperty(SDL_GetRendererProperties(sdl), SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, side);
    };
    {
        MadeUp assets;
        Renderer renderer(sdl, assets);
        const auto scene = [&] { return everything(renderer) && renderer.draw({"TILE", 7}, 211, 57); };
        // Refused from the start.
        largest(100);
        for (int i = 0; i < 3; ++i) CHECK(!renderer.begin_frame() && *SDL_GetError() != 0);
        largest(0);
        CHECK(renderer.begin_frame() && scene());
        renderer.end_frame();
        const auto before = colours(surface);
        CHECK(before == draw_at(1000, 625, 0, [](Renderer& r) { return everything(r) && r.draw({"TILE", 7}, 211, 57); }).output);
        // Refused while running, then there again; and nothing is drawn on
        // the window meanwhile.
        largest(100);
        for (int i = 0; i < 3; ++i) CHECK(!renderer.begin_frame());
        CHECK(colours(surface) == before);
        largest(0);
        for (int i = 0; i < 2; ++i) {
            CHECK(renderer.begin_frame() && scene());
            renderer.end_frame();
            CHECK(colours(surface) == before);
        }
    }
    SDL_DestroyRenderer(sdl);
    SDL_DestroySurface(surface);
}

// A window with no pixels is not an error: the frame is drawn, and shown on
// nothing.
TEST(renderer_no_output) {
    const auto none = [](int w, int h) {
        const auto r = picture_rect(w, h);
        return r.w == 0 || r.h == 0;
    };
    CHECK(none(0, 0) && none(0, 600) && none(960, 0));
    // The software renderer's output is its surface's size as it is made.
    SDL_Surface* surface = SDL_CreateSurface(640, 400, SDL_PIXELFORMAT_XRGB8888);
    surface->w = surface->h = 0;
    SDL_Renderer* sdl = SDL_CreateSoftwareRenderer(surface);
    int w = -1, h = -1;
    CHECK(sdl && SDL_GetCurrentRenderOutputSize(sdl, &w, &h) && w == 0 && h == 0);
    if (sdl) {
        MadeUp assets;
        Renderer renderer(sdl, assets);
        for (int i = 0; i < 3; ++i) {
            CHECK(renderer.begin_frame() && everything(renderer));
            SDL_Surface* frame = renderer.read_frame();
            CHECK(frame && frame->w == kScreenWidth && frame->h == kScreenHeight);
            SDL_DestroySurface(frame);
            renderer.end_frame();
        }
    }
    SDL_DestroyRenderer(sdl);
    surface->w = 640, surface->h = 400;
    SDL_DestroySurface(surface);
}
