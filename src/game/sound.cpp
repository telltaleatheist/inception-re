#include "game/sound.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace chr::game {

namespace {

constexpr int kKind = 0x3e8;  // codes above it are kinds 1-4; up to it, notes

// How many words an entry with this code takes. The code is compared
// signed, so a word of 0x8000 or more is a note.
std::size_t entry_size(int word) {
    const int code = std::int16_t(word);
    if (code <= kKind) return 3;
    switch (code - kKind) {
    case 1: return 6;
    case 2:
    case 3:
    case 4: return 7;
    default: return 1;
    }
}

// The lists' words from effect n's first entry, or -1 when there is no
// effect n (27c5:0002's walk).
struct Lists {
    std::span<const int> words;
    int word(std::size_t i) const { return i < words.size() ? words[i] & 0xffff : 0; }
    int s16(std::size_t i) const { return std::int16_t(word(i)); }
    bool ends(std::size_t i) const { return word(i + 1) == 0 && word(i + 2) == 0; }
    long first(int n) const {
        // Step over n - 1 effects: each entry followed by two 0 words ends one.
        int skip = n - 1;
        std::size_t i = 0;
        while (word(i) != 0) {
            if (skip == 0) break;
            if (ends(i)) --skip;
            i += entry_size(word(i));
        }
        return skip == 0 ? long(i) : -1;
    }
};

// A sweep that never reaches its end would play for ever in the original
// (16-bit wrap aside); the rebuild stops it here.
constexpr int kSweepLimit = 0x10000;

// BTECH.EXE 27c5:04f1 / 27c5:059a (Ghidra FUN_27c5_04f1, FUN_27c5_059a)
// A burst: the speaker off, a short wait, then `flips` flips (287f:007d),
// each followed by DG:5006 delays of 287f:00a9. The delay's count is
// (r & a) | b, r the value 287f:007d leaves behind from its seed; a count
// of 0 loops 65536 times.
SoundStep burst(int a, int b, int flips, int seed) {
    const std::uint16_t x = std::uint16_t(seed + 0x9248);
    const std::uint16_t r = std::uint16_t((x >> 3) | (x << 13));
    const int count = (r & a) | b;
    SoundStep s;
    s.kind = SoundStep::Kind::burst;
    s.flips = std::max(0, flips);
    s.count = (count & 0xffff) == 0 ? 0x10000 : count & 0xffff;
    return s;
}

}  // namespace

void play_entries(SoundState& state, std::span<const int> lists, int n) {
    const Lists l{lists};
    const long first = l.first(n);
    if (first < 0) return;
    for (std::size_t i = std::size_t(first); !l.ends(i); i += entry_size(l.word(i))) {
        if (l.word(i) == kKind + 2)
            for (std::size_t k = 0; k < state.sweep.size(); ++k) state.sweep[k] = std::uint16_t(l.word(i + 2 + k));
    }
}

std::vector<SoundStep> effect_steps(std::span<const int> lists, int n) {
    const Lists l{lists};
    std::vector<SoundStep> steps;
    const long first = l.first(n);
    if (first < 0) return steps;
    for (std::size_t i = std::size_t(first); !l.ends(i); i += entry_size(l.word(i))) {
        const int code = l.s16(i);
        if (code <= kKind) {
            // 27c5:02a3: the note `code` times (27c5:047b): its tone unless
            // the divisor is 0, then 50 waits of the length.
            for (int k = 0; k < code; ++k) {
                SoundStep s;
                s.kind = l.word(i + 1) != 0 ? SoundStep::Kind::tone : SoundStep::Kind::rest;
                s.divisor = l.word(i + 1);
                s.units = l.s16(i + 2);
                s.times = 50;
                steps.push_back(s);
            }
            continue;
        }
        if (code - kKind > 4) continue;  // any other code plays nothing
        const int count = l.s16(i + 1);
        for (int k = 0; k < count; ++k) {
            switch (code - kKind) {
            case 1:
                // 27c5:02eb: a burst with the entry's own words.
                steps.push_back(burst(l.word(i + 2), l.word(i + 3), l.s16(i + 4), l.word(i + 5)));
                break;
            case 2: {
                // 27c5:0345 / 0643: for each of `reps`, tones c - r + step x j
                // while step x j is below 2r (16-bit, signed), each held for
                // the length (27c5:06f6).
                const int c = l.word(i + 2), r = l.word(i + 3), units = l.s16(i + 4), reps = l.s16(i + 5),
                          step = l.word(i + 6);
                for (int rep = 0; rep < reps; ++rep) {
                    for (int j = 0; j < kSweepLimit; ++j) {
                        const int at = std::int16_t(step * j);
                        if (!(std::int16_t(r * 2) > at)) break;
                        const int d = (c - r + at) & 0xffff;
                        SoundStep s;
                        s.kind = SoundStep::Kind::tone;
                        s.divisor = d == 0 ? 0x10000 : d;
                        s.units = units;
                        steps.push_back(s);
                    }
                }
                break;
            }
            case 3:
            case 4: {
                // 27c5:03a8 / 0747 down, 27c5:040b / 07da up: a burst for
                // each v from w1 by w5 while past w2, with a = v, b = v - w3,
                // w4 flips and the seed 0x3e8 (27c5:04f1).
                const int w1 = l.word(i + 2), w2 = l.s16(i + 3), w3 = l.word(i + 4), flips = l.s16(i + 5),
                          w5 = l.word(i + 6);
                for (int j = 0; j < kSweepLimit; ++j) {
                    const int v = code - kKind == 3 ? std::int16_t(w1 - std::int16_t(w5 * j))
                                                    : std::int16_t(std::int16_t(w5 * j) + w1);
                    if (code - kKind == 3 ? !(w2 < v) : !(v < w2)) break;
                    steps.push_back(burst(v & 0xffff, (v - w3) & 0xffff, flips, 0x3e8));
                }
                break;
            }
            default:
                break;
            }
        }
    }
    return steps;
}

const SoundMachine& default_sound_machine() {
    // The 80286's cycle counts (docs/sound.md, "Lengths").
    static const SoundMachine kAt{"IBM PC AT, 8 MHz", 8e6, 3065, 57, 29, 151, 10, 184};
    return kAt;
}

int sound_calibration(const SoundMachine& m) {
    // Four ticks of the 1193182 Hz timer's 65536 counts, and the passes that
    // start in them.
    const double cycles = 4.0 * 65536.0 / 1193182.0 * m.clock_hz;
    const long passes = long(std::ceil(cycles / m.calibration_pass));
    const long speed = passes * 455 / 10000;  // F:3ff4
    return std::max(1L, (speed - 4) / 6);
}

std::vector<SoundSegment> sound_segments(std::span<const SoundStep> steps, const SoundMachine& m) {
    const int d = sound_calibration(m);
    const double ms_per_cycle = 1000.0 / m.clock_hz;
    std::vector<SoundSegment> out;
    const auto add = [&](double hz, double ms) {
        if (ms <= 0) return;
        if (!out.empty() && out.back().hz == hz) out.back().ms += ms;
        else out.push_back({hz, ms});
    };
    for (const auto& s : steps) {
        switch (s.kind) {
        case SoundStep::Kind::tone:
        case SoundStep::Kind::rest: {
            const long passes = long(s.times) * std::max(0, int(std::int16_t(s.units * d)));
            add(s.kind == SoundStep::Kind::tone ? 1193182.0 / s.divisor : 0.0,
                double(passes) * m.tone_pass * ms_per_cycle);
            break;
        }
        case SoundStep::Kind::burst: {
            add(0.0, double(d - 1) * m.short_pass * ms_per_cycle);
            const double half = m.flip + double(d) * (m.delay_call + double(m.delay_pass) * s.count);
            if (s.flips > 0) add(m.clock_hz / (2.0 * half), s.flips * half * ms_per_cycle);
            break;
        }
        }
    }
    return out;
}

}  // namespace chr::game
