#include "formats/open_source.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "formats/amiga_source.h"
#include "formats/cmp.h"  // FormatError
#include "formats/disk_image.h"
#include "formats/game_files.h"

namespace chr::formats {

namespace fs = std::filesystem;

namespace {

constexpr const char* kAmigaFolder = "Battletech";  // on the Amiga's disk
constexpr const char* kAmigaFiles[] = {"battletech", "builds.bld", "maps"};

bool is_amiga_folder(const fs::path& dir) {
    return std::all_of(std::begin(kAmigaFiles), std::end(kAmigaFiles), [&](const char* name) {
        const auto path = find_game_file(dir, name);
        std::error_code ec;
        return path && fs::is_regular_file(*path, ec);
    });
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The files of a disk image's Battletech folder, by their names there.
std::map<std::string, std::vector<std::uint8_t>> amiga_disk_files(const fs::path& image) {
    std::error_code ec;
    const auto size = fs::file_size(image, ec);
    if (ec || disk_kind_by_size(size) != DiskKind::Adf)
        throw FormatError(image.string() + " is not a folder or an Amiga disk image (.adf)");
    std::map<std::string, std::vector<std::uint8_t>> files;
    const std::string folder = lower(kAmigaFolder) + "/";
    for (auto& f : read_adf(read_file(image))) {
        const std::string path = lower(f.path);
        if (path.compare(0, folder.size(), folder) == 0 && path.find('/', folder.size()) == std::string::npos)
            files[path.substr(folder.size())] = std::move(f.data);
    }
    for (const char* name : kAmigaFiles)
        if (!files.contains(name))
            throw FormatError("the disk image " + image.string() + " is not the Amiga game's: it has no " +
                              std::string(kAmigaFolder) + "/" + name);
    return files;
}

}  // namespace

std::shared_ptr<const GameSource> open_game_source(const fs::path& copy) {
    std::error_code ec;
    if (fs::is_regular_file(copy, ec)) return std::make_shared<const AmigaGameSource>(amiga_disk_files(copy), copy.string());
    if (fs::is_directory(copy, ec) && !find_game_file(copy, "BTECH.EXE")) {
        if (is_amiga_folder(copy)) return std::make_shared<const AmigaGameSource>(AmigaGameSource::from_folder(copy));
        for (const auto& entry : fs::directory_iterator(copy, ec))
            if (entry.is_directory(ec) && lower(entry.path().filename().string()) == lower(kAmigaFolder) &&
                is_amiga_folder(entry.path()))
                return std::make_shared<const AmigaGameSource>(AmigaGameSource::from_folder(entry.path()));
    }
    return std::make_shared<const DosGameSource>(copy);
}

}  // namespace chr::formats
