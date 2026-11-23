#include <vector>

#include "game/music.h"
#include "game/sound.h"
#include "platform/sound_wave.h"
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

TEST(sound_square_wave) {
    // 1 ms of 1000 Hz at 8000 samples a second: 4 high, 4 low; then 0.5 ms
    // of silence; then the wave goes on from where it was.
    const std::vector<SoundSegment> segments = {{1000, 1.0}, {0, 0.5}, {1000, 0.5}};
    const auto clip = chr::platform::square_wave(segments, 8000, 100);
    CHECK(clip.rate == 8000);
    CHECK(clip.samples.size() == 8 + 4 + 4);
    const std::vector<std::int16_t> want = {100, 100, 100, 100, -100, -100, -100, -100, 0, 0, 0, 0, 100, 100, 100, 100};
    CHECK(clip.samples == want);
    // Lengths that are not whole samples carry over: 16 pieces of a 16th
    // of a sample each make one sample.
    std::vector<SoundSegment> bits(16, SoundSegment{0, 1000.0 / 8000 / 16});
    CHECK(chr::platform::square_wave(bits, 8000, 100).samples.size() == 1);
}

namespace {
// A made-up pitch table: 1000 Hz up by 1 Hz a step.
constexpr std::array<int, 12> kNoteHz = {1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1008, 1009, 1010, 1011};
}  // namespace

TEST(music_steps_follow_the_bytes) {
    // A top-octave note, a rest, a lone 0 before an octave-7 note, then the
    // end mark: the byte after it is never read.
    const std::vector<std::uint8_t> tune = {8 * 12, 0x80, 0, 7 * 12 + 1, 0, 0, 5};
    const auto steps = tune_steps(tune, kNoteHz);
    CHECK(steps.size() == 3);
    CHECK(!steps[0].rest && steps[0].divisor == 1193182 / 1000);
    CHECK(steps[1].rest && steps[1].divisor == kMusicRestDivisor);
    CHECK(!steps[2].rest && steps[2].divisor == (1193182 / 1001) * 2);
}

TEST(music_ends_with_its_data) {
    // No end mark: the four 0 bytes 1000:476d adds end it.
    const std::vector<std::uint8_t> tune = {3 * 12 + 11};
    const auto steps = tune_steps(tune, kNoteHz);
    CHECK(steps.size() == 1 && steps[0].divisor == (1193182 / 1011) << 5);
    // An octave above 8 shifts the count out: 0, which the timer takes as 65536.
    const std::vector<std::uint8_t> high = {9 * 12, 10 * 12 + 2};
    const auto shifted = tune_steps(high, kNoteHz);
    CHECK(shifted.size() == 2 && shifted[0].divisor == 0x10000 && shifted[1].divisor == 0x10000);
}

TEST(music_steps_are_four_ticks) {
    CHECK(music_step_ms() > 13.727 && music_step_ms() < 13.729);
    const std::vector<std::uint8_t> tune = {8 * 12, 8 * 12, 0x90, 0x81, 8 * 12 + 1};
    const auto segments = music_segments(tune_steps(tune, kNoteHz));
    CHECK(segments.size() == 3);  // a repeated pitch or rest plays on
    CHECK(segments[0].ms > 27.45 && segments[0].ms < 27.46 && segments[1].hz == 0);
}
