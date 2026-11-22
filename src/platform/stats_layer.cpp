#include "platform/stats_layer.h"

#include <array>

#include "game/asset_id.h"

namespace chr::platform {

namespace {

constexpr int kWhole = 2, kLost = 4, kFlash = 14;  // EGA colour numbers (CGA: 1, 2 and 3)
constexpr int kPulsing = 4;                        // the colour DG:1378 steps
constexpr int kBarWidth = 6;
constexpr int kHeatBarX = 0x100, kHeatBarBottom = 0xb7;  // 15ab:225d
// 15ab:1858's calls: the pips' first cell (column, row).
constexpr int kEngineCol = 0x25, kEngineRow = 0;
constexpr int kGyroCol = 0x25, kGyroRow = 1;
constexpr int kSensorsCol = 0x25, kSensorsRow = 2;
constexpr int kSinksCol = 0x22, kSinksRow = 6;
constexpr int kPipsPerRow = 5;

}  // namespace

formats::Palette16 stats_colours(const game::StatsScene& scene, const formats::Tables& tables,
                                 const formats::Palette16& standard) {
    if (!scene.pulse) return standard;
    std::array<int, 16> numbers = tables.stats_palette;
    if (*scene.pulse >= 0) numbers[kPulsing] = tables.stats_pulse[std::size_t(*scene.pulse & 3)];
    return formats::palette_ega(numbers);
}

bool draw_stats(Renderer& renderer, const game::StatsScene& scene, const formats::Tables& tables,
                const formats::Palette16& standard, const formats::Palette16& colours) {
    // The picture, coloured again by its colour numbers when the screen's
    // colours differ from those it was decoded with.
    bool drawn = false;
    const Image* picture = renderer.source(assets::kStatsPicture);
    if (picture && colours != standard && !picture->numbers.empty() &&
        picture->numbers.size() == std::size_t(picture->width * picture->height)) {
        std::vector<std::uint8_t> rgba = picture->rgba;
        for (std::size_t i = 0; i < picture->numbers.size(); ++i) {
            const int n = picture->numbers[i];
            if (n > 15) continue;
            const auto& c = colours[std::size_t(n)];
            rgba[4 * i] = c.r, rgba[4 * i + 1] = c.g, rgba[4 * i + 2] = c.b;
        }
        renderer.draw_rgba(0, 0, picture->width, picture->height, rgba);
        drawn = true;
    } else {
        drawn = renderer.draw(assets::kStatsPicture, 0, 0);
    }
    auto box = [&](int x1, int y1, int x2, int y2, int colour) {  // inclusive corners
        if (x2 < x1) std::swap(x1, x2);
        if (y2 < y1) std::swap(y1, y2);
        const auto& c = colours[std::size_t(colour)];
        renderer.fill(x1, y1, x2 - x1 + 1, y2 - y1 + 1, c.r, c.g, c.b);
    };
    // 15ab:174c: from the bottom row up, the lower part, then the upper, a
    // pixel a point.
    auto bar = [&](int x, int y, int upper, int lower, int lower_colour) {
        if (lower != 0) {
            const int top = y - lower + 1;
            box(x, top, x + kBarWidth - 1, y, lower_colour);
            y = top - 1;
        }
        if (upper != 0) box(x, y - upper + 1, x + kBarWidth - 1, y, kWhole);
    };
    for (int i = 0; i < game::StatsScene::kBars; ++i) {
        const auto& b = scene.bars[std::size_t(i)];
        bar(tables.stats_bar_x[std::size_t(i)], tables.stats_bar_y[std::size_t(i)], b.armour, b.structure, kLost);
    }
    bar(kHeatBarX, kHeatBarBottom, game::StatsScene::kHeatMax - scene.heat, scene.heat,
        scene.heat_flash ? kFlash : kLost);
    // 15ab:1858: a pip a column, five a row; the rest lost from the `whole`-th.
    auto pips = [&](int col, int row, const game::StatsScene::Pips& p) {
        int colour = kWhole;
        for (int i = 0; i < p.count; ++i) {
            if (i == kPipsPerRow) {
                col -= kPipsPerRow;
                ++row;
            }
            if (i == p.whole) colour = kLost;
            box(col * 8 + 2, row * 8 + 3, col * 8 + 4, row * 8 + 5, colour);
            ++col;
        }
    };
    pips(kEngineCol, kEngineRow, scene.engine);
    pips(kGyroCol, kGyroRow, scene.gyro);
    pips(kSensorsCol, kSensorsRow, scene.sensors);
    pips(kSinksCol, kSinksRow, scene.heat_sinks);
    return drawn;
}

}  // namespace chr::platform
