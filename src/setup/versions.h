// The game's other versions: recognising a copy of any of them in whatever a player points the
// setup at (a folder, a zip, zips inside zips, the Amiga .adf, the C64 .d64, the Apple II .dsk),
// by its files' names, sizes and SHA-256 hashes (docs/versions.md). The DOS copy is the setup's
// own (setup.h); this names the others, so a player who brings one is told what it is and what
// is still missing, rather than that nothing was found.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "formats/amiga_disk.h"

namespace chr::setup {

enum class Machine { Amiga, C64, AppleII };

// A file a known disk holds: its name (lower case, without its folder), size and hash.
struct KnownFile {
    const char* name;
    std::uint64_t size;
    const char* sha256;
};

// One disk of a version. It is recognised when every `files` entry is there; its variant is
// the one whose file is there too (none: a variant we don't know).
struct KnownDisk {
    Machine machine;
    const char* version;  // "" when the copy doesn't say
    int disk, disks;      // disk n of m
    std::vector<KnownFile> files;
    struct Variant {
        const char* label;  // "" for the plain copy, else its catalogue mark ("[a4]")
        KnownFile file;
    };
    std::vector<Variant> variants;
};

const std::vector<KnownDisk>& known_disks();

// The checks' own entries, from the file CHR_EXTRA_KNOWN_DISKS names (none when unset): one disk
// a line, "<amiga|c64|apple2> <disk> <of disks> <file name> <size> <sha256>". The checks make up
// a disk image and name it here, since the real table holds only the real copies' hashes.
std::vector<KnownDisk> extra_known_disks();

// A file seen in what was picked, by name (lower case, without its folder), size and hash.
struct SeenFile {
    std::string name;
    std::uint64_t size = 0;
    std::string sha256;
};

// Every file in `path` (a folder, an archive at any depth, a disk image, inside one another)
// that might be one of known_disks()'s files: those with a known name and size, hashed.
// `detail` (may be null) gets a line for each container it couldn't read.
std::vector<SeenFile> seen_files(const std::string& path, std::string* detail = nullptr);
// The same against another table (the tests' own).
std::vector<SeenFile> seen_files(const std::string& path, const std::vector<KnownDisk>& disks, std::string* detail);

// A version found among the seen files.
struct OtherCopy {
    Machine machine = Machine::Amiga;
    std::string version;
    int disks = 1;
    std::vector<int> found;          // its disks found, in order
    std::vector<std::string> variants;  // each found disk's variant ("" plain, "unknown")
    std::string name() const;        // "the Amiga version 2.3", "the Commodore 64 version"
    std::string label() const;       // for --status's "data": "Amiga 2.3", "Amiga 2.3 [a4]", "Apple II (changed)"
    std::vector<std::string> missing() const;  // "disk 2 of 2", in plain words
    // Whether this version's game is DOS's: the C64's and the Apple II's towns are their own
    // (docs/formats/c64.md), so their copies are recognised and never played.
    bool same_game() const;
    std::vector<std::string> needed() const;  // for --status's "missing"
    // For the player: what was found, and why it can't be played.
    std::string note() const;
};

std::vector<OtherCopy> recognise(const std::vector<SeenFile>& seen);
std::vector<OtherCopy> recognise(const std::vector<SeenFile>& seen, const std::vector<KnownDisk>& disks);

// seen_files then recognise, against known_disks() and extra_known_disks(): the best find (most
// disks, then the Amiga first), or none.
bool find_other_copy(const std::string& path, OtherCopy* out, std::string* detail = nullptr);

// An Amiga copy the rebuild plays, on a disk image anywhere in what was picked.
struct AmigaCopy {
    std::string where;    // the image, with the archives it is in ("a.zip/b.zip/c.adf")
    std::string variant;  // as KnownDisk::Variant::label, "unknown" for one we don't know
    formats::AmigaFiles files;
};

struct AmigaChoice {
    std::optional<AmigaCopy> copy;
    std::size_t different = 0;  // how many different copies there are
    // Which was taken when there are several, or why none was; "" when there is nothing to say.
    std::string note;
};

// The Amiga copy in `path` (a folder, an archive at any depth, a disk image). Images with the
// same game files are one copy. Of several different copies the plain build is taken, and `note`
// says so; without it none is, and `note` names them.
AmigaChoice choose_amiga_copy(const std::string& path);

}  // namespace chr::setup
