#include "platform/sound_player.h"

#include <SDL3/SDL.h>

namespace chr::platform {

std::optional<SoundClip> WavSounds::effect(int n) { return load(folder_ / (std::to_string(n) + ".wav")); }

std::optional<SoundClip> WavSounds::music(std::string_view name) {
    return load(folder_ / (std::string(name) + ".wav"));
}

std::optional<SoundClip> WavSounds::load(const std::filesystem::path& path) {
    SDL_AudioSpec spec{};
    Uint8* data = nullptr;
    Uint32 bytes = 0;
    if (!SDL_LoadWAV(path.string().c_str(), &spec, &data, &bytes)) return std::nullopt;
    // Into the player's format: 16-bit, one channel, its rate.
    const SDL_AudioSpec want{SDL_AUDIO_S16, 1, kSoundRate};
    Uint8* converted = nullptr;
    int size = 0;
    const bool ok = SDL_ConvertAudioSamples(&spec, data, int(bytes), &want, &converted, &size);
    SDL_free(data);
    if (!ok) return std::nullopt;
    SoundClip clip;
    clip.rate = kSoundRate;
    clip.samples.resize(std::size_t(size) / sizeof(std::int16_t));
    SDL_memcpy(clip.samples.data(), converted, std::size_t(size));
    SDL_free(converted);
    return clip;
}

SDL_AudioStream* SoundPlayer::open() {
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 1, kSoundRate};
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream) SDL_ResumeAudioStreamDevice(stream);
    return stream;
}

SoundPlayer::SoundPlayer(SoundProvider* mods, SoundProvider* copy, SoundProvider& speaker)
    : mods_(mods), copy_(copy), speaker_(&speaker) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
    stream_ = open();
    if (mods_ || copy_)
        for (auto& voice : voices_) voice = open();
    music_ = open();
}

SoundPlayer::~SoundPlayer() {
    if (stream_) SDL_DestroyAudioStream(stream_);
    for (auto* voice : voices_)
        if (voice) SDL_DestroyAudioStream(voice);
    if (music_) SDL_DestroyAudioStream(music_);
    if (SDL_WasInit(SDL_INIT_AUDIO)) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SoundPlayer::play(int n) {
    std::optional<SoundClip> clip;
    if (mods_) clip = mods_->effect(n);
    if (!clip && copy_) clip = copy_->effect(n);
    SDL_AudioStream* stream = stream_;
    if (clip) {
        stream = voices_[next_voice_];
        next_voice_ = (next_voice_ + 1) % voices_.size();
        if (stream) SDL_ClearAudioStream(stream);
    } else {
        clip = speaker_->effect(n);
    }
    if (!stream || !clip || clip->samples.empty()) return;
    SDL_PutAudioStreamData(stream, clip->samples.data(), int(clip->samples.size() * sizeof(std::int16_t)));
    SDL_FlushAudioStream(stream);
}

void SoundPlayer::play_music(std::span<const game::SoundSegment> segments) {
    if (!music_) return;
    SDL_ClearAudioStream(music_);
    std::optional<SoundClip> clip;
    if (mods_) clip = mods_->music("title");
    if (!clip) clip = square_wave(segments, kSoundRate, kSpeakerAmplitude);
    if (clip->samples.empty()) return;
    SDL_PutAudioStreamData(music_, clip->samples.data(), int(clip->samples.size() * sizeof(std::int16_t)));
}

void SoundPlayer::stop_music() {
    if (music_) SDL_ClearAudioStream(music_);
}

}  // namespace chr::platform
