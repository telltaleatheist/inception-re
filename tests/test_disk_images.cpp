// The disk image readers on images built here, byte by byte (no game files).
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "formats/cmp.h"
#include "formats/disk_image.h"
#include "formats/exe_data.h"
#include "formats/open_source.h"
#include "test.h"

using namespace chr::formats;

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes text(const std::string& s) { return Bytes(s.begin(), s.end()); }

bool throws(const Bytes& image) {
    try {
        read_disk_image(image);
    } catch (const FormatError&) {
        return true;
    }
    return false;
}

// --- Amiga OFS ---

struct Adf {
    Bytes d = Bytes(901120, 0);
    std::uint32_t next = 882;  // free blocks after the root
    void put32(std::uint32_t block, std::size_t at, std::uint32_t v) {
        std::uint8_t* p = &d[block * 512 + at];
        p[0] = std::uint8_t(v >> 24), p[1] = std::uint8_t(v >> 16), p[2] = std::uint8_t(v >> 8), p[3] = std::uint8_t(v);
    }
    void name(std::uint32_t block, const std::string& n) {
        d[block * 512 + 432] = std::uint8_t(n.size());
        for (std::size_t i = 0; i < n.size(); ++i) d[block * 512 + 433 + i] = std::uint8_t(n[i]);
    }
    Adf() {
        d[0] = 'D', d[1] = 'O', d[2] = 'S', d[3] = 0;
        put32(880, 0, 2), put32(880, 508, 1);
        name(880, "Fixture");
    }
    // An entry in `dir`'s hash table, slot `slot` (chained after what is there).
    void link(std::uint32_t dir, std::size_t slot, std::uint32_t entry) {
        const std::size_t at = 24 + 4 * slot;
        std::uint32_t k = std::uint32_t(d[dir * 512 + at]) << 24 | std::uint32_t(d[dir * 512 + at + 1]) << 16 |
                          std::uint32_t(d[dir * 512 + at + 2]) << 8 | d[dir * 512 + at + 3];
        put32(entry, 496, k);
        put32(dir, at, entry);
    }
    std::uint32_t folder(std::uint32_t parent, std::size_t slot, const std::string& n) {
        const std::uint32_t b = next++;
        put32(b, 0, 2), put32(b, 508, std::uint32_t(2)), name(b, n);
        link(parent, slot, b);
        return b;
    }
    std::uint32_t file(std::uint32_t parent, std::size_t slot, const std::string& n, const Bytes& data) {
        const std::uint32_t h = next++;
        put32(h, 0, 2), put32(h, 508, std::uint32_t(-3)), put32(h, 324, std::uint32_t(data.size())), name(h, n);
        link(parent, slot, h);
        std::uint32_t prev = 0;
        for (std::size_t at = 0; at < data.size(); at += 488) {
            const std::uint32_t b = next++;
            const std::size_t n2 = std::min<std::size_t>(488, data.size() - at);
            put32(b, 0, 8), put32(b, 4, h), put32(b, 12, std::uint32_t(n2));
            std::copy(data.begin() + std::ptrdiff_t(at), data.begin() + std::ptrdiff_t(at + n2), d.begin() + b * 512 + 24);
            if (prev) put32(prev, 16, b);
            else put32(h, 16, b);
            prev = b;
        }
        return h;
    }
};

// --- C64 1541 ---

struct D64 {
    Bytes d = Bytes(174848, 0);
    static std::size_t at(int t, int s) {
        std::size_t before = 0;
        for (int i = 1; i < t; ++i) before += i <= 17 ? 21 : i <= 24 ? 19 : i <= 30 ? 18 : 17;
        return (before + std::size_t(s)) * 256;
    }
    D64() {
        d[at(18, 0)] = 18, d[at(18, 0) + 1] = 1;  // the directory starts at 18/1
        d[at(18, 1)] = 0, d[at(18, 1) + 1] = 0xff;
    }
    // A PRG at `slot` of the first directory sector, its data on track `t` from sector 0.
    void file(int slot, const std::string& n, const Bytes& data, int t) {
        std::uint8_t* e = &d[at(18, 1) + std::size_t(slot) * 32];
        e[2] = 0x82, e[3] = std::uint8_t(t), e[4] = 0;
        for (int i = 0; i < 16; ++i) e[5 + i] = i < int(n.size()) ? std::uint8_t(n[std::size_t(i)]) : 0xa0;
        int s = 0;
        for (std::size_t from = 0; from < data.size(); from += 254, ++s) {
            const std::size_t n2 = std::min<std::size_t>(254, data.size() - from);
            std::uint8_t* p = &d[at(t, s)];
            std::copy(data.begin() + std::ptrdiff_t(from), data.begin() + std::ptrdiff_t(from + n2), p + 2);
            if (from + n2 < data.size()) p[0] = std::uint8_t(t), p[1] = std::uint8_t(s + 1);
            else p[0] = 0, p[1] = std::uint8_t(n2 + 1);
        }
    }
};

// --- Apple II RDOS 3.3 ---

// Written from docs/formats/rdos.md, "The sectors' order", as a table: where in the image's track
// each of RDOS's 16 sectors is.
constexpr std::size_t kRdosSectorAt[16] = {0, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 15};

struct Rdos {
    Bytes d = Bytes(143360, 0);
    std::size_t slot = 1;
    // The image's byte for byte `at` of the disk as RDOS counts it.
    std::uint8_t& byte(std::size_t at) {
        const std::size_t sector = at / 256;
        return d[(sector / 16 * 16 + kRdosSectorAt[sector % 16]) * 256 + at % 256];
    }
    Rdos() {
        const std::string mark = "RDOS 3.3 FIXTURE";
        for (std::size_t i = 0; i < mark.size(); ++i) byte(4096 + i) = std::uint8_t(mark[i] | 0x80);
    }
    void file(const std::string& n, const Bytes& data, std::size_t start) {
        const std::size_t e = 4096 + slot++ * 32;
        for (std::size_t i = 0; i < 24; ++i) byte(e + i) = std::uint8_t((i < n.size() ? n[i] : ' ') | 0x80);
        byte(e + 24) = 'B' | 0x80, byte(e + 25) = std::uint8_t((data.size() + 255) / 256);
        byte(e + 28) = std::uint8_t(data.size()), byte(e + 29) = std::uint8_t(data.size() >> 8);
        byte(e + 30) = std::uint8_t(start), byte(e + 31) = std::uint8_t(start >> 8);
        for (std::size_t i = 0; i < data.size() && start * 256 + i < d.size(); ++i) byte(start * 256 + i) = data[i];
    }
};

Bytes pattern(std::size_t n, std::uint8_t seed) {
    Bytes b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = std::uint8_t(seed + i * 7);
    return b;
}

}  // namespace

TEST(adf_lists_folders_and_reads_files_across_blocks) {
    Adf a;
    const Bytes big = pattern(1200, 3);  // three OFS data blocks
    const std::uint32_t game = a.folder(880, 5, "Game");
    a.file(game, 1, "data.bin", big);
    a.file(game, 1, "small", text("abc"));  // chained in the same hash slot
    a.file(880, 9, "top", text("x"));
    CHECK(disk_kind_by_size(a.d.size()) == DiskKind::Adf);
    CHECK(adf_volume(a.d) == "Fixture");
    const auto files = read_disk_image(a.d);
    CHECK(files.size() == 3);
    bool saw_big = false, saw_small = false, saw_top = false;
    for (const auto& f : files) {
        if (f.path == "Game/data.bin") saw_big = f.data == big;
        if (f.path == "Game/small") saw_small = f.data == text("abc");
        if (f.path == "top") saw_top = f.data == text("x");
    }
    CHECK(saw_big && saw_small && saw_top);
}

TEST(adf_refuses_a_loop_and_a_foreign_disk) {
    Adf a;
    const std::uint32_t f = a.file(880, 2, "loop", pattern(600, 1));
    a.put32(f + 1, 16, f + 1);  // the first data block points at itself
    CHECK(throws(a.d));
    Adf b;
    b.d[0] = 'X';
    CHECK(throws(b.d));
    Adf c;
    c.put32(880, 0, 0);  // no root block
    CHECK(throws(c.d));
}

TEST(d64_reads_a_chained_file_and_keeps_its_load_address) {
    D64 d;
    Bytes data{0x00, 0x20};  // load address
    const Bytes body = pattern(600, 9);
    data.insert(data.end(), body.begin(), body.end());
    d.file(0, "FIXTURE", data, 1);
    d.file(1, "B", text("hi"), 2);
    CHECK(disk_kind_by_size(d.d.size()) == DiskKind::D64);
    const auto files = read_disk_image(d.d);
    CHECK(files.size() == 2);
    CHECK(files[0].path == "FIXTURE" && files[0].data == data);
    CHECK(files[1].path == "B" && files[1].data == text("hi"));
}

TEST(d64_refuses_a_loop_and_a_sector_off_the_disk) {
    D64 d;
    d.file(0, "LOOP", pattern(600, 1), 1);
    d.d[D64::at(1, 1)] = 1, d.d[D64::at(1, 1) + 1] = 0;  // back to the first sector
    CHECK(throws(d.d));
    D64 e;
    e.file(0, "FAR", pattern(10, 1), 1);
    e.d[D64::at(18, 1) + 3] = 40;  // track 40
    CHECK(throws(e.d));
}

TEST(rdos_reads_its_catalog) {
    Rdos r;
    const Bytes a = pattern(700, 5), b = text("tiny");
    r.file("FIRST FILE", a, 32);
    r.file("B", b, 40);
    CHECK(disk_kind_by_size(r.d.size()) == DiskKind::Rdos);
    const auto files = read_disk_image(r.d);
    CHECK(files.size() == 2);
    CHECK(files[0].path == "FIRST FILE" && files[0].data == a);
    CHECK(files[1].path == "B" && files[1].data == b);
}

TEST(rdos_reads_files_and_the_catalog_in_rdos_s_sector_order) {
    Rdos r;
    const Bytes track = pattern(16 * 256, 1), across = pattern(5000, 2);  // a whole track; across two tracks
    r.file("TRACK", track, 48);
    r.file("ACROSS", across, 70);
    std::vector<std::string> names{"TRACK", "ACROSS"};
    for (int i = 0; i < 18; ++i) {  // on into the catalog's third sector
        names.push_back("FILE" + std::to_string(i));
        r.file(names.back(), text(names.back()), 100 + std::size_t(i));
    }
    const auto files = read_disk_image(r.d);
    CHECK(files.size() == names.size());
    for (std::size_t i = 0; i < files.size() && i < names.size(); ++i) CHECK(files[i].path == names[i]);
    CHECK(files[0].data == track);
    CHECK(files[1].data == across);
    for (std::size_t i = 2; i < files.size(); ++i) CHECK(files[i].data == text(names[i]));
}

TEST(rdos_refuses_another_disk_and_an_entry_off_the_disk) {
    Rdos r;
    r.d[4096] = 'D' | 0x80;  // not RDOS
    CHECK(throws(r.d));
    Rdos s;
    s.file("FAR", pattern(512, 1), 559);  // two sectors from the last one
    CHECK(throws(s.d));
}

TEST(other_sizes_are_not_disk_images) {
    CHECK(disk_kind_by_size(1000) == DiskKind::None);
    CHECK(read_disk_image(Bytes(1000, 0)).empty());
}

// Which source a copy opens as (formats/open_source.h). The Amiga's program here is junk, so an
// Amiga copy is told by its refusal.
namespace {

std::string opened_as(const std::filesystem::path& copy) {
    try {
        return open_game_source(copy)->program_name();
    } catch (const FormatError& e) {
        return e.what();
    }
}

void put_file(const std::filesystem::path& path, const Bytes& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

}  // namespace

TEST(a_copy_opens_by_what_it_holds) {
    const auto dir = std::filesystem::path(CHR_TEST_SCRATCH) / "open-source";
    std::filesystem::remove_all(dir);
    const Bytes junk = pattern(64, 1);

    std::filesystem::create_directories(dir / "nothing");
    CHECK(opened_as(dir / "nothing") == dos_program_name());
    CHECK(opened_as(dir / "not-there") == dos_program_name());

    put_file(dir / "dos" / "btech.exe", junk);
    CHECK(opened_as(dir / "dos") == dos_program_name());

    for (const char* name : {"battletech", "BUILDS.BLD", "Maps"}) put_file(dir / "amiga" / "Battletech" / name, junk);
    const auto files = opened_as(dir / "amiga" / "Battletech");
    CHECK(files != dos_program_name());
    CHECK(opened_as(dir / "amiga") == files);  // the disk's top folder

    // A folder with both is the DOS copy.
    for (const char* name : {"battletech", "builds.bld", "maps"}) put_file(dir / "dos" / name, junk);
    CHECK(opened_as(dir / "dos") == dos_program_name());

    Adf disk;
    const auto folder = disk.folder(880, 0, "Battletech");
    disk.file(folder, 0, "battletech", junk);
    disk.file(folder, 1, "builds.bld", junk);
    put_file(dir / "part.adf", disk.d);
    CHECK(opened_as(dir / "part.adf").find("is not the Amiga game's") != std::string::npos);
    disk.file(folder, 2, "maps", junk);
    put_file(dir / "whole.adf", disk.d);
    CHECK(opened_as(dir / "whole.adf") == files);

    // A folder with disk images: other disks are passed over, the same copy twice is one copy,
    // different ones are refused by name.
    put_file(dir / "images" / "1.adf", disk.d);
    put_file(dir / "images" / "0-other.adf", Bytes(disk.d.size(), 0));
    put_file(dir / "images" / "readme.txt", junk);
    CHECK(opened_as(dir / "images") == files);
    put_file(dir / "images" / "2.adf", disk.d);
    CHECK(opened_as(dir / "images") == files);
    Adf changed;
    const auto changed_folder = changed.folder(880, 0, "Battletech");
    changed.file(changed_folder, 0, "battletech", junk);
    changed.file(changed_folder, 1, "builds.bld", junk);
    changed.file(changed_folder, 2, "maps", pattern(64, 2));
    put_file(dir / "images" / "3.adf", changed.d);
    CHECK(opened_as(dir / "images").find("1.adf, 2.adf, 3.adf") != std::string::npos);

    put_file(dir / "some.txt", junk);
    CHECK(opened_as(dir / "some.txt").find("not a folder or an Amiga disk image") != std::string::npos);
}
