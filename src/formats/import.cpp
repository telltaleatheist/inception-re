#include "formats/import.h"

#include <cstdio>

#include "formats/sha256.h"
#include "formats/tables.h"

#include <cstdio>
#include <fstream>
#include <random>

#include "formats/bld.h"
#include "formats/cmp.h"  // FormatError
#include "formats/exe_data.h"
#include "formats/font_file.h"
#include "formats/game_files.h"
#include "formats/json.h"
#include "formats/rules.h"

namespace chr::formats {

namespace fs = std::filesystem;

namespace {

fs::path manifest_path(const fs::path& data_dir) { return data_dir / "import.json"; }

// Each writer has its own temporary name, so runs importing into the same
// folder at once (the reference check's parallel cases) each rename a whole
// file into place.
void write_file(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    char tag[24];
    std::snprintf(tag, sizeof tag, ".%08x.part", static_cast<unsigned>(std::random_device{}()));
    const fs::path temp = path.string() + tag;
    {
        std::ofstream out(temp, std::ios::binary);
        out << contents;
        if (!out) throw FormatError("can't write " + temp.string());
    }
    fs::rename(temp, path);
}

}  // namespace

fs::path imported_text_path(const fs::path& data_dir) { return data_dir / "text" / "english.json"; }
fs::path imported_font_path(const fs::path& data_dir) { return data_dir / "font" / "font.txt"; }

std::string import_table_hash(std::span<const ExeSpan> table) {
    std::string all;
    for (const auto& span : table) {
        char line[96];
        std::snprintf(line, sizeof line, "%d %04x:%04x %u ", static_cast<int>(span.kind), span.segment,
                      span.offset, static_cast<unsigned>(span.size));
        all += line;
        all += span.name;
        all += ' ';
        all += span.sha256;
        all += '\n';
    }
    return sha256_hex({reinterpret_cast<const std::uint8_t*>(all.data()), all.size()});
}

std::string import_table_hash() { return import_table_hash(OriginalExe::table()); }

ImportState import_state(const fs::path& data_dir) {
    std::error_code ec;
    if (!fs::exists(manifest_path(data_dir), ec) || !fs::exists(imported_text_path(data_dir), ec) ||
        !fs::exists(imported_font_path(data_dir), ec))
        return ImportState::Missing;
    for (const auto name : kRulesFiles)
        if (!fs::exists(rules_dir(data_dir) / name, ec)) return ImportState::Missing;
    if (!fs::exists(tables_path(data_dir), ec)) return ImportState::Missing;
    try {
        const auto bytes = read_file(manifest_path(data_dir));
        const auto manifest = parse_json(std::string(bytes.begin(), bytes.end()));
        const auto* version = manifest.find("import_version");
        if (!version || version->number() != kImportVersion) return ImportState::Outdated;
        const auto* table = manifest.find("table_sha256");
        if (!table || table->string() != import_table_hash()) return ImportState::Outdated;
    } catch (const std::exception&) {
        return ImportState::Outdated;
    }
    return ImportState::Current;
}

void import_original(const GameSource& source, const fs::path& data_dir) {
    const auto program = source.program();
    const ProgramData& exe = *program;

    std::string text = "{\n  \"import_version\": " + std::to_string(kImportVersion) +
                       ",\n  \"source\": " + json_quote(exe.source()) + ",\n  \"texts\": [\n";
    bool first = true;
    for (const auto& span : OriginalExe::table()) {
        if (span.kind != ExeSpan::Kind::Text) continue;
        if (!first) text += ",\n";
        first = false;
        text += "    {\"id\": " + json_quote(span.name) + ", \"address\": " +
                json_quote(exe.address(span.name)) + ", \"text\": " + json_quote(exe.text(span.name)) + "}";
    }
    // The names of the 'Mech templates and the weapons, from the rule tables.
    const auto rules = rules_from_original(exe);
    for (const auto& named : rules_texts_from_original(exe))
        text += ",\n    {\"id\": " + json_quote(named.id) + ", \"address\": " + json_quote(named.address) +
                ", \"text\": " + json_quote(named.bytes) + "}";
    // The places' texts, from the player's .BLD files: every text a script
    // can print, found by walking the script from its start.
    const auto tables = tables_from_original(exe);
    for (std::size_t f = 0; f < kBldFiles.size(); ++f) {
        const auto file = kBldFiles[f];
        const std::string name = std::string(file) + ".BLD";
        const auto script = source.script(int(f));
        if (!script) throw FormatError(name + " is missing from " + source.describe());
        for (const auto& [at, in] : walk(*script, tables.menus)) {
            if (in.op != Op::Print) continue;
            char address[16];
            std::snprintf(address, sizeof address, "+%04x", at + 1);
            text += ",\n    {\"id\": " + json_quote(bld_text_id(file, at)) + ", \"address\": " +
                    json_quote(name + address) + ", \"text\": " + json_quote(in.text) + "}";
        }
    }
    text += "\n  ]\n}\n";

    // The manifest goes last: a folder without one has no finished import.
    std::error_code ec;
    fs::remove(manifest_path(data_dir), ec);
    write_file(imported_text_path(data_dir), text);
    write_file(imported_font_path(data_dir), write_font_file(font_from_original(exe.get("font"))));
    const auto files = write_rules_files(rules, kImportVersion);
    write_file(rules_dir(data_dir) / kRulesFiles[0], files.mechs);
    write_file(rules_dir(data_dir) / kRulesFiles[1], files.weapons);
    write_file(rules_dir(data_dir) / kRulesFiles[2], files.combat);
    write_file(rules_dir(data_dir) / kRulesFiles[3], files.quiz);
    write_file(rules_dir(data_dir) / kRulesFiles[4], files.shops);
    write_file(tables_path(data_dir), write_tables_file(tables, kImportVersion));
    write_file(manifest_path(data_dir),
               "{\n  \"import_version\": " + std::to_string(kImportVersion) + ",\n  \"source\": " +
                   json_quote(exe.source()) + ",\n  \"exe_sha256\": " +
                   json_quote(exe.sha256()) + ",\n  \"table_sha256\": " + json_quote(import_table_hash()) +
                   "\n}\n");
}

void import_original(const fs::path& game_dir, const fs::path& data_dir) {
    import_original(DosGameSource(game_dir), data_dir);
}

bool ensure_imported(const fs::path& game_dir, const fs::path& data_dir) {
    const auto state = import_state(data_dir);
    if (state == ImportState::Current) return false;
    try {
        import_original(game_dir, data_dir);
    } catch (const std::exception& e) {
        throw FormatError(std::string("the game's text, font and rule tables have ") +
                          (state == ImportState::Missing ? "not been imported" : "an older import") +
                          " in " + data_dir.string() + ", and importing them failed: " + e.what() +
                          ". Run `chr --import --game-dir DIR` with your copy of the game (DOS " +
                          std::string(kSupportedVersion.substr(4)) + ").");
    }
    return true;
}

}  // namespace chr::formats
