// The sound effects as samples (docs/sound.md, "Playing it"). The logic
// gives an effect as square-wave tones and silences (game/sound.h); this
// turns them into 16-bit samples for the platform's audio stream, and a
// mod can give a file for any effect instead. No SDL types here.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "game/sound.h"

namespace chr::platform {

struct SoundClip {
    int rate = 0;  // samples per second, one channel
    std::vector<std::int16_t> samples;
};

// The segments at `rate` samples a second: a square wave of `amplitude`
// at each segment's Hz, silence where it is 0. The wave keeps its phase
// from one segment to the next, as the speaker's one output does.
SoundClip square_wave(std::span<const game::SoundSegment> segments, int rate, int amplitude);

// Where an effect's sound comes from: the original's (worked out from the
// lists) unless a provider has another for it.
class SoundProvider {
public:
    virtual ~SoundProvider() = default;
    // Effect n's sound, or nothing if this provider doesn't have it.
    virtual std::optional<SoundClip> effect(int n) = 0;
    // The tune `name` ("title"), or nothing if this provider doesn't have it.
    virtual std::optional<SoundClip> music(std::string_view name) { (void)name; return std::nullopt; }
};

}  // namespace chr::platform
