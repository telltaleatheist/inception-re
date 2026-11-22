#include "platform/text_layer.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "game/fight.h"

namespace chr::platform {

namespace {

using game::TextRole;

// The original look: the colour number (EGA) the program uses for what each
// role names. The values in brackets are what it uses instead when DG:4fba,
// the display, is 0 (CGA); the rebuild draws the EGA display.
constexpr std::array<std::uint8_t, game::kTextRoleCount> kOriginalRoleColours = [] {
    std::array<std::uint8_t, game::kTextRoleCount> t{};
    auto set = [&t](TextRole r, int n) { t[std::size_t(r)] = std::uint8_t(n); };
    set(TextRole::Text, 15);         // F:37fe = 15
    set(TextRole::Background, 0);    // 273d:00d5 and 273d:01fb calls with colour 0
    set(TextRole::Heading, 10);      // 1000:28a2 (1)
    set(TextRole::Label, 10);        // 1000:28a2 (1)
    set(TextRole::Amount, 10);       // 1000:28a2 (1); 24d3:0231 course prices
    set(TextRole::Report, 10);       // 1000:28a2 (1)
    set(TextRole::Emphasis, 10);     // 1000:28a2 (1)
    set(TextRole::PartyAction, 15);  // 22e8:000c, F:37fe = 15 for a party shooter
    set(TextRole::EnemyAction, 14);  // 22e8:000c, F:37fe = 14 for an enemy shooter
    set(TextRole::Targeted, 14);     // 1d43:0004, a weapon with a target
    set(TextRole::Worn, 14);         // 15ab:18e8, armour below full
    set(TextRole::Destroyed, 8);     // 15ab:18e8, 1d43:0004
    set(TextRole::Funds, 2);         // 24d3:096f (1)
    set(TextRole::FileName, 2);      // 1000:32b3, DG:37fe = 2
    set(TextRole::Warning, 4);       // 1000:35d3, DG:37fe = 4 (2)
    set(TextRole::MapPointer, 15);   // 1000:4441, 273d:00d5 with 15
    set(TextRole::Unease, 14);       // 1000:378d, F:37fe = 14
    set(TextRole::StatValue, 14);    // 15ab:1afe, 273d:00d5 with 14; F:37fe = 14
    return t;
}();

}  // namespace

int box_marks_colour(std::uint8_t marks) {
    int colour = 0;
    if (marks & game::mark_bits(game::BoxMark::Unit)) colour ^= 15;    // 203b:1774, colour 15
    if (marks & game::mark_bits(game::BoxMark::Cursor)) colour ^= 14;  // 203b:1c1f, colour 14
    return colour;
}

int colour_number(game::TextColour colour) {
    if (colour.from == game::TextColour::From::Data) return colour.value & 0x0f;
    return colour.value < kOriginalRoleColours.size() ? kOriginalRoleColours[colour.value] : 15;
}

std::optional<CellSize> font_cell_size(Renderer& renderer) {
    CellSize size;
    for (int ch = 0x20; ch < 0x80; ++ch)
        if (const auto s = renderer.logical_size(game::font_glyph(std::uint8_t(ch)))) {
            size.width = std::max(size.width, s->first);
            size.height = std::max(size.height, s->second);
        }
    if (size.width == 0 || size.height == 0) return std::nullopt;
    return size;
}

bool draw_text(Renderer& renderer, const game::TextScreen& text, const formats::Tables& tables,
               const formats::Palette16& palette, const TextLayout& layout) {
    using Kind = game::TextCell::Kind;
    bool ok = true;
    int cw = layout.cell_width, ch = layout.cell_height;
    if (cw == 0 || ch == 0) {
        static thread_local std::optional<CellSize> measured;  // the font doesn't change
        if (!measured) measured = font_cell_size(renderer);
        if (!measured) return false;
        if (cw == 0) cw = measured->width;
        if (ch == 0) ch = measured->height;
    }
    for (int row = 0; row < game::kTextRows; ++row)
        for (int col = 0; col < game::kTextColumns; ++col) {
            const auto& c = text.cell(col, row);
            if (c.kind == Kind::Empty) continue;
            const int x = layout.left + col * cw, y = layout.top + row * ch;
            if (c.kind == Kind::Frame) {
                ok = renderer.draw(c.asset(), x, y) && ok;
                continue;
            }
            int x_or = box_marks_colour(c.box_marks);
            for (const auto bar : c.bars)
                if (bar != 0 && bar - 1 < int(tables.menus.size())) x_or ^= tables.menus[std::size_t(bar - 1)].highlight;
            const auto& bg = palette[std::size_t((colour_number(c.bg) ^ x_or) & 0x0f)];
            renderer.fill(x, y, cw, ch, bg.r, bg.g, bg.b);
            if (c.kind == Kind::Glyph) {
                const auto& fg = palette[std::size_t((colour_number(c.fg) ^ x_or) & 0x0f)];
                ok = renderer.draw_tinted(c.asset(), x, y, fg.r, fg.g, fg.b) && ok;
            }
        }
    return ok;
}

}  // namespace chr::platform
