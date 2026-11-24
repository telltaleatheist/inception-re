// A DOS copy's folder as a GameSource (formats/game_source.h).

#include "formats/game_source.h"

#include "formats/bld.h"
#include "formats/exe_data.h"
#include "formats/game_files.h"

namespace chr::formats {

std::optional<std::vector<std::uint8_t>> DosGameSource::file(const std::string& name) const {
    const auto path = find_game_file(dir_, name);
    if (!path) return std::nullopt;
    return read_file(*path);
}

std::optional<std::vector<std::uint8_t>> DosGameSource::script(int number) const {
    if (number < 0 || number >= static_cast<int>(kBldFiles.size())) return std::nullopt;
    const auto bytes = file(std::string(kBldFiles[std::size_t(number)]) + ".BLD");
    if (!bytes) return std::nullopt;
    return parse_bld(*bytes);
}

std::optional<std::vector<std::uint8_t>> DosGameSource::map(int number) const {
    return file("MAP" + std::to_string(number) + ".MTP");
}

std::optional<AnmPicture> DosGameSource::place_picture(int number) const {
    const auto bytes = file("O" + std::to_string(number) + ".ANM");
    if (!bytes) return std::nullopt;
    return decode_anm(*bytes);
}

std::optional<CmpPicture> DosGameSource::picture(const std::string& name) const {
    const auto bytes = file(name);
    if (!bytes) return std::nullopt;
    return decode_cmp(*bytes);
}

std::optional<TileSheet> DosGameSource::tile_sheet(const std::string& name) const {
    const auto bytes = file(name);
    if (!bytes) return std::nullopt;
    return decode_icn(*bytes);
}

std::optional<std::vector<std::uint8_t>> DosGameSource::demo() const { return file("DEMOFILE"); }

std::optional<std::vector<std::uint8_t>> DosGameSource::title_tune() const { return file("WWOODBT.SIF"); }

std::unique_ptr<const ProgramData> DosGameSource::program() const {
    return std::make_unique<const OriginalExe>(OriginalExe::load(dir_));
}

}  // namespace chr::formats
