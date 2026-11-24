// The Amiga floppy's file system, OFS and FFS (docs/formats/adf.md).

#include <set>

#include "formats/cmp.h"  // FormatError
#include "formats/disk_image.h"

namespace chr::formats {

namespace {

constexpr std::size_t kBlock = 512;
constexpr std::size_t kHashSize = 72;      // a 512-byte block's hash table
constexpr std::int32_t kTypeHeader = 2;    // a header block (root, folder, file)
constexpr std::int32_t kTypeList = 16;     // an FFS file's extension block
constexpr std::int32_t kSecRoot = 1;
constexpr std::int32_t kSecDir = 2;
constexpr std::int32_t kSecFile = -3;
constexpr int kMaxDepth = 16;
constexpr std::size_t kMaxFiles = 4096;

struct Disk {
    std::span<const std::uint8_t> bytes;
    std::size_t blocks = 0;
    bool ffs = false;

    std::span<const std::uint8_t> block(std::uint32_t n) const {
        if (n >= blocks) throw FormatError("ADF: a block number past the disk's end");
        return bytes.subspan(std::size_t(n) * kBlock, kBlock);
    }
    static std::uint32_t u32(std::span<const std::uint8_t> b, std::size_t at) {
        return std::uint32_t(b[at]) << 24 | std::uint32_t(b[at + 1]) << 16 | std::uint32_t(b[at + 2]) << 8 | b[at + 3];
    }
    static std::int32_t s32(std::span<const std::uint8_t> b, std::size_t at) { return std::int32_t(u32(b, at)); }
    static std::string name(std::span<const std::uint8_t> b) {
        const std::size_t n = std::min<std::size_t>(b[kBlock - 80], 30);
        std::string s(reinterpret_cast<const char*>(b.data() + kBlock - 79), n);
        for (char& c : s)
            if (c == '/' || static_cast<unsigned char>(c) < 0x20) c = '_';
        return s;
    }

    std::vector<std::uint8_t> file(std::uint32_t header) const {
        const auto h = block(header);
        const std::uint32_t size = u32(h, kBlock - 188);
        if (size > bytes.size()) throw FormatError("ADF: a file larger than the disk");
        std::vector<std::uint8_t> out;
        out.reserve(size);
        std::set<std::uint32_t> seen;
        if (!ffs) {
            // OFS: each data block has a 24-byte header: its size at 12, the next block at 16.
            for (std::uint32_t k = u32(h, 16); k != 0 && out.size() < size;) {
                if (!seen.insert(k).second) throw FormatError("ADF: a data chain loops");
                const auto d = block(k);
                const std::uint32_t n = u32(d, 12);
                if (n > kBlock - 24) throw FormatError("ADF: a data block's size is too large");
                out.insert(out.end(), d.begin() + 24, d.begin() + 24 + n);
                k = u32(d, 16);
            }
        } else {
            // FFS: the header (then each extension block) lists its data blocks from the end of its table.
            for (std::uint32_t list = header; list != 0 && out.size() < size;) {
                if (!seen.insert(list).second) throw FormatError("ADF: an extension chain loops");
                const auto l = block(list);
                if (list != header && s32(l, 0) != kTypeList) throw FormatError("ADF: not an extension block");
                for (std::size_t i = kHashSize; i-- > 0 && out.size() < size;) {
                    const std::uint32_t k = u32(l, 24 + 4 * i);
                    if (k == 0) continue;
                    const auto d = block(k);
                    out.insert(out.end(), d.begin(), d.end());
                }
                list = u32(l, kBlock - 8);
            }
        }
        if (out.size() < size) throw FormatError("ADF: a file's data ends early");
        out.resize(size);
        return out;
    }

    void walk(std::uint32_t dir, const std::string& prefix, int depth, std::set<std::uint32_t>& seen,
              std::vector<ImageFile>& out) const {
        if (depth > kMaxDepth) throw FormatError("ADF: folders nested too deep");
        const auto d = block(dir);
        for (std::size_t i = 0; i < kHashSize; ++i) {
            for (std::uint32_t k = u32(d, 24 + 4 * i); k != 0;) {
                if (!seen.insert(k).second) throw FormatError("ADF: a folder's chain loops");
                const auto e = block(k);
                if (s32(e, 0) != kTypeHeader) throw FormatError("ADF: a folder entry that is not a header");
                const std::string path = prefix + name(e);
                const std::int32_t sec = s32(e, kBlock - 4);
                if (sec == kSecDir) {
                    walk(k, path + "/", depth + 1, seen, out);
                } else if (sec == kSecFile) {
                    if (out.size() >= kMaxFiles) throw FormatError("ADF: too many files");
                    out.push_back({path, file(k)});
                }
                k = u32(e, kBlock - 16);  // the next entry with the same hash
            }
        }
    }
};

Disk open(std::span<const std::uint8_t> image) {
    if (disk_kind_by_size(image.size()) != DiskKind::Adf) throw FormatError("ADF: not an 880K or 1760K image");
    if (image[0] != 'D' || image[1] != 'O' || image[2] != 'S' || image[3] > 7)
        throw FormatError("ADF: not an Amiga DOS disk");
    Disk disk{image, image.size() / kBlock, (image[3] & 1) != 0};
    const auto root = disk.block(std::uint32_t(disk.blocks / 2));
    if (Disk::s32(root, 0) != kTypeHeader || Disk::s32(root, kBlock - 4) != kSecRoot)
        throw FormatError("ADF: no root block in the middle of the disk");
    return disk;
}

}  // namespace

DiskKind disk_kind_by_size(std::uint64_t size) {
    switch (size) {
        case 901120:
        case 1802240:
            return DiskKind::Adf;
        case 174848:
        case 175531:
            return DiskKind::D64;
        case 143360:
            return DiskKind::Rdos;
        default:
            return DiskKind::None;
    }
}

std::vector<ImageFile> read_adf(std::span<const std::uint8_t> image) {
    const Disk disk = open(image);
    std::vector<ImageFile> out;
    std::set<std::uint32_t> seen;
    disk.walk(std::uint32_t(disk.blocks / 2), "", 0, seen, out);
    return out;
}

std::string adf_volume(std::span<const std::uint8_t> image) {
    const Disk disk = open(image);
    return Disk::name(disk.block(std::uint32_t(disk.blocks / 2)));
}

std::vector<ImageFile> read_disk_image(std::span<const std::uint8_t> image) {
    switch (disk_kind_by_size(image.size())) {
        case DiskKind::Adf:
            return read_adf(image);
        case DiskKind::D64:
            return read_d64(image);
        case DiskKind::Rdos:
            return read_rdos(image);
        case DiskKind::None:
            break;
    }
    return {};
}

}  // namespace chr::formats
