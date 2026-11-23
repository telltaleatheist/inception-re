// The sound effects (docs/sound.md). The logic names an effect by number
// and works out what it sounds like in plain terms: square-wave tones in Hz
// and silences, each with a length in ms on a chosen machine. The platform
// plays them. It also keeps the words the original's player stores in the
// far data, because the game reads one back.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace chr::game {

struct SoundState {
    bool on = true;  // DG:015c

    // The last kind-2 entry's five words: F:0000, F:39f4, F:4000, F:4034,
    // F:0062 (0 in the program image).
    std::array<std::uint16_t, 5> sweep{};

    // Figure -1's position (F:4002, F:4034): x is never written.
    std::uint16_t minus_one_x() const { return 0; }
    std::uint16_t minus_one_y() const { return sweep[3]; }
};

// BTECH.EXE 27c5:0002 (Ghidra FUN_27c5_0002)
// Effect n's entries from the lists (DG:5008), with what they store: here
// only the kind-2 words.
void play_entries(SoundState& state, std::span<const int> lists, int n);

// One step of an effect in the original's own terms (docs/sound.md, "What
// an effect plays"). The schedule below is worked out from these, and the
// rig compares them with the original's speaker; nothing else uses them.
struct SoundStep {
    enum class Kind { tone, rest, burst };
    Kind kind = Kind::rest;
    // tone: the timer's count for the pitch, 1..65536.
    int divisor = 0;
    // tone, rest: the wait, in passes of the wait loop per DG:5006 (it
    // waits int16(units x DG:5006) passes, none when that is not above 0).
    int units = 0;
    // tone, rest: how many times that wait is made (50 for a note).
    int times = 1;
    // burst: how many times the speaker flips, and the delay loop's count
    // in each of the DG:5006 calls after a flip (1..65536).
    int flips = 0;
    int count = 0;
};

// BTECH.EXE 27c5:0002 and the routines it runs (27c5:02a3, 02eb, 0345,
// 03a8, 040b; Ghidra FUN_27c5_*)
// Effect n's steps, in the order the speaker plays them. Empty when there is
// no effect n.
std::vector<SoundStep> effect_steps(std::span<const int> lists, int n);

// A machine the lengths are worked out for: its clock and what each of the
// player's loops costs on it, in clock cycles (docs/sound.md, "Lengths").
struct SoundMachine {
    std::string_view name;
    double clock_hz;
    int calibration_pass;  // 15ab:0b95: one pass of the speed test
    int tone_pass;         // 27c5:047b / 06f6: one pass of the tone wait
    int short_pass;        // 27c5:04f1 / 059a: one pass of the wait before a burst
    int delay_call;        // one call of 287f:00a9, its loop aside
    int delay_pass;        // one pass of 287f:00a9's loop
    int flip;              // the rest of a burst's step: the flip and the loop around it
};

// The default: an IBM PC AT at 8 MHz.
const SoundMachine& default_sound_machine();

// BTECH.EXE 15ab:0b95, 15ab:0c8f (Ghidra FUN_15ab_0b95, FUN_15ab_0c8f)
// DG:5006 on that machine: the speed test's passes in four timer ticks,
// x 455 / 10000, less 4, / 6, at least 1.
int sound_calibration(const SoundMachine& machine);

// A stretch of an effect: a square wave at `hz`, or silence when hz is 0.
struct SoundSegment {
    double hz = 0;
    double ms = 0;
};

// The steps as heard on `machine`.
std::vector<SoundSegment> sound_segments(std::span<const SoundStep> steps, const SoundMachine& machine);

}  // namespace chr::game
