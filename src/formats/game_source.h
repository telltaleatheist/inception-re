// The player's original game, by what each piece is (docs/versions.md, "One source by ID"): a
// place's script, a map, a place picture, a full-screen picture, a tile sheet, the demo, the
// title tune. Each comes in DOS 1.03's working form, whichever version the copy is, so the game
// and the asset provider never see a file name, a folder or a version. DosGameSource reads a
// DOS copy's folder; other versions' sources lay their files out in the same forms.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "formats/anm.h"
#include "formats/cmp.h"
#include "formats/icn.h"

namespace chr::formats {

class GameSource {
public:
    virtual ~GameSource() = default;

    // Where the copy is, for messages only.
    virtual std::string describe() const = 0;

    // Place script `file` (0-25, formats/bld.h kBldFiles), deciphered, as DOS's <name>.BLD.
    virtual std::optional<std::vector<std::uint8_t>> script(int file) const = 0;
    // Map `number` (1-15), the bytes of DOS's MAP<number>.MTP (formats/mtp.h).
    virtual std::optional<std::vector<std::uint8_t>> map(int number) const = 0;
    // Place picture `number` (0-21), DOS's O<number>.ANM decoded (formats/anm.h).
    virtual std::optional<AnmPicture> place_picture(int number) const = 0;
    // A full-screen picture by its DOS name ("BTTITLE.CMP", ...), decoded (formats/cmp.h).
    virtual std::optional<CmpPicture> picture(const std::string& name) const = 0;
    // A tile sheet by its DOS name ("BTTLTECH.ICN", ...), decoded (formats/icn.h).
    virtual std::optional<TileSheet> tile_sheet(const std::string& name) const = 0;
    // The demo's keys, DOS's DEMOFILE.
    virtual std::optional<std::vector<std::uint8_t>> demo() const = 0;
    // The title tune, DOS's WWOODBT.SIF.
    virtual std::optional<std::vector<std::uint8_t>> title_tune() const = 0;
};

// A DOS copy: a folder holding BTECH.EXE's data files, found whatever their case
// (formats/game_files.h find_game_file). Nothing found is nullopt; a broken file throws
// FormatError, as the readers do.
class DosGameSource final : public GameSource {
public:
    explicit DosGameSource(std::filesystem::path dir) : dir_(std::move(dir)) {}

    std::string describe() const override { return dir_.string(); }
    std::optional<std::vector<std::uint8_t>> script(int file) const override;
    std::optional<std::vector<std::uint8_t>> map(int number) const override;
    std::optional<AnmPicture> place_picture(int number) const override;
    std::optional<CmpPicture> picture(const std::string& name) const override;
    std::optional<TileSheet> tile_sheet(const std::string& name) const override;
    std::optional<std::vector<std::uint8_t>> demo() const override;
    std::optional<std::vector<std::uint8_t>> title_tune() const override;

private:
    std::optional<std::vector<std::uint8_t>> file(const std::string& name) const;
    std::filesystem::path dir_;
};

}  // namespace chr::formats
