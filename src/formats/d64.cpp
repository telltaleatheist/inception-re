// The C64's 1541 disk image (docs/formats/d64.md).

#include <set>

#include "formats/cmp.h"  // FormatError
#include "formats/disk_image.h"

namespace chr::formats {

namespace {

constexpr int kTracks = 35;
constexpr int kDirTrack = 18;
constexpr std::size_t kSector = 256;

int sectors_on(int track) {
    return track <= 17 ? 21 : track <= 24 ? 19 : track <= 30 ? 18 : 17;
}

std::size_t offset(int track, int sector) {
    if (track < 1 || track > kTracks || sector < 0 || sector >= sectors_on(track))
        throw FormatError("D64: a track or sector outside the disk");
    std::size_t before = 0;
    for (int t = 1; t < track; ++t) before += std::size_t(sectors_on(t));
    return (before + std::size_t(sector)) * kSector;
}

// A file's bytes: each sector's first two bytes name the next; on the last, track 0 and the
// index of its last byte.
std::vector<std::uint8_t> chain(std::span<const std::uint8_t> image, int track, int sector) {
    std::vector<std::uint8_t> out;
    std::set<std::pair<int, int>> seen;
    while (true) {
        if (!seen.insert({track, sector}).second) throw FormatError("D64: a file's chain loops");
        const auto s = image.subspan(offset(track, sector), kSector);
        if (s[0] == 0) {
            if (s[1] < 1) throw FormatError("D64: a file's last sector is empty");
            out.insert(out.end(), s.begin() + 2, s.begin() + 1 + s[1]);
            return out;
        }
        out.insert(out.end(), s.begin() + 2, s.end());
        track = s[0], sector = s[1];
    }
}

std::string ascii_name(std::span<const std::uint8_t> raw) {
    std::string s;
    for (std::uint8_t c : raw) {
        if (c == 0xa0) break;  // the name's padding
        s += (c >= 0x20 && c < 0x7f && c != '/') ? char(c) : '_';
    }
    return s;
}

}  // namespace

std::vector<ImageFile> read_d64(std::span<const std::uint8_t> image) {
    if (disk_kind_by_size(image.size()) != DiskKind::D64) throw FormatError("D64: not a 35-track image");
    const auto bam = image.subspan(offset(kDirTrack, 0), kSector);
    if (bam[0] != kDirTrack) throw FormatError("D64: no directory on track 18");
    std::vector<ImageFile> out;
    std::set<std::pair<int, int>> seen;
    for (int t = bam[0], s = bam[1]; t != 0;) {
        if (!seen.insert({t, s}).second) throw FormatError("D64: the directory's chain loops");
        const auto dir = image.subspan(offset(t, s), kSector);
        for (std::size_t e = 0; e < 8; ++e) {
            const auto entry = dir.subspan(e * 32, 32);
            const int type = entry[2] & 7;
            if ((entry[2] & 0x80) == 0 || type < 1 || type > 3) continue;  // closed SEQ, PRG or USR files only
            out.push_back({ascii_name(entry.subspan(5, 16)), chain(image, entry[3], entry[4])});
        }
        t = dir[0], s = dir[1];
    }
    return out;
}

}  // namespace chr::formats
