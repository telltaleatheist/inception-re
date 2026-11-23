// The title's tune (docs/sound.md, "The title music"). The logic works out
// what the speaker plays, step by step, in plain terms: a pitch or a rest,
// each the same length. The platform plays it while the title waits.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "game/sound.h"

namespace chr::game {

// The timer's count while the tune plays (284b:0048): it ticks 1193182 /
// 4095 times a second.
inline constexpr int kMusicTimerCount = 0x0fff;
// One step every 4 ticks on the speaker (1000:476d passes the 4 to 284b:0233).
inline constexpr int kMusicTicksPerStep = 4;
// What a rest plays (284b:0187): this divisor, far above hearing.
inline constexpr int kMusicRestDivisor = 14;

// One step: the timer's divisor for its pitch, or a rest.
struct MusicStep {
    bool rest = false;
    int divisor = 0;  // 1..65536
};

// BTECH.EXE 284b:0298, 284b:0139, 284b:0187 (not functions in Ghidra)
// The speaker's steps from the tune file's bytes, up to its end mark (two 0
// bytes; the end of the data counts as one). A lone 0 is passed over, and
// the byte after it plays in the same step. A byte of 0x80 or more is a
// rest; any other is a note: its octave the byte / 12, its pitch the byte
// % 12, read in `note_hz` (the imported 284b:0121).
std::vector<MusicStep> tune_steps(std::span<const std::uint8_t> tune, std::span<const int, 12> note_hz);

// How long a step lasts, in ms.
double music_step_ms();

// The steps as heard: each step's tone, or silence for a rest.
std::vector<SoundSegment> music_segments(std::span<const MusicStep> steps);

}  // namespace chr::game
