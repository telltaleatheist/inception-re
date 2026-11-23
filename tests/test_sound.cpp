#include <vector>

#include "game/sound.h"
#include "test.h"

using namespace chr::game;

namespace {

// A made-up list in the original's shape: effect 1 is a note then a kind-2
// entry; effect 2 is two kind-2 entries; effect 3 a kind-3 entry and a
// code past the kinds. Each ends with an entry followed by two 0 words.
const std::vector<int> kLists = {
    500, 1, 2,                          // effect 1: a note
    0x3ea, 2, 10, 11, 12, 13, 14,       //   kind 2
    0x3e9, 0, 0, 1, 1, 1,               //   kind 1, followed by two 0 words: the end
    0x3ea, 1, 20, 21, 22, 23, 24,       // effect 2: kind 2
    0x3ea, 1, 30, 31, 32, 33, 34,       //   kind 2, the last
    500, 0, 0,                          //   the end
    0x3eb, 1, 9, 9, 9, 9, 9,            // effect 3: kind 3
    0x3f0,                              //   a code past the kinds: one word
    500, 0, 0,                          //   the end
    0,
};

}  // namespace

TEST(sound_keeps_the_last_kind_two_words) {
    SoundState s;
    CHECK(s.minus_one_x() == 0 && s.minus_one_y() == 0);  // the image's values
    play_entries(s, kLists, 1);
    CHECK(s.sweep[0] == 10 && s.sweep[3] == 13 && s.sweep[4] == 14);
    play_entries(s, kLists, 2);
    CHECK(s.sweep[0] == 30 && s.minus_one_y() == 33);
    play_entries(s, kLists, 3);  // no kind-2 entry: kept
    CHECK(s.minus_one_y() == 33);
    CHECK(s.minus_one_x() == 0);
}

TEST(sound_past_the_last_effect_plays_nothing) {
    SoundState s;
    play_entries(s, kLists, 4);
    play_entries(s, kLists, 40);
    CHECK(s.minus_one_y() == 0);
    // An entry whose next two words are 0 ends the effect before it plays.
    const std::vector<int> ends_first = {0x3ea, 0, 0, 5, 5, 5, 5, 0};
    play_entries(s, ends_first, 1);
    CHECK(s.minus_one_y() == 0);
}

TEST(sound_codes_are_compared_signed) {
    // 0x8000 counts as a note (3 words), so the kind-2 entry after it is
    // reached as effect 1's second entry.
    const std::vector<int> lists = {0x8000, 1, 1, 0x3ea, 1, 1, 2, 3, 77, 5, 500, 0, 0, 0};
    SoundState s;
    play_entries(s, lists, 1);
    CHECK(s.minus_one_y() == 77);
}

TEST(sound_calibration_on_the_default_machine) {
    // docs/sound.md, "Lengths": 574 passes of the speed test in four ticks,
    // 26, then (26 - 4) / 6.
    CHECK(sound_calibration(default_sound_machine()) == 3);
    // A slow machine gives at least 1.
    SoundMachine slow = default_sound_machine();
    slow.clock_hz = 1e6;
    CHECK(sound_calibration(slow) == 1);
}

TEST(sound_steps_of_each_kind) {
    // Made-up entries: a note, a rest, a kind-2 sweep, a kind-3 sweep down, a
    // kind-4 sweep up and a kind-1 burst whose delay count is 0.
    const std::vector<int> lists = {
        2, 1000, 4,                       // a note twice
        1, 0, 4,                          // a rest
        0x3ea, 1, 1000, 10, 7, 1, 5,      // tones 990, 995, 1000, 1005
        0x3eb, 1, 100, 90, 1, 2, 5,       // bursts at v = 100, 95
        0x3ec, 1, 10, 20, 1, 3, 6,        // bursts at v = 10, 16
        0x3e9, 1, 0, 0, 4, 0x3e8,         // a = b = 0: the count wraps to 65536
        500, 0, 0,                        // the end
        0,
    };
    const auto steps = effect_steps(lists, 1);
    CHECK(steps.size() == 2 + 1 + 4 + 2 + 2 + 1);
    using Kind = SoundStep::Kind;
    CHECK(steps[0].kind == Kind::tone && steps[0].divisor == 1000 && steps[0].units == 4 && steps[0].times == 50);
    CHECK(steps[2].kind == Kind::rest && steps[2].times == 50);
    CHECK(steps[3].divisor == 990 && steps[6].divisor == 1005 && steps[6].times == 1 && steps[6].units == 7);
    // 287f:007d's value from the seed 0x3e8 is 0x12c6; the count is
    // (0x12c6 & v) | (v - w3).
    CHECK(steps[7].kind == Kind::burst && steps[7].flips == 2 && steps[7].count == ((0x12c6 & 100) | 99));
    CHECK(steps[8].count == ((0x12c6 & 95) | 94));
    CHECK(steps[9].count == ((0x12c6 & 10) | 9) && steps[10].count == ((0x12c6 & 16) | 15) && steps[10].flips == 3);
    CHECK(steps[11].count == 0x10000 && steps[11].flips == 4);
    CHECK(effect_steps(lists, 2).empty());
}

TEST(sound_segments_on_the_default_machine) {
    const auto& m = default_sound_machine();
    const std::vector<SoundStep> steps = {
        {SoundStep::Kind::tone, 990, 7, 1, 0, 0},
        {SoundStep::Kind::burst, 0, 0, 1, 5, 100},
    };
    const auto s = sound_segments(steps, m);
    const auto near = [](double a, double b) { return a > b - 1e-9 && a < b + 1e-9; };
    // The tone: 7 x 3 passes of 57 cycles at 8 MHz.
    CHECK(s.size() == 3);
    CHECK(near(s[0].hz, 1193182.0 / 990) && near(s[0].ms, 7 * 3 * 57 / 8000.0));
    // The burst: the short wait (2 passes of 29) in silence, then 5 flips,
    // each 184 + 3 x (151 + 10 x 100) cycles.
    CHECK(s[1].hz == 0 && near(s[1].ms, 2 * 29 / 8000.0));
    const double half = 184 + 3 * (151 + 10 * 100);
    CHECK(near(s[2].hz, 8e6 / (2 * half)) && near(s[2].ms, 5 * half / 8000.0));
}
