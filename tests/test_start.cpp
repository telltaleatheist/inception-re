// What a start without a console tells the player (platform/sdl_setup.h). No box is shown: the
// tests put their own in its place.

#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "platform/sdl_setup.h"
#include "test.h"

using namespace chr;
namespace fs = std::filesystem;

namespace {

// The boxes shown while one of these is there.
struct Boxes {
    std::vector<std::string> shown;
    std::function<void(const std::string&)> before = platform::message_box();
    Boxes() {
        platform::message_box() = [this](const std::string& text) { shown.push_back(text); };
    }
    ~Boxes() { platform::message_box() = before; }
};

bool yes() { return true; }
bool no() { return false; }

}  // namespace

TEST(a_start_that_throws_is_told) {
    Boxes boxes;
    const auto throws = []() -> int { throw std::runtime_error("a name nothing can be made of"); };
    CHECK(platform::guarded([] { return 7; }, no, no) == 7 && boxes.shown.empty());
    CHECK(platform::guarded(throws, no, yes) == 1 && boxes.shown.empty());
    CHECK(platform::guarded(throws, no, no) == 1 && boxes.shown.size() == 1);
    CHECK(!boxes.shown.empty() && boxes.shown[0].find("a name nothing can be made of") != std::string::npos);
    CHECK(platform::guarded(throws, yes, no) == 2 && boxes.shown.size() == 2);
}

TEST(left_saves_are_told_until_there_are_some) {
    const fs::path base = fs::path(CHR_TEST_SCRATCH) / "left";
    fs::remove_all(base);
    const std::string right = (base / "Zo\xc3\xab" / "").string(), left = (base / "Zo\xc3\x83\xc2\xab" / "").string();
    const setup::PageText page = [&](const std::string& bytes) -> std::optional<std::string> {
        return bytes == right ? left : std::optional<std::string>();
    };
    fs::create_directories(right);
    fs::create_directories(left);
    Boxes boxes;
    platform::tell_left_saves(right, page, false);
    CHECK(boxes.shown.empty());
    std::ofstream(fs::path(left) / "Game2") << "x";
    platform::tell_left_saves(right, {}, false);
    platform::tell_left_saves(right, page, true);
    CHECK(boxes.shown.empty());
    platform::tell_left_saves(right, page, false);
    CHECK(boxes.shown.size() == 1 && boxes.shown[0].find(left) != std::string::npos &&
          boxes.shown[0].find(right) != std::string::npos);
    std::ofstream(fs::path(right) / "settings.ini") << "x";  // set up: on stderr alone
    platform::tell_left_saves(right, page, false);
    CHECK(boxes.shown.size() == 1);
    fs::remove(fs::path(right) / "settings.ini");
    std::ofstream(fs::path(right) / "Game1") << "x";
    platform::tell_left_saves(right, page, false);
    CHECK(boxes.shown.size() == 1);
    CHECK(fs::exists(fs::path(left) / "Game2"));
    fs::remove_all(base);
}
