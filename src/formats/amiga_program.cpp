// The Amiga program's hunks and spans (docs/formats/amiga.md).

#include "formats/amiga_program.h"

#include <array>
#include <string>

#include "formats/cmp.h"  // FormatError
#include "formats/sha256.h"

namespace chr::formats {

namespace {

constexpr std::uint32_t kHeader = 0x3F3, kCode = 0x3E9, kData = 0x3EA, kBss = 0x3EB, kReloc32 = 0x3EC,
                        kSymbol = 0x3F0, kDebug = 0x3F1, kEnd = 0x3F2;
constexpr std::uint32_t kMaxHunks = 64;

struct Reader {
    std::span<const std::uint8_t> b;
    std::size_t at = 0;
    std::uint32_t u32() {
        if (at + 4 > b.size()) throw FormatError("Amiga program: ends inside a hunk");
        const std::uint32_t v = std::uint32_t(b[at]) << 24 | std::uint32_t(b[at + 1]) << 16 |
                                std::uint32_t(b[at + 2]) << 8 | b[at + 3];
        at += 4;
        return v;
    }
    void skip_longs(std::uint64_t n) {
        if (n > (b.size() - at) / 4) throw FormatError("Amiga program: a hunk runs past the file's end");
        at += std::size_t(n) * 4;
    }
};

// The program's spans. Hunk 0 is the code, hunk 1 the data.
constexpr std::array<AmigaSpan, 9> kSpans{{
    // 0:5fba reads place n's script from builds.bld at the n-th of these 26 offsets (big-endian).
    {"bld.offsets", 1, 0x07d6, 26 * 4},
    // 0:a81e, the text printer: a text byte with its top bit set stands for two letters, the
    // first from this table by bits 3-6, the second from the next by bits 0-6.
    {"text.pair_first", 1, 0x148a, 16},
    {"text.pair_second", 1, 0x149a, 128},
    // 0:21d12, the map loader: where each of the 15 maps starts in `maps`, big-endian longs.
    {"maps.offsets", 1, 0x1b8e, 15 * 4},
    // 0:23f7a, the place picture loader: where each of the 22 starts in `animation.anm`.
    {"anm.offsets", 1, 0x1c40, 22 * 4},
    // 0:265ea plays sound n from snd at the n-th of these offsets, with the n-th period.
    {"snd.offsets", 1, 0x2540, 19 * 4},
    {"snd.periods", 1, 0x2590, 19 * 2},
    // The text windows and the menus, DOS's 385b:0000 and 385b:0092 in the same layout with
    // big-endian words (the last two windows overlap the first menus, as in DOS).
    {"windows", 1, 0x1530, 11 * 16},
    {"menus", 1, 0x15c2, 41 * 16},
}};

}  // namespace

std::vector<AmigaHunk> read_amiga_hunks(std::span<const std::uint8_t> file) {
    Reader r{file};
    if (r.u32() != kHeader) throw FormatError("not an Amiga program");
    while (const std::uint32_t name = r.u32()) r.skip_longs(name);  // resident library names
    const std::uint32_t table = r.u32(), first = r.u32(), last = r.u32();
    if (first > last || last >= table || table > kMaxHunks) throw FormatError("Amiga program: a bad hunk table");
    std::vector<AmigaHunk> hunks(last - first + 1);
    for (AmigaHunk& h : hunks) {
        const std::uint32_t words = r.u32() & 0x3FFFFFFF;
        if (words > file.size()) throw FormatError("Amiga program: a hunk larger than the file allows");
        h.size = words * 4;
    }
    std::size_t current = 0;
    bool started = false;
    while (r.at < file.size()) {
        const std::uint32_t type = r.u32() & 0x3FFFFFFF;
        if (type == kCode || type == kData || type == kBss) {
            if (started) ++current;
            started = true;
            if (current >= hunks.size()) throw FormatError("Amiga program: more hunks than its table");
            AmigaHunk& h = hunks[current];
            h.kind = type == kCode ? AmigaHunk::Kind::Code : type == kData ? AmigaHunk::Kind::Data : AmigaHunk::Kind::Bss;
            const std::uint32_t words = r.u32();
            if (type == kBss) continue;
            if (std::uint64_t(words) * 4 > h.size) throw FormatError("Amiga program: a hunk holds more than its size");
            const std::size_t start = r.at;
            r.skip_longs(words);
            h.bytes.assign(file.begin() + std::ptrdiff_t(start), file.begin() + std::ptrdiff_t(r.at));
        } else if (type == kReloc32) {
            while (const std::uint32_t count = r.u32()) {
                r.u32();  // the hunk the offsets point into
                r.skip_longs(count);
            }
        } else if (type == kSymbol) {
            while (const std::uint32_t name = r.u32()) r.skip_longs(std::uint64_t(name) + 1);
        } else if (type == kDebug) {
            r.skip_longs(r.u32());
        } else if (type != kEnd) {
            throw FormatError("Amiga program: a hunk type this reader doesn't know");
        }
    }
    if (!started || current + 1 != hunks.size()) throw FormatError("Amiga program: fewer hunks than its table");
    return hunks;
}

std::span<const AmigaSpan> amiga_spans() { return kSpans; }

AmigaProgram::AmigaProgram(std::span<const std::uint8_t> file) : hunks_(read_amiga_hunks(file)) {
    if (hunks_.size() < 2 || hunks_[1].kind != AmigaHunk::Kind::Data || sha256_hex(hunks_[1].bytes) != kAmigaDataHunkSha256)
        throw FormatError("this Amiga program is not version 2.3's");
}

std::span<const std::uint8_t> AmigaProgram::span(std::string_view id) const {
    for (const AmigaSpan& s : kSpans) {
        if (s.id != id) continue;
        const AmigaHunk& h = hunks_.at(std::size_t(s.hunk));
        if (std::uint64_t(s.offset) + s.length > h.bytes.size()) throw FormatError("Amiga span past its hunk's data");
        return std::span<const std::uint8_t>(h.bytes).subspan(s.offset, s.length);
    }
    throw FormatError("no Amiga span " + std::string(id));
}

std::span<const std::uint8_t> AmigaProgram::hunk(std::size_t n) const {
    if (n >= hunks_.size()) throw FormatError("the Amiga program has no hunk " + std::to_string(n));
    return hunks_[n].bytes;
}

std::array<MenuRecord, kMenuCount> amiga_menus(std::span<const std::uint8_t> menus) {
    if (menus.size() < std::size_t(kMenuCount) * 16) throw FormatError("the Amiga menu table is short");
    // Words 0, 1, 2, 4 and 5 of each 8-word record, as formats/tables.cpp reads DOS's.
    constexpr std::array<int, 5> kWords = {0, 1, 2, 4, 5};
    std::array<MenuRecord, kMenuCount> out{};
    for (std::size_t m = 0; m < out.size(); ++m) {
        int v[5];
        for (std::size_t k = 0; k < 5; ++k) {
            const std::size_t at = m * 16 + std::size_t(kWords[k]) * 2;
            v[k] = std::int16_t(std::uint16_t(menus[at] << 8 | menus[at + 1]));
        }
        out[m] = {v[0], v[1], v[2], v[3], v[4]};
    }
    return out;
}

}  // namespace chr::formats
