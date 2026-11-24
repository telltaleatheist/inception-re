// The Amiga 2.3 copy as a GameSource (formats/game_source.h, docs/versions.md "One source by
// ID"): each piece read from the Amiga's own files and laid out in DOS 1.03's working form, with
// the Amiga's rule differences always put back to DOS's (docs/versions.md, "Amiga rules"). The
// pictures keep the Amiga's colour numbers.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "formats/amiga_program.h"
#include "formats/game_source.h"
#include "formats/tables.h"

namespace chr::formats {

// The demo's keys in DOS's codes: the Amiga's DEMOFILE records the movement keys as the
// number pad's digits where DOS has its own codes, and is otherwise DOS's byte for byte.
std::vector<std::uint8_t> amiga_demo_keys(std::span<const std::uint8_t> demofile);

class AmigaGameSource final : public GameSource {
public:
    // The copy's files by name (any case), e.g. from its disk's Battletech folder, and where
    // they came from (for messages). Reads and checks the program `battletech` at once.
    AmigaGameSource(std::map<std::string, std::vector<std::uint8_t>> files, std::string where);
    // The files of a folder holding an Amiga copy's Battletech files.
    static AmigaGameSource from_folder(const std::filesystem::path& dir);

    std::string describe() const override { return where_; }
    std::optional<std::vector<std::uint8_t>> script(int file) const override;
    std::optional<std::vector<std::uint8_t>> map(int number) const override;
    std::optional<AnmPicture> place_picture(int number) const override;
    std::optional<CmpPicture> picture(const std::string& name) const override;
    std::optional<TileSheet> tile_sheet(const std::string& name) const override;
    std::optional<std::vector<std::uint8_t>> demo() const override;
    // The Amiga has no speaker tune; its music is among its sound samples.
    std::optional<std::vector<std::uint8_t>> title_tune() const override { return std::nullopt; }
    std::unique_ptr<const ProgramData> program() const override;

private:
    const std::vector<std::uint8_t>* file(const std::string& name) const;

    std::map<std::string, std::vector<std::uint8_t>> files_;  // by lower-case name
    std::string where_;
    AmigaProgram program_;
    std::array<MenuRecord, kMenuCount> menus_;
};

}  // namespace chr::formats
