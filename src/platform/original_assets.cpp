#include "platform/original_assets.h"

#include <algorithm>
#include <cctype>
#include <exception>

#include "formats/cells.h"
#include "formats/cmp.h"
#include "formats/game_files.h"
#include "formats/icn.h"
#include "formats/import.h"
#include "formats/anm.h"
#include "formats/palette.h"
#include "formats/sprite_sheet.h"

namespace chr::platform {

namespace {

bool has_extension(const std::string& name, const char* ext) {
    const auto dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string e = name.substr(dot);
    for (auto& ch : e) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return e == ext;
}

Image from_colour_numbers(const std::vector<std::uint8_t>& numbers, int width, int height,
                          const formats::Palette16& palette) {
    Image image{width, height, width, height, {}, {}};
    image.rgba.resize(numbers.size() * 4);
    for (std::size_t i = 0; i < numbers.size(); ++i) {
        const auto& c = palette[numbers[i] & 0x0f];
        image.rgba[4 * i + 0] = c.r;
        image.rgba[4 * i + 1] = c.g;
        image.rgba[4 * i + 2] = c.b;
        image.rgba[4 * i + 3] = 0xff;
    }
    image.numbers.resize(numbers.size());
    for (std::size_t i = 0; i < numbers.size(); ++i) image.numbers[i] = numbers[i] & 0x0f;
    return image;
}

// n of the name "O<n>.ANM" (any case), the place picture's number.
std::optional<int> place_picture_number(const std::string& file) {
    if (file.size() < 6 || (file[0] != 'O' && file[0] != 'o')) return std::nullopt;
    int n = 0;
    std::size_t i = 1;
    for (; i < file.size() && std::isdigit(static_cast<unsigned char>(file[i])); ++i) n = n * 10 + (file[i] - '0');
    if (i == 1 || i > 3 || file.size() != i + 4) return std::nullopt;
    return n;
}

}  // namespace

OriginalAssets::OriginalAssets(std::shared_ptr<const formats::GameSource> source, std::filesystem::path data_dir,
                               formats::Palette16 palette,
                               std::map<std::string, formats::Palette16> picture_palettes)
    : source_(std::move(source)), data_dir_(std::move(data_dir)), palette_(palette),
      picture_palettes_(std::move(picture_palettes)) {}

const formats::TileSheet* OriginalAssets::sheet(const std::string& file) {
    if (auto it = sheets_.find(file); it != sheets_.end()) return &it->second;
    auto found = source_->tile_sheet(file);
    if (!found) {
        last_error_ = file + " not found in " + source_->describe();
        return nullptr;
    }
    return &sheets_.emplace(file, std::move(*found)).first->second;
}

const formats::Font* OriginalAssets::font() {
    if (!font_ && font_error_.empty()) {
        try {
            const auto bytes = formats::read_file(formats::imported_font_path(data_dir_));
            font_ = std::make_unique<formats::Font>(
                formats::parse_font_file(std::string(bytes.begin(), bytes.end())));
        } catch (const std::exception& e) {
            font_error_ = e.what();
        }
    }
    if (!font_) last_error_ = font_error_;
    return font_.get();
}

const formats::CmpPicture* OriginalAssets::picture(const std::string& file) {
    auto it = pictures_.find(file);
    if (it == pictures_.end()) {
        auto found = source_->picture(file);
        if (!found) {
            last_error_ = file + " not found in " + source_->describe();
            return nullptr;
        }
        it = pictures_.emplace(file, std::move(*found)).first;
    }
    return &it->second;
}

std::optional<Image> OriginalAssets::image(const AssetId& id) {
    last_error_.clear();
    try {
        if (id.file == "COLOUR") {
            if (id.index >= palette_.size()) {
                last_error_ = to_string(id) + ": colours run 0-15";
                return std::nullopt;
            }
            const auto& c = palette_[id.index];
            return Image{1, 1, 1, 1, {c.r, c.g, c.b, 0xff}, {std::uint8_t(id.index)}};
        }
        if (id.file == "FONT") {
            const auto* f = font();
            if (!f) return std::nullopt;
            if (id.index >= f->glyphs.size()) {
                last_error_ = to_string(id) + ": no such character";
                return std::nullopt;
            }
            const auto& bits = f->glyphs[id.index];
            Image glyph{f->width, f->height, f->width, f->height,
                        std::vector<std::uint8_t>(bits.size() * 4, 0), {}};
            for (std::size_t i = 0; i < bits.size(); ++i)
                if (bits[i]) std::fill_n(glyph.rgba.begin() + std::ptrdiff_t(4 * i), 4, std::uint8_t(0xff));
            return glyph;
        }
        if (const auto colon = id.file.find(":8x8"); colon != std::string::npos) {
            const auto* pic = picture(id.file.substr(0, colon));
            if (!pic) return std::nullopt;
            const auto cell = formats::picture_cell(*pic, id.index);
            return from_colour_numbers({cell.begin(), cell.end()}, 8, 8, palette_);
        }
        if (id.file == "MECHSHAP.CMP:sprite") {
            // A slot of the program's sprite table. Colour 0 is see-through:
            // the EGA sprite drawer (287f:0377) masks with the OR of the four
            // planes.
            const auto rect = formats::mechshap_sprite(id.index);
            if (!rect) {
                last_error_ = to_string(id) + ": the program never fills this slot";
                return std::nullopt;
            }
            const auto* pic = picture("MECHSHAP.CMP");
            if (!pic) return std::nullopt;
            const auto all = pic->pixels();
            std::vector<std::uint8_t> numbers;
            numbers.reserve(std::size_t(rect->width * rect->height));
            for (int y = 0; y < rect->height; ++y)
                for (int x = 0; x < rect->width; ++x)
                    numbers.push_back(all[std::size_t((rect->y + y) * formats::kCmpWidth + rect->x + x)]);
            auto image = from_colour_numbers(numbers, rect->width, rect->height, palette_);
            for (std::size_t i = 0; i < numbers.size(); ++i)
                if ((numbers[i] & 0x0f) == 0) {
                    image.rgba[4 * i + 3] = 0;
                    image.numbers[i] = 0xff;
                }
            return image;
        }
        if (has_extension(id.file, ".CMP")) {
            const auto* found = picture(id.file);
            if (!found) return std::nullopt;
            if (id.index != 0) {
                last_error_ = to_string(id) + ": a .CMP holds one picture";
                return std::nullopt;
            }
            // The standard 256K EGA/VGA palette (formats/palette.h), from the
            // import, unless the picture has its own.
            const auto own = picture_palettes_.find(id.file);
            return from_colour_numbers(found->pixels(), formats::kCmpWidth, formats::kCmpHeight,
                                       own != picture_palettes_.end() ? own->second : palette_);
        }
        if (has_extension(id.file, ".ICN")) {
            const auto* tiles = sheet(id.file);
            if (!tiles) return std::nullopt;
            const auto pixels = tiles->tile(id.index);
            return from_colour_numbers({pixels.begin(), pixels.end()}, formats::kTileSize,
                                       formats::kTileSize, palette_);
        }
        if (has_extension(id.file, ".ANM")) {
            // {"O17.ANM", k}: frame k of a place's picture, as shown.
            // Frame 0xffff is the picture as it ends.
            const auto number = place_picture_number(id.file);
            const auto anm = number ? source_->place_picture(*number) : std::nullopt;
            if (!anm) {
                last_error_ = id.file + " not found in " + source_->describe();
                return std::nullopt;
            }
            const std::size_t frame = id.index == 0xffff ? anm->frames.size() - 1 : id.index;
            if (frame >= anm->frames.size()) {
                last_error_ = to_string(id) + ": the picture has " + std::to_string(anm->frames.size()) + " frames";
                return std::nullopt;
            }
            return from_colour_numbers(anm->frames[frame], formats::kAnmSize, formats::kAnmSize,
                                       palette_);
        }
        last_error_ = to_string(id) + ": no reader for this file type yet";
    } catch (const std::exception& e) {
        last_error_ = to_string(id) + ": " + e.what();
    }
    return std::nullopt;
}

}  // namespace chr::platform
