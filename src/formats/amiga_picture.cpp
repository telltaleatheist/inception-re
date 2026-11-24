// The Amiga pictures and tile sheets (docs/formats/amiga.md, "Pictures and tile sheets").

#include "formats/amiga_picture.h"

#include <algorithm>
#include <vector>

namespace chr::formats {

namespace {

constexpr std::size_t kPaletteBytes = 32;
constexpr std::size_t kPlaneBytes = 8000;  // 320 x 200 / 8
constexpr int kPlanes = 4;
constexpr int kMapTiles = 170;
constexpr std::size_t kMapTileBytes = 128;  // 16 rows of 4 planes x 2 bytes

AmigaPalette palette_of(std::span<const std::uint8_t> file) {
    if (file.size() < kPaletteBytes + 2) throw FormatError("Amiga picture: shorter than its palette");
    AmigaPalette p{};
    for (std::size_t i = 0; i < 16; ++i) p[i] = std::uint16_t(file[2 * i] << 8 | file[2 * i + 1]) & 0x0FFF;
    return p;
}

// The bytes the program's loader reads after the palette: a big-endian length, then that many.
std::span<const std::uint8_t> body_of(std::span<const std::uint8_t> file) {
    const std::size_t length = std::size_t(file[kPaletteBytes]) << 8 | file[kPaletteBytes + 1];
    if (length == 0 || kPaletteBytes + 2 + length > file.size()) throw FormatError("Amiga picture: its length runs past the file");
    return file.subspan(kPaletteBytes + 2, length);
}

// The DOS run-length code (formats/cmp.h) with the 0 code's count big-endian, written straight
// out: the planes one after another.
std::vector<std::uint8_t> unpack_planes(std::span<const std::uint8_t> stream) {
    std::vector<std::uint8_t> out;
    out.reserve(kPlaneBytes * kPlanes);
    std::size_t in = 0;
    auto next = [&]() -> std::uint8_t {
        if (in >= stream.size()) throw FormatError("Amiga picture: data ends before the picture is full");
        return stream[in++];
    };
    while (out.size() < kPlaneBytes * kPlanes) {
        const auto code = static_cast<std::int8_t>(next());
        if (code > 0) {
            for (int i = 0; i < code && out.size() < kPlaneBytes * kPlanes; ++i) out.push_back(next());
            continue;
        }
        std::size_t count;
        if (code < 0) {
            count = std::size_t(-int(code));
        } else {
            count = std::size_t(next()) << 8;
            count |= next();
            if (count == 0) count = 0x10000;
        }
        const std::uint8_t value = next();
        out.insert(out.end(), std::min(count, kPlaneBytes * kPlanes - out.size()), value);
    }
    return out;
}

// Pixel x of the four plane bytes `b` (bit 7 is the leftmost pixel; plane 0 is the colour's bit 0).
std::uint8_t colour(const std::array<std::uint8_t, kPlanes>& b, int x) {
    std::uint8_t c = 0;
    for (int pl = 0; pl < kPlanes; ++pl) c |= std::uint8_t(((b[std::size_t(pl)] >> (7 - x)) & 1) << pl);
    return c;
}

// One 16-pixel tile row into DOS's form: 8 bytes, the high nibble the left pixel.
void put_tile_row(const std::array<std::uint8_t, kPlanes>& left, const std::array<std::uint8_t, kPlanes>& right,
                  std::uint8_t* out) {
    for (int x = 0; x < 16; x += 2) {
        const auto& b = x < 8 ? left : right;
        out[x / 2] = std::uint8_t(colour(b, x % 8) << 4 | colour(b, x % 8 + 1));
    }
}

}  // namespace

AmigaPicture decode_amiga_cmp(std::span<const std::uint8_t> file) {
    AmigaPicture r;
    r.palette = palette_of(file);
    const auto body = body_of(file);
    r.picture.order = body[0] == 1 ? CmpOrder::Rows : CmpOrder::Columns;
    const auto planes = unpack_planes(body.subspan(1));
    r.picture.packed.assign(kCmpPackedSize, 0);
    // Within each plane, row order is byte y * 40 + xb; column order, byte xb * 200 + y.
    for (int y = 0; y < kCmpHeight; ++y) {
        for (int xb = 0; xb < kCmpWidth / 8; ++xb) {
            const std::size_t at = r.picture.order == CmpOrder::Rows ? std::size_t(y) * 40 + std::size_t(xb)
                                                                     : std::size_t(xb) * kCmpHeight + std::size_t(y);
            std::array<std::uint8_t, kPlanes> b{};
            for (int pl = 0; pl < kPlanes; ++pl) b[std::size_t(pl)] = planes[std::size_t(pl) * kPlaneBytes + at];
            for (int x = 0; x < 8; x += 2)
                r.picture.packed[std::size_t(y) * kCmpRowBytes + std::size_t(xb) * 4 + std::size_t(x / 2)] =
                    std::uint8_t(colour(b, x) << 4 | colour(b, x + 1));
        }
    }
    return r;
}

AmigaTileSheet decode_amiga_icn(std::span<const std::uint8_t> file) {
    AmigaTileSheet r;
    r.palette = palette_of(file);
    const auto planes = unpack_planes(body_of(file).subspan(1));
    r.sheet.packed.assign(kCmpPackedSize, 0);
    for (int t = 0; t < kTilesPerSheet; ++t) {
        for (int y = 0; y < kTileSize; ++y) {
            std::array<std::uint8_t, kPlanes> left{}, right{};
            for (int pl = 0; pl < kPlanes; ++pl) {
                const std::size_t at = std::size_t(pl) * kPlaneBytes + std::size_t(t) * 32 + std::size_t(y) * 2;
                left[std::size_t(pl)] = planes[at], right[std::size_t(pl)] = planes[at + 1];
            }
            put_tile_row(left, right, &r.sheet.packed[std::size_t(t) * kTileBytes + std::size_t(y) * 8]);
        }
    }
    return r;
}

AmigaTileSheet decode_amiga_map_icn(std::span<const std::uint8_t> file) {
    AmigaTileSheet r;
    r.palette = palette_of(file);
    const auto body = body_of(file);
    if (body.size() != kMapTiles * kMapTileBytes) throw FormatError("Amiga map.icn: not 170 tiles");
    r.sheet.packed.assign(kCmpPackedSize, 0);
    for (int t = 0; t < kMapTiles; ++t) {
        for (int y = 0; y < kTileSize; ++y) {
            std::array<std::uint8_t, kPlanes> left{}, right{};
            for (int pl = 0; pl < kPlanes; ++pl) {
                const std::size_t at = std::size_t(t) * kMapTileBytes + std::size_t(y) * 8 + std::size_t(pl) * 2;
                left[std::size_t(pl)] = body[at], right[std::size_t(pl)] = body[at + 1];
            }
            put_tile_row(left, right,
                         &r.sheet.packed[std::size_t(t + kAmigaMapIcnFirstTile) * kTileBytes + std::size_t(y) * 8]);
        }
    }
    return r;
}

}  // namespace chr::formats
