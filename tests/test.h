// A very small test harness: CHECK records a failure and carries on.
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace chr::formats {
struct Tables;
}

namespace chrtest {

struct Test {
    const char* name;
    std::function<void()> body;
};

std::vector<Test>& registry();
void fail(const char* file, int line, const std::string& what);
// How many CHECKs have run, and how many skip()s were said.
inline long checks = 0, skips = 0;
// The game folder, or empty if it isn't there: tests that need it skip.
std::string game_dir();
void skip(const std::string& why);
// The player's data imported once into the build tree (empty without the
// game folder), and the small tables read from it (nullptr without it):
// the repo holds no values, so tests that need them skip.
std::string imported_data_dir();
const chr::formats::Tables* tables();
// An interface text by ID, from the repo's data/text/interface.json ("" and a failure when it isn't there).
std::string interface_text(const char* id);
// A text as the printer shows it: its colour codes (0x02 n, 0x06 n) and line breaks left out.
std::string shown(const std::string& text);

struct Register {
    Register(const char* name, std::function<void()> body) {
        registry().push_back({name, std::move(body)});
    }
};

}  // namespace chrtest

#define CHR_CAT2(a, b) a##b
#define CHR_CAT(a, b) CHR_CAT2(a, b)
#define TEST(name)                                                             \
    static void name();                                                        \
    static chrtest::Register CHR_CAT(reg_, name)(#name, name);                 \
    static void name()
#define CHECK(cond)                                                            \
    do {                                                                       \
        ++chrtest::checks;                                                     \
        if (!(cond)) chrtest::fail(__FILE__, __LINE__, #cond);                 \
    } while (0)
