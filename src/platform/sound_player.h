// The sound effects played through SDL3's audio (docs/sound.md, "Playing
// it"): each effect is queued on one audio stream and plays while the game
// waits out its length. Only an interactive run opens it: a run with a
// script or a screenshot (the rig, the tests) never opens an audio device.
#pragma once

#include <filesystem>
#include <memory>
#include <span>

#include "game/sound.h"
#include "platform/sound_wave.h"

struct SDL_AudioStream;

namespace chr::platform {

// The player's samples a second, one channel; the stream converts to the
// device's own.
constexpr int kSoundRate = 44100;

// A mod's sounds: <folder>/<n>.wav for effect n, any rate, mono or stereo
// (converted to the player's format).
class WavSounds : public SoundProvider {
public:
    explicit WavSounds(std::filesystem::path folder) : folder_(std::move(folder)) {}
    std::optional<SoundClip> effect(int n) override;

private:
    std::filesystem::path folder_;
};

class SoundPlayer {
public:
    // Opens the default playback device; `mods` may be null.
    explicit SoundPlayer(SoundProvider* mods);
    ~SoundPlayer();
    SoundPlayer(const SoundPlayer&) = delete;
    SoundPlayer& operator=(const SoundPlayer&) = delete;
    bool ok() const { return stream_ != nullptr; }
    // Effect n: the mod's sound if it has one, else the segments.
    void play(int n, std::span<const game::SoundSegment> segments);

private:
    SDL_AudioStream* stream_ = nullptr;
    SoundProvider* mods_ = nullptr;
};

}  // namespace chr::platform
