// The sound effects as samples (docs/sound.md, "Playing it"), by the number
// the logic gives: the speaker's tones and silences turned into 16-bit
// samples, or the files of the player's copy or of a mod. No SDL types here.
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

// Where an effect's sound comes from.
class SoundProvider {
public:
    virtual ~SoundProvider() = default;
    // Effect n's sound, or nothing if this provider doesn't have it.
    virtual std::optional<SoundClip> effect(int n) = 0;
    // The tune `name` ("title"), or nothing if this provider doesn't have it.
    virtual std::optional<SoundClip> music(std::string_view name) { (void)name; return std::nullopt; }
};

// DOS's speaker: the effects from the lists (DG:5008), one at a time.
class SpeakerSounds : public SoundProvider {
public:
    explicit SpeakerSounds(std::span<const int> lists) : lists_(lists.begin(), lists.end()) {}
    std::optional<SoundClip> effect(int n) override;

private:
    std::vector<int> lists_;
};

// The speaker's square wave at a quarter of full scale.
inline constexpr int kSpeakerAmplitude = 8192;
// The speaker's samples a second.
inline constexpr int kSpeakerRate = 44100;

}  // namespace chr::platform
