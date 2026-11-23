// The asset-provider seam (for a visual makeover as a mod). The renderer asks a provider
// for the picture behind an asset ID and never reads the original's formats
// itself. The default provider (original_assets.h) decodes the player's own
// files; a mod provider can answer any ID with new art at any
// resolution. No SDL types here, so a provider can be written without them.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "formats/palette.h"
#include "game/asset_id.h"

namespace chr::platform {

struct Image {
    int width = 0;   // in the image's own pixels
    int height = 0;
    // The size it covers on the original's 320x200 screen. For the original
    // art this equals width x height; a 4x mod image of the same picture has
    // the same logical size and four times the pixels.
    int logical_width = 0;
    int logical_height = 0;
    std::vector<std::uint8_t> rgba;  // width * height * 4 bytes, row-major, R G B A
    // The original's colour number of each pixel (0xff: see-through), when
    // the picture is the original's own art. Empty for new art. The
    // original sometimes works on colour numbers, not on what they show
    // (two numbers can show the same black): the orders screen's XOR box.
    std::vector<std::uint8_t> numbers;
};

class AssetProvider {
public:
    virtual ~AssetProvider() = default;
    // The picture for `id`, or nothing if this provider doesn't have it.
    virtual std::optional<Image> image(const AssetId& id) = 0;
};

// The 16 screen colours, each from the provider's {"COLOUR", n}, or from
// `fallback` where it has none.
inline formats::Palette16 screen_colours(AssetProvider& provider, const formats::Palette16& fallback) {
    formats::Palette16 out = fallback;
    for (int n = 0; n < 16; ++n) {
        const auto image = provider.image(assets::colour(n));
        if (image && image->rgba.size() >= 3)
            out[std::size_t(n)] = {image->rgba[0], image->rgba[1], image->rgba[2]};
    }
    return out;
}

}  // namespace chr::platform
