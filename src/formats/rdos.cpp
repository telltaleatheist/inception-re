// The Apple II disk under RDOS 3.3 (docs/formats/rdos.md).

#include "formats/cmp.h"  // FormatError
#include "formats/disk_image.h"

namespace chr::formats {

namespace {

constexpr std::size_t kSector = 256;
constexpr std::size_t kSectors = 560;                   // 35 tracks of 16
constexpr std::size_t kCatalog = 16 * kSector;          // track 1, sector 0
constexpr std::size_t kCatalogSize = 16 * kSector;      // the whole of track 1
constexpr std::size_t kEntry = 32;
constexpr char kMark[] = "RDOS";

// The disk's sectors as RDOS counts them, from an image in DOS 3.3's order (docs/formats/rdos.md,
// "The sectors' order").
std::vector<std::uint8_t> in_rdos_order(std::span<const std::uint8_t> image) {
    std::vector<std::uint8_t> disk;
    disk.reserve(image.size());
    for (std::size_t s = 0; s < kSectors; ++s) {
        const std::size_t k = s % 16;
        const std::size_t from = s - k + (k == 0 || k == 15 ? k : 15 - k);
        const auto sector = image.subspan(from * kSector, kSector);
        disk.insert(disk.end(), sector.begin(), sector.end());
    }
    return disk;
}

}  // namespace

std::vector<ImageFile> read_rdos(std::span<const std::uint8_t> image) {
    if (disk_kind_by_size(image.size()) != DiskKind::Rdos) throw FormatError("RDOS: not a 140K image");
    const std::vector<std::uint8_t> ordered = in_rdos_order(image);
    const std::span<const std::uint8_t> disk = ordered;
    const auto catalog = disk.subspan(kCatalog, kCatalogSize);
    for (std::size_t i = 0; i < 4; ++i)
        if ((catalog[i] & 0x7f) != std::uint8_t(kMark[i])) throw FormatError("RDOS: no RDOS catalog on track 1");
    std::vector<ImageFile> out;
    // The first 32 bytes are the catalog's own header; each entry after it names one file.
    for (std::size_t at = kEntry; at + kEntry <= kCatalogSize; at += kEntry) {
        const auto e = catalog.subspan(at, kEntry);
        if (e[0] < 0xa0) continue;  // unused (names are high-bit ASCII)
        std::string name;
        for (std::size_t i = 0; i < 24; ++i) name += char(e[i] & 0x7f);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        for (char& c : name)
            if (c == '/' || static_cast<unsigned char>(c) < 0x20) c = '_';
        const std::size_t sectors = e[25];
        const std::size_t length = std::size_t(e[28]) | std::size_t(e[29]) << 8;
        const std::size_t start = std::size_t(e[30]) | std::size_t(e[31]) << 8;
        if (length > sectors * kSector || start + sectors > kSectors)
            throw FormatError("RDOS: a catalog entry past the disk's end");
        const auto data = disk.subspan(start * kSector, length);
        out.push_back({name, {data.begin(), data.end()}});
    }
    return out;
}

}  // namespace chr::formats
