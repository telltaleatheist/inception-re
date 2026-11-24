#include "game/music.h"

namespace chr::game {

namespace {
constexpr int kTimerHz = 1193182;
}  // namespace

std::vector<MusicStep> tune_steps(std::span<const std::uint8_t> tune, std::span<const int, 12> note_hz) {
    std::vector<MusicStep> steps;
    std::size_t i = 0;
    const auto next = [&]() -> int { return i < tune.size() ? tune[i++] : 0; };
    for (;;) {
        int b = next();
        if (b == 0) {
            b = next();
            if (b == 0) break;
        }
        MusicStep s;
        if (b & 0x80) {
            s.rest = true;
            s.divisor = kMusicRestDivisor;
        } else {
            // 284b:0139: the timer's count for the top octave's pitch
            // (284b:00cf), shifted left by 8 less the octave. The shift's
            // count is a byte, and the processor uses its low 5 bits: an
            // octave above 8 leaves 0, which the timer takes as 65536.
            const int octave = b / 12;
            const unsigned top = unsigned(kTimerHz / note_hz[std::size_t(b % 12)]) & 0xffff;
            const unsigned shift = unsigned(8 - octave) & 0x1f;
            const unsigned d = (top << shift) & 0xffff;
            s.divisor = d == 0 ? 0x10000 : int(d);
        }
        steps.push_back(s);
    }
    return steps;
}

int title_wait_ticks(std::size_t steps) { return 1 + kMusicTicksPerStep * int(steps); }

double title_wait_ms(std::size_t steps) { return 1000.0 * title_wait_ticks(steps) * kMusicTimerCount / kTimerHz; }

double music_step_ms() {
    return 1000.0 * kMusicTicksPerStep * kMusicTimerCount / kTimerHz;
}

std::vector<SoundSegment> music_segments(std::span<const MusicStep> steps) {
    std::vector<SoundSegment> out;
    const double ms = music_step_ms();
    for (const auto& s : steps) {
        const double hz = s.rest ? 0.0 : double(kTimerHz) / s.divisor;
        if (!out.empty() && out.back().hz == hz) out.back().ms += ms;
        else out.push_back({hz, ms});
    }
    return out;
}

}  // namespace chr::game
