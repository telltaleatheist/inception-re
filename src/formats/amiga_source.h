// The Amiga 2.3 copy as a GameSource (formats/game_source.h, docs/versions.md "One source by
// ID"): each piece read from the Amiga's own files and laid out in DOS 1.03's working form, with
// the Amiga's rule differences always put back to DOS's (docs/versions.md, "Amiga rules") and
// its colour numbers turned into DOS's (formats/amiga_colours.h).
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

// Sound n (1-19) of the sound file `snd`, whose offsets and periods are the program's
// (docs/formats/amiga.md, "The sound file"). Throws FormatError for one that isn't in the file.
SoundSample amiga_sound(std::span<const std::uint8_t> snd, std::span<const std::uint8_t> offsets,
                        std::span<const std::uint8_t> periods, int n);

class AmigaGameSource final : public GameSource {
public:
    // The copy's files by name (any case), e.g. from its disk's Battletech folder, and where
    // they came from (for messages). Reads and checks the program `battletech` at once.
    AmigaGameSource(std::map<std::string, std::vector<std::uint8_t>> files, std::string where);
    // The files of a folder holding an Amiga copy's Battletech files.
    static AmigaGameSource from_folder(const std::filesystem::path& dir);

    std::string describe() const override { return where_; }
    std::string program_name() const override;
    std::optional<std::vector<std::uint8_t>> script(int file) const override;
    std::optional<std::vector<std::uint8_t>> map(int number) const override;
    std::optional<AnmPicture> place_picture(int number) const override;
    std::optional<CmpPicture> picture(const std::string& name) const override;
    std::optional<TileSheet> tile_sheet(const std::string& name) const override;
    std::optional<Palette16> picture_palette(const std::string& name) const override;
    std::optional<std::vector<std::uint8_t>> demo() const override;
    std::optional<SoundSample> effect_sound(int n) const override;
    // The Amiga has no title tune (docs/formats/amiga.md, "The sound file").
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
