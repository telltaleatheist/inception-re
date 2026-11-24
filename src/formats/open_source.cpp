#include "formats/open_source.h"

#include <string>
#include <system_error>
#include <utility>

#include "formats/amiga_disk.h"
#include "formats/amiga_source.h"
#include "formats/cmp.h"  // FormatError
#include "formats/disk_image.h"
#include "formats/game_files.h"

namespace chr::formats {

namespace fs = std::filesystem;

namespace {

std::shared_ptr<const GameSource> amiga_source(AmigaFiles files, const fs::path& where) {
    return std::make_shared<const AmigaGameSource>(std::move(files), where.string());
}

std::shared_ptr<const GameSource> open_image(const fs::path& image) {
    std::error_code ec;
    const auto size = fs::file_size(image, ec);
    if (ec || disk_kind_by_size(size) != DiskKind::Adf)
        throw FormatError(image.string() + " is not a folder or an Amiga disk image (.adf)");
    auto files = amiga_disk_files(read_file(image));
    if (files.empty())
        throw FormatError("the disk image " + image.string() + " is not the Amiga game's: it has no Battletech/" +
                          std::string(kAmigaTellingFiles[0]) + ", " + std::string(kAmigaTellingFiles[1]) + " and " +
                          std::string(kAmigaTellingFiles[2]));
    return amiga_source(std::move(files), image);
}

}  // namespace

std::shared_ptr<const GameSource> open_game_source(const fs::path& copy) {
    std::error_code ec;
    if (fs::is_regular_file(copy, ec)) return open_image(copy);
    if (fs::is_directory(copy, ec) && !find_game_file(copy, "BTECH.EXE")) {
        if (const auto folder = amiga_folder(copy))
            return std::make_shared<const AmigaGameSource>(AmigaGameSource::from_folder(*folder));
        auto images = amiga_images_in(copy);
        for (const auto& image : images)
            if (image.files != images.front().files) {
                std::string names;
                for (const auto& i : images) names += (names.empty() ? "" : ", ") + i.path.filename().string();
                throw FormatError("there are different copies of the Amiga game in " + copy.string() + " (" + names +
                                  "): name the disk image to play");
            }
        if (!images.empty()) return amiga_source(std::move(images.front().files), images.front().path);
    }
    return std::make_shared<const DosGameSource>(copy);
}

}  // namespace chr::formats
