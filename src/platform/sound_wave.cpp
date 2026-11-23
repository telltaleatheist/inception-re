#include "platform/sound_wave.h"

#include <cmath>

namespace chr::platform {

SoundClip square_wave(std::span<const game::SoundSegment> segments, int rate, int amplitude) {
    SoundClip clip;
    clip.rate = rate;
    double phase = 0;  // in periods, 0 to 1
    double owed = 0;   // the part of a sample the segments so far ran past
    for (const auto& s : segments) {
        const double exact = s.ms * rate / 1000.0 + owed;
        const auto n = static_cast<long>(std::floor(exact));
        owed = exact - double(n);
        for (long i = 0; i < n; ++i) {
            if (s.hz <= 0) {
                clip.samples.push_back(0);
                continue;
            }
            clip.samples.push_back(std::int16_t(phase < 0.5 ? amplitude : -amplitude));
            phase += s.hz / rate;
            phase -= std::floor(phase);
        }
    }
    return clip;
}

}  // namespace chr::platform
