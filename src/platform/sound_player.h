// The sound effects played through SDL3's audio (docs/sound.md, "Playing
// it") while the game waits out their lengths. Only an interactive run opens
// it: a run with a script or a screenshot (the rig, the tests) never opens an
// audio device.
#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <span>

#include "game/sound.h"
#include "platform/sound_wave.h"

struct SDL_AudioStream;

namespace chr::platform {

// The player's samples a second, one channel; the stream converts to the
// device's own.
constexpr int kSoundRate = kSpeakerRate;

// How many recorded sounds can be heard at once, as on the Amiga's channels
// (0:272a2).
constexpr int kSoundVoices = 4;

// Recorded sounds, the imported copy's or a mod's: <folder>/<n>.wav for effect n and <folder>/<name>.wav
// for a tune (title.wav), any rate, mono or stereo (converted to the
// player's format).
class WavSounds : public SoundProvider {
public:
    explicit WavSounds(std::filesystem::path folder) : folder_(std::move(folder)) {}
    std::optional<SoundClip> effect(int n) override;
    std::optional<SoundClip> music(std::string_view name) override;

private:
    std::optional<SoundClip> load(const std::filesystem::path& path);
    std::filesystem::path folder_;
};

class SoundPlayer {
public:
    // Opens the default playback device. An effect is heard from `mods`, else
    // `copy` (either may be null), else the speaker.
    SoundPlayer(SoundProvider* mods, SoundProvider* copy, SoundProvider& speaker);
    ~SoundPlayer();
    SoundPlayer(const SoundPlayer&) = delete;
    SoundPlayer& operator=(const SoundPlayer&) = delete;
    bool ok() const { return stream_ != nullptr; }
    // Effect n. Recorded sounds play over each other, each on the next voice
    // in turn, cutting off what that voice still plays; the speaker plays one
    // after another.
    void play(int n);
    // The title's tune on a stream of its own, from the start: the mod's
    // title.wav if it has one, else the segments. stop_music() cuts it off.
    void play_music(std::span<const game::SoundSegment> segments);
    void stop_music();

private:
    SDL_AudioStream* open();

    SDL_AudioStream* stream_ = nullptr;  // the speaker
    std::array<SDL_AudioStream*, kSoundVoices> voices_{};
    std::size_t next_voice_ = 0;
    SDL_AudioStream* music_ = nullptr;
    SoundProvider* mods_ = nullptr;
    SoundProvider* copy_ = nullptr;
    SoundProvider* speaker_ = nullptr;
};

}  // namespace chr::platform
