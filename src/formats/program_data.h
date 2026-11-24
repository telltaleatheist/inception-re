// The pieces the importer takes from a copy's program, by the IDs of the DOS 1.03 import table
// (formats/exe_data.h): the texts and the tables, each in DOS's form whichever version the copy
// is. OriginalExe reads BTECH.EXE; another version's reader turns its own program's pieces into
// the same forms (docs/versions.md, "One source by ID").
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace chr::formats {

class ProgramData {
public:
    virtual ~ProgramData() = default;

    // The bytes of the named piece, as DOS 1.03's. Throws FormatError for a piece this copy
    // can't give.
    virtual std::span<const std::uint8_t> get(std::string_view name) const = 0;
    // A named text, read the way DOS's printer reads it (OriginalExe::text_of).
    virtual std::string text(std::string_view name) const = 0;
    // Where the named piece is in this copy's program, for the imported files.
    virtual std::string address(std::string_view name) const = 0;
    // What the copy is ("BTECH.EXE DOS 1.03"), and its program's SHA-256, for the manifest.
    virtual std::string source() const = 0;
    virtual std::string sha256() const = 0;
};

}  // namespace chr::formats
