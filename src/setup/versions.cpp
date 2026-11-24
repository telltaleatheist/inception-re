// Recognising the game's other versions (setup/versions.h, docs/versions.md).

#include "setup/versions.h"

#include <rekit/archive.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <set>
#include <tuple>

#include "formats/cmp.h"  // FormatError
#include "formats/disk_image.h"
#include "formats/sha256.h"

namespace chr::setup {

namespace fs = std::filesystem;

// Each known disk's files, by name, size and SHA-256 only (docs/versions.md, "Recognising a
// copy"). The common files are game files every copy of that disk has; a variant's file is
// the one its copies differ in (the Amiga's program, the Apple II's boot and view files).
const std::vector<KnownDisk>& known_disks() {
    static const std::vector<KnownDisk> disks{
    {Machine::Amiga, "2.3", 1, 1,
     {
         {"btborder.cmp", 274, "61df894f6652d57b18245b0e0fc184434c8205d7e5070883af595afda5a2bcf9"},
         {"animate.icn", 3454, "3f1002e8c5c72064b48b2e31d0f4871705b5c5992cd6f1034707e36c01ba2803"},
         {"animation.anm", 134378, "f6d3a5d2bb9efaa9be07ff0c5a0a0ae63e1b7242c08f74e78025c0129f81189e"},
         {"btstats.cmp", 11425, "5a3fead07c152ac6a4c29e05ba1453dc0a545ef6cfd1c5a44525035fdff8612e"},
         {"bttitle.cmp", 23804, "972b1fa9603c1e7068be88acce77f1347a92df9962e2f20dc634e104cccc1dc0"},
         {"bttltech.icn", 27050, "62ab9479741c9a6bfbd79c645c2d361f1075a4a69aea258209fe93b383f987d2"},
         {"builds.bld", 55861, "98193b4ebd0ac8fe8c8f1bf665ea4814596e9069c32a7442c9fdd110f08310db"},
         {"demofile", 1023, "7741627fa66a50d58bf89332627b87fb7a52d65ceb7f8b3d3e997409ef2f4b41"},
         {"destruct.icn", 28097, "29027166aabd0ebf1db8e436da31e8ea83a99d4c240f0c9bfe9887fbbff21a98"},
         {"endmech.cmp", 15732, "8872c5b4b54f4c9793043b1be47ff456a6eae39e029d8226634111a7d45795fb"},
         {"infocom.cmp", 7136, "446edd9aec472d339f5e0ad1f98b79eadc420c20e5e16113aa3dbe4d9556edb4"},
         {"map.icn", 21794, "71be54dd5403a42c93267cf429290c39b8805a2ca8baa86aa4b2aa860d1f7dc0"},
         {"maps", 33046, "c175c876b7c547dea6be894450b25e47b28bdc7a29755ae496fa7f941838173d"},
         {"mechshap.cmp", 17389, "55112fd7acee900e3e37f647133836e9cd9046442775776dacc11f4a7731e6d0"},
         {"snd", 65532, "76066a8239e2a94751a233ebba44f8f3ca570527cae624f1532d57bae57f7d6a"},
         {"starleag.icn", 25990, "87c7d1cf29236ca18000257356bc76100cc2141cfef79ade915d1f32a1e9bd6c"},
         {"tinyland.cmp", 1890, "6d7907f70e6db3bb80187bb8dec8b9e3fad14fa014df1db910bc25dbeda6a48a"},
     },
     {
         {"", {"battletech", 204784, "08e300c25fa7fb97979a03f7d2831a1af349db025bd7fd9a4bcc7fffae95b8be"}},
         {"[a4]", {"battletech", 204784, "e6541a065ad657a40afaf44d3b79d57c44604668159acd7e6d4da8fadcd5d8a0"}},
         {"[h Madwand]", {"battletech", 204784, "70d07087165b721b516f95a2128294a3fdf04e6a0e586c6e5805001c7d4cee5a"}},
     }},
    {Machine::C64, "", 1, 2,
     {
         {"mechs", 3074, "075b118222fbaa01f9756ec2913de613068474733a9b60a5fee6e1e278223554"},
         {"seta", 2050, "598371a7d1fe56ca1a100b8b3a5a2c59fedc118969ee805c0b5c8f3e3e44fb84"},
         {"tpag", 8002, "2894f4e2b36ecf4d0efb60bf7b5508f22cf98cea1ed89750b0e92790ba867d02"},
     },
     {
     }},
    {Machine::C64, "", 2, 2,
     {
         {"bc", 1489, "c874ff0192adb3df58914b20687c91dcb38c61d40a385f050c04fc4b6a7c1f64"},
         {"mz", 1026, "447b2484f179dcccd5acb6d623a8b069fbbe9b7166a52e2c6deb0102d894a729"},
         {"sp.txt", 361, "de23af75deac1e75d82b5edb5bbc6208937f970ce95e88576842393564403065"},
     },
     {
     }},
    {Machine::AppleII, "", 1, 2,
     {
         {"btech", 23808, "88f0b46b602eb13e457a60b32a952ce962f6ea71153efdd1bb90f20b22e60e8d"},
         {"set1", 1048, "bcb078a7e36920ab066eee6ef92a69595be0a90671ac49610847a07bb27c32ad"},
         {"title", 6657, "586abc202be01c379e8221639c7e9e8fe7080833913119e648c0c795ab334be1"},
     },
     {
         {"", {"hello", 32, "5ab622fb8bb0278b60436789c2e1fedcee2887bfa285eea732d533c6d852a399"}},
         {"[cr]", {"hello", 125, "a3ef0bc008fb5184b6b2ba0316e6087c087f4b22d0dc1f2f98263188ed56acae"}},
     }},
    {Machine::AppleII, "", 2, 2,
     {
         {"det.scrn", 2304, "ce27bbccec80017c89aea4fcc72086cbe4a0e39c7eea1ada917b0e2d9daba91e"},
         {"i0", 7520, "3e1e5746d150d51c99c02d5bffc8b608a9346449ffee21484071f5ba5a3c42e0"},
         {"mechs", 3456, "3e5d6397b9a3ccb426623bbd4ad4afad842701081c5c833a0f2b29ada226e6a3"},
     },
     {
         {"", {"view", 2033, "fb202ac6488331b95278415668397b393ef4bd8c4aacc1a8018c2c3b2553bfbf"}},
         {"[a]", {"view", 2033, "8a41512cf9387c030048ff2f2d74ac5bc1d736316ffbe5de04c7d715f97323e9"}},
     }},
    };
    return disks;
}

namespace {

constexpr int kMaxArchiveDepth = 4;
constexpr int kMaxFolderDepth = 6;
constexpr std::size_t kMaxFolderEntries = 50000;
constexpr std::size_t kMaxImage = 2u << 20;  // larger than any disk image we read

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string base_name(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return lower(slash == std::string::npos ? path : path.substr(slash + 1));
}

bool disk_name(const std::string& name) {
    for (const char* ext : {".adf", ".d64", ".dsk", ".do"})
        if (name.size() > std::strlen(ext) && name.compare(name.size() - std::strlen(ext), std::string::npos, ext) == 0)
            return true;
    return false;
}

struct Walk {
    const std::vector<KnownDisk>& disks;
    std::set<std::pair<std::string, std::uint64_t>> wanted;
    std::vector<SeenFile> seen;
    std::string* detail;

    explicit Walk(const std::vector<KnownDisk>& d, std::string* why) : disks(d), detail(why) {
        for (const KnownDisk& k : disks) {
            for (const KnownFile& f : k.files) wanted.insert({f.name, f.size});
            for (const auto& v : k.variants) wanted.insert({v.file.name, v.file.size});
        }
    }
    void note(const std::string& line) {
        if (detail) *detail += line + "\n";
    }
    bool wants(const std::string& name, std::uint64_t size) const { return wanted.count({name, size}) != 0; }
    void add(const std::string& name, std::span<const std::uint8_t> data) {
        seen.push_back({name, data.size(), formats::sha256_hex(data)});
    }
    void image(const std::string& where, std::span<const std::uint8_t> bytes) {
        try {
            for (const auto& f : formats::read_disk_image(bytes)) {
                const std::string name = base_name(f.path);
                if (wants(name, f.data.size())) add(name, f.data);
            }
        } catch (const formats::FormatError& e) {
            note(where + ": " + e.what());
        }
    }
    // A file's bytes (or the archive entry's), when it is a disk image or a wanted file.
    void bytes(const std::string& where, const std::string& name, const std::vector<std::uint8_t>& data) {
        if (formats::disk_kind_by_size(data.size()) != formats::DiskKind::None) image(where, data);
        else if (wants(name, data.size())) add(name, data);
    }

    void archive(rekit::ArchiveReader& r, const std::string& where, int depth) {
        rekit::ArchiveReader::Entry e;
        while (r.next(&e)) {
            if (e.dir || e.other) continue;
            const std::string name = base_name(e.path), at = where + "/" + e.path;
            if (rekit::archive_name(name)) {
                if (depth >= kMaxArchiveDepth) {
                    note(at + ": archives nested too deep");
                    continue;
                }
                rekit::ArchiveReader inner;
                if (inner.open_stream(r.entry_stream())) archive(inner, at, depth + 1);
                else note(at + ": " + inner.error());
                continue;
            }
            const bool image = e.size >= 0 ? formats::disk_kind_by_size(std::uint64_t(e.size)) != formats::DiskKind::None
                                           : disk_name(name);
            if (!image && !(e.size >= 0 && wants(name, std::uint64_t(e.size)))) continue;
            std::vector<std::uint8_t> data;
            std::uint8_t buf[65536];
            for (std::int64_t n; (n = r.read(buf, sizeof buf)) > 0 && data.size() <= kMaxImage;)
                data.insert(data.end(), buf, buf + n);
            if (data.size() <= kMaxImage) bytes(at, name, data);
        }
        if (!r.error().empty()) note(where + ": " + r.error());
    }

    void file(const fs::path& p, std::uint64_t size) {
        const std::string name = base_name(p.filename().string());
        if (rekit::archives_supported() && rekit::archive_name(name)) {
            rekit::ArchiveReader r;
            if (r.open_file(p.string())) archive(r, p.filename().string(), 0);
            else note(p.filename().string() + ": " + r.error());
            return;
        }
        if (formats::disk_kind_by_size(size) == formats::DiskKind::None && !wants(name, size)) return;
        std::ifstream in(p, std::ios::binary);
        std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), {});
        bytes(p.filename().string(), name, data);
    }

    void path(const fs::path& p) {
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) return file(p, fs::file_size(p, ec));
        if (!fs::is_directory(p, ec)) return;
        std::size_t entries = 0;
        for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (++entries > kMaxFolderEntries) return note(p.string() + ": too many files to look through");
            std::error_code e2;
            if (it.depth() >= kMaxFolderDepth || it->is_symlink(e2)) {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file(e2)) file(it->path(), it->file_size(e2));
        }
    }
};

const char* machine_name(Machine m) {
    switch (m) {
        case Machine::Amiga:
            return "Amiga";
        case Machine::C64:
            return "Commodore 64";
        case Machine::AppleII:
            return "Apple II";
    }
    return "";
}

}  // namespace

std::vector<SeenFile> seen_files(const std::string& path, std::string* detail) {
    return seen_files(path, known_disks(), detail);
}

std::vector<SeenFile> seen_files(const std::string& path, const std::vector<KnownDisk>& disks, std::string* detail) {
    Walk w(disks, detail);
    w.path(fs::path(path));
    return std::move(w.seen);
}

std::string OtherCopy::name() const {
    std::string s = std::string("the ") + machine_name(machine) + " version";
    if (!version.empty()) s += " " + version;
    return s;
}

std::string OtherCopy::label() const {
    std::string s = machine_name(machine);
    if (!version.empty()) s += " " + version;
    for (const std::string& v : variants)
        if (!v.empty()) s += " " + (v == "unknown" ? std::string("(changed)") : v);
    return s;
}

std::vector<std::string> OtherCopy::missing() const {
    std::vector<std::string> m;
    for (int d = 1; d <= disks; ++d)
        if (std::find(found.begin(), found.end(), d) == found.end())
            m.push_back("disk " + std::to_string(d) + " of " + std::to_string(disks));
    return m;
}

std::string OtherCopy::note() const {
    std::string s;
    const auto gone = missing();
    if (gone.empty()) {
        s = name() + " was found";
        s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
    } else {
        s = "Disk " + std::to_string(found.front()) + " of " + std::to_string(disks) + " of " + name() +
            " was found (";
        for (std::size_t i = 0; i < gone.size(); ++i) s += (i ? ", " : "") + gone[i];
        s += " is missing)";
    }
    if (std::find(variants.begin(), variants.end(), "unknown") != variants.end())
        s += ", changed from the copies we know";
    return s + ", but the rebuild can't play it yet: it plays the DOS version 1.03 for now.";
}

std::vector<OtherCopy> recognise(const std::vector<SeenFile>& seen) { return recognise(seen, known_disks()); }

std::vector<OtherCopy> recognise(const std::vector<SeenFile>& seen, const std::vector<KnownDisk>& disks) {
    std::set<std::tuple<std::string, std::uint64_t, std::string>> have;
    for (const SeenFile& f : seen) have.insert({f.name, f.size, f.sha256});
    auto has = [&have](const KnownFile& f) { return have.count({f.name, f.size, f.sha256}) != 0; };
    std::vector<OtherCopy> out;
    for (const KnownDisk& k : disks) {
        if (!std::all_of(k.files.begin(), k.files.end(), has)) continue;
        std::string variant = k.variants.empty() ? "" : "unknown";
        for (const auto& v : k.variants)
            if (has(v.file)) {
                variant = v.label;
                break;
            }
        auto copy = std::find_if(out.begin(), out.end(), [&k](const OtherCopy& c) {
            return c.machine == k.machine && c.version == k.version;
        });
        if (copy == out.end()) {
            out.push_back({k.machine, k.version, k.disks, {}, {}});
            copy = out.end() - 1;
        }
        if (std::find(copy->found.begin(), copy->found.end(), k.disk) != copy->found.end()) continue;
        copy->found.push_back(k.disk), copy->variants.push_back(variant);
    }
    for (OtherCopy& c : out) {  // disks in order
        std::vector<std::pair<int, std::string>> d;
        for (std::size_t i = 0; i < c.found.size(); ++i) d.push_back({c.found[i], c.variants[i]});
        std::sort(d.begin(), d.end());
        for (std::size_t i = 0; i < d.size(); ++i) c.found[i] = d[i].first, c.variants[i] = d[i].second;
    }
    std::stable_sort(out.begin(), out.end(), [](const OtherCopy& a, const OtherCopy& b) {
        if (a.missing().size() != b.missing().size()) return a.missing().size() < b.missing().size();
        return a.machine < b.machine;
    });
    return out;
}

std::vector<KnownDisk> extra_known_disks() {
    std::vector<KnownDisk> out;
    const char* file = std::getenv("CHR_EXTRA_KNOWN_DISKS");
    if (!file || !*file) return out;
    static std::deque<std::string> words;  // the entries' names and hashes, kept for their pointers
    std::ifstream in(file);
    std::string machine, name, sha;
    int disk = 0, disks = 0;
    std::uint64_t size = 0;
    while (in >> machine >> disk >> disks >> name >> size >> sha) {
        const Machine m = machine == "c64" ? Machine::C64 : machine == "apple2" ? Machine::AppleII : Machine::Amiga;
        words.push_back(lower(name));
        const char* n = words.back().c_str();
        words.push_back(lower(sha));
        out.push_back({m, "", disk, disks, {{n, size, words.back().c_str()}}, {}});
    }
    return out;
}

bool find_other_copy(const std::string& path, OtherCopy* out, std::string* detail) {
    std::vector<KnownDisk> table = known_disks();
    for (KnownDisk& k : extra_known_disks()) table.push_back(std::move(k));
    const auto copies = recognise(seen_files(path, table, detail), table);
    if (copies.empty()) return false;
    if (out) *out = copies.front();
    return true;
}

}  // namespace chr::setup
