// Asset IDs: how the game logic names a graphic without knowing what it looks
// like. An ID is the original's file name plus an index into that file (a
// picture, a tile or a frame). The renderer turns an ID into pixels through an
// asset provider (src/platform/asset_provider.h). The default provider decodes
// the player's original files; a mod provider may replace any ID with new art.
#pragma once

#include <compare>
#include <cstdint>
#include <string>

namespace chr {

struct AssetId {
    std::string file;     // the original's file name, upper case, e.g. "BTTITLE.CMP"
    std::uint16_t index = 0;  // which image in the file; a .CMP holds one, index 0

    auto operator<=>(const AssetId&) const = default;
};

// "BTTITLE.CMP#0": for logs, error messages and mod manifests.
std::string to_string(const AssetId& id);

// The full-screen pictures the program shows (each a .CMP).
namespace assets {
inline const AssetId kInfocomLogo{"INFOCOM.CMP", 0};
inline const AssetId kTitleScreen{"BTTITLE.CMP", 0};
// The training center's quiz (1c67:08a8): ten 'Mech parts.
inline const AssetId kQuizPicture{"BTSTATS.CMP", 0};
// The 'Mech stats screen (15ab:1afe) shows the same picture.
inline const AssetId kStatsPicture{"BTSTATS.CMP", 0};
// The cache's end: special 45 (24d3:15dd).
inline const AssetId kEndMechPicture{"ENDMECH.CMP", 0};
// Screen colour n (0-15), as a 1x1 picture: what the renderer paints for
// text, panel bars, shot lines, the overhead map and the boxes. The default
// is the original's palette; a mod may give any number another colour.
inline AssetId colour(int n) { return {"COLOUR", std::uint16_t(n & 0x0f)}; }
}  // namespace assets

}  // namespace chr
