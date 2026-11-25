#include "formats/amiga_disk.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <system_error>
#include <utility>

#include "formats/cmp.h"  // FormatError
#include "formats/disk_image.h"
#include "formats/game_files.h"

namespace chr::formats {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kFolder = "battletech";  // on the disk, in lower case

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool holds_the_game(const fs::path& dir) {
    return std::all_of(kAmigaTellingFiles.begin(), kAmigaTellingFiles.end(), [&](std::string_view name) {
        const auto path = find_game_file(dir, std::string(name));
        std::error_code ec;
        return path && fs::is_regular_file(*path, ec);
    });
}

}  // namespace

AmigaFiles amiga_disk_files(std::span<const std::uint8_t> image) {
    AmigaFiles files;
    if (disk_kind_by_size(image.size()) != DiskKind::Adf) return files;
    try {
        for (auto& f : read_adf(image)) {
            const std::string path = lower(f.path);
            if (path.size() <= kFolder.size() || path.compare(0, kFolder.size(), kFolder) != 0 || path[kFolder.size()] != '/')
                continue;
            const std::string name = path.substr(kFolder.size() + 1);
            if (std::find(kAmigaFiles.begin(), kAmigaFiles.end(), name) != kAmigaFiles.end()) files[name] = std::move(f.data);
        }
    } catch (const FormatError&) {
        return {};
    }
    const bool game = std::all_of(kAmigaTellingFiles.begin(), kAmigaTellingFiles.end(),
                                  [&](std::string_view name) { return files.contains(std::string(name)); });
    return game ? files : AmigaFiles{};
}

std::vector<std::string> amiga_files_missing(const AmigaFiles& files) {
    std::vector<std::string> missing;
    for (const auto name : kAmigaFiles)
        if (!files.contains(std::string(name))) missing.emplace_back(name);
    return missing;
}

std::optional<fs::path> amiga_folder(const fs::path& dir) {
    if (holds_the_game(dir)) return dir;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        if (const auto name = name_text(it->path().filename()); name && lower(*name) == kFolder && holds_the_game(it->path()))
            return it->path();
    }
    return std::nullopt;
}

std::vector<AmigaImage> amiga_images_in(const fs::path& dir) {
    std::vector<fs::path> paths;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && disk_kind_by_size(it->file_size(e2)) == DiskKind::Adf && name_text(it->path()))
            paths.push_back(it->path());
    }
    std::sort(paths.begin(), paths.end());
    std::vector<AmigaImage> images;
    for (const auto& path : paths) {
        std::vector<std::uint8_t> image;
        try {
            image = read_file(path);
        } catch (const std::exception&) {
            continue;  // can't be opened: the others are looked at
        }
        if (auto files = amiga_disk_files(image); !files.empty()) images.push_back({path, std::move(files)});
    }
    return images;
}

}  // namespace chr::formats
