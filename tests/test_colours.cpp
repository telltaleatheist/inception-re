#include <map>

#include "platform/asset_provider.h"
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

}  // namespace

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
