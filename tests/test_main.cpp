#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>

#include "formats/import.h"
#include "formats/tables.h"
#include "game/text_provider.h"
#include "test.h"

namespace chrtest {

namespace {
int failures = 0;
}

std::vector<Test>& registry() {
    static std::vector<Test> tests;
    return tests;
}

void fail(const char* file, int line, const std::string& what) {
    ++failures;
    std::printf("  FAIL %s:%d: %s\n", file, line, what.c_str());
}

void skip(const std::string& why) { std::printf("  skipped: %s\n", why.c_str()); }

std::string game_dir() {
    std::string dir;
    if (const char* env = std::getenv("CHR_GAME_DIR"); env && *env)
        dir = env;
    else
        dir = CHR_DEFAULT_GAME_DIR;
    std::error_code ec;
    return std::filesystem::is_directory(dir, ec) ? dir : std::string();
}

std::string interface_text(const char* id) {
    static const auto table = chr::game::TextTable::load(CHR_INTERFACE_TEXT);
    if (const auto text = table.find(id)) return std::string(*text);
    fail(__FILE__, __LINE__, std::string(id) + " is not in interface.json");
    return "";
}

std::string shown(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == 0x02 || text[i] == 0x06) ++i;
        else if (text[i] != '\r') out += text[i];
    }
    return out;
}

std::string imported_data_dir() {
    static const std::string dir = [] {
        const auto game = game_dir();
        if (game.empty()) return std::string();
        const auto data = std::filesystem::path(CHR_TEST_SCRATCH) / "walk-data";
        chr::formats::ensure_imported(game, data);
        return data.string();
    }();
    return dir;
}

const chr::formats::Tables* tables() {
    static const auto loaded = []() -> std::unique_ptr<chr::formats::Tables> {
        const auto data = imported_data_dir();
        if (data.empty()) return nullptr;
        return std::make_unique<chr::formats::Tables>(chr::formats::read_tables(data));
    }();
    return loaded.get();
}

}  // namespace chrtest

int main() {
    for (const auto& test : chrtest::registry()) {
        std::printf("%s\n", test.name);
        try {
            test.body();
        } catch (const std::exception& e) {
            chrtest::fail(__FILE__, __LINE__, std::string("exception: ") + e.what());
        }
    }
    std::printf("%zu tests, %d failures\n", chrtest::registry().size(), chrtest::failures);
    return chrtest::failures == 0 ? 0 : 1;
}
