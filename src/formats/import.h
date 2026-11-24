// The importer: reads the player's own copy's program once (checked as the version it is)
// and writes the data the game needs from it as plain files in the player's
// data folder, never in the repo:
//
//   import.json          what was imported, by which import version
//   text/english.json    every text: id, its address in the program (or a
//                        .BLD file), text
//   font/font.txt        the font (formats/font_file.h)
//   rules/*.json         the rule tables (formats/rules.h)
//   tables.json          the small tables: screen, town, fight (formats/tables.h)
//
// From then on the game reads only those files; the program is needed only
// to import. Overrides (a translation, a mod) are more text files of the same
// form, layered on top (game/text_provider.h).
#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "formats/exe_data.h"
#include "formats/game_source.h"

namespace chr::formats {

// The shape of the imported files (docs/import.md has each version). A data
// folder from another version, or imported with another import table
// (import_table_hash), is imported again.
inline constexpr int kImportVersion = 33;

std::filesystem::path imported_text_path(const std::filesystem::path& data_dir);
std::filesystem::path imported_font_path(const std::filesystem::path& data_dir);

// A hash of the import table (OriginalExe::table()): every piece's kind,
// ID, address, size and expected hash. import.json records it, and a data
// folder imported with another table is imported again, so a piece added
// to the table can't be missed by an older folder.
std::string import_table_hash(std::span<const ExeSpan> table);
std::string import_table_hash();

enum class ImportState { Current, Missing, Outdated };
// With `program` (GameSource::program_name), an import from another version's copy is outdated:
// the colours and the rest follow the copy played.
ImportState import_state(const std::filesystem::path& data_dir, std::string_view program = {});

// Imports from the player's copy into `data_dir`: the texts and tables from its program
// (GameSource::program), the places' texts from its scripts. Throws FormatError (a missing or
// unknown program) or std::filesystem errors.
void import_original(const GameSource& source, const std::filesystem::path& data_dir);
// The same from a copy on disk (formats/open_source.h).
void import_original(const std::filesystem::path& copy, const std::filesystem::path& data_dir);

// Imports if the data folder has no import, an older one or another version's; true if it did.
// If that is needed and impossible, throws FormatError saying how to import.
bool ensure_imported(const GameSource& source, const std::filesystem::path& data_dir);
bool ensure_imported(const std::filesystem::path& copy, const std::filesystem::path& data_dir);

}  // namespace chr::formats
