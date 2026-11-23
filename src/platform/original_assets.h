// The default asset provider: decodes the player's original files.
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "formats/cmp.h"
#include "formats/font_file.h"
#include "formats/icn.h"
#include "formats/palette.h"
#include "platform/asset_provider.h"

namespace chr::platform {

class OriginalAssets final : public AssetProvider {
public:
    // Pictures come from the game folder; the font from the data folder the
    // importer wrote (formats/import.h). `palette` colours the pictures,
    // except a .CMP named in `picture_palettes`, which is coloured with its own.
    OriginalAssets(std::filesystem::path game_dir, std::filesystem::path data_dir, formats::Palette16 palette,
                   std::map<std::string, formats::Palette16> picture_palettes = {});
    // Also answers {"FONT", c}, the font's character c from the imported
    // font file (white on transparent, for the renderer to colour), and
    // {"NAME.CMP:8x8", n}, cell n of that picture cut into 8x8 cells, and
    // {"MECHSHAP.CMP:sprite", n}, slot n of the program's sprite table
    // (formats/sprite_sheet.h), colour 0 see-through, and {"O17.ANM", k},
    // frame k of a place's picture (formats/anm.h), and {"COLOUR", n}, screen
    // colour n from `palette`.
    std::optional<Image> image(const AssetId& id) override;


    // Why the last image() call returned nothing (empty after a success).
    const std::string& last_error() const { return last_error_; }

private:
    const formats::TileSheet* sheet(const std::string& file);
    const formats::CmpPicture* picture(const std::string& file);

    std::filesystem::path game_dir_;
    std::map<std::string, formats::TileSheet> sheets_;  // decoded once per file
    std::map<std::string, formats::CmpPicture> pictures_;
    const formats::Font* font();

    std::filesystem::path data_dir_;
    formats::Palette16 palette_;
    std::map<std::string, formats::Palette16> picture_palettes_;
    std::unique_ptr<formats::Font> font_;
    std::string font_error_;
    std::string last_error_;
};

}  // namespace chr::platform
