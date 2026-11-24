#include <map>
#include <memory>

#include "formats/game_source.h"
#include "platform/asset_provider.h"
#include "platform/original_assets.h"
#include "test.h"

using namespace chr;

namespace {

// A provider that answers only the colours it is given.
class SomeColours : public platform::AssetProvider {
public:
    explicit SomeColours(std::map<int, formats::Rgb> colours) : colours_(std::move(colours)) {}
    std::optional<platform::Image> image(const AssetId& id) override {
        if (id.file != "COLOUR") return std::nullopt;
        const auto it = colours_.find(id.index);
        if (it == colours_.end()) return std::nullopt;
        return platform::Image{1, 1, 1, 1, {it->second.r, it->second.g, it->second.b, 0xff}, {}};
    }

private:
    std::map<int, formats::Rgb> colours_;
};

// A source with one tile sheet under any name, and colours of its own for one.
class OneSheet : public formats::GameSource {
public:
    std::string describe() const override { return "a made-up source"; }
    std::string program_name() const override { return "a made-up program"; }
    std::optional<std::vector<std::uint8_t>> script(int) const override { return std::nullopt; }
    std::optional<std::vector<std::uint8_t>> map(int) const override { return std::nullopt; }
    std::optional<formats::AnmPicture> place_picture(int) const override { return std::nullopt; }
    std::optional<formats::CmpPicture> picture(const std::string&) const override { return std::nullopt; }
    std::optional<formats::TileSheet> tile_sheet(const std::string&) const override {
        return formats::TileSheet{std::vector<std::uint8_t>(32000, 0x55)};
    }
    std::optional<formats::Palette16> picture_palette(const std::string& name) const override {
        if (name != "OWN.ICN") return std::nullopt;
        formats::Palette16 own{};
        own[5] = {9, 8, 7};
        return own;
    }
    std::optional<std::vector<std::uint8_t>> demo() const override { return std::nullopt; }
    std::optional<std::vector<std::uint8_t>> title_tune() const override { return std::nullopt; }
    std::unique_ptr<const formats::ProgramData> program() const override { return nullptr; }
};

}  // namespace

TEST(a_sheet_takes_the_copys_own_colours_for_it) {
    formats::Palette16 game{}, map{};
    game[5] = {1, 2, 3};
    map[5] = {4, 5, 6};
    platform::OriginalAssets assets(std::make_shared<OneSheet>(), "", game, {{"MAP.ICN", map}});
    const auto shown = [&](const char* file) {
        const auto image = assets.image({file, 0});
        return image ? formats::Rgb{image->rgba[0], image->rgba[1], image->rgba[2]} : formats::Rgb{};
    };
    CHECK((shown("OWN.ICN") == formats::Rgb{9, 8, 7}));
    CHECK((shown("MAP.ICN") == formats::Rgb{4, 5, 6}));
    CHECK((shown("BTTLTECH.ICN") == formats::Rgb{1, 2, 3}));
}

TEST(screen_colours_take_the_providers_and_keep_the_rest) {
    formats::Palette16 fallback{};
    for (int n = 0; n < 16; ++n) fallback[std::size_t(n)] = {std::uint8_t(n), std::uint8_t(n), std::uint8_t(n)};
    SomeColours none({});
    CHECK(platform::screen_colours(none, fallback) == fallback);
    SomeColours two({{4, {1, 2, 3}}, {15, {250, 240, 230}}});
    const auto got = platform::screen_colours(two, fallback);
    CHECK((got[4] == formats::Rgb{1, 2, 3}));
    CHECK((got[15] == formats::Rgb{250, 240, 230}));
    CHECK(got[0] == fallback[0]);
    CHECK(got[14] == fallback[14]);
    CHECK((assets::colour(0x13) == AssetId{"COLOUR", 3}));
}
