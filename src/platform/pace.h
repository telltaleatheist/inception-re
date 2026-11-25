// Real time for the game's waits, in milliseconds. The logic counts what the
// original counted (game/clock.h); here a count becomes a time, and a wait
// ends at the first pass of the window's loop at or after its time. What is
// left over goes to the next one, so nothing depends on how long a pass
// takes. No SDL, so the tests can give it any passes.
#pragma once

#include <algorithm>

#include "game/clock.h"
#include "game/shown.h"

namespace chr::platform {

class Pace {
public:
    // A pass longer than this (a moved window, a machine asleep) counts as
    // this much: the rest is not caught up.
    static constexpr double kLongestPassMs = 250;

    // A pass took `ms`.
    void add(double ms) { due_ms_ += std::clamp(ms, 0.0, kLongestPassMs); }
    // Uses `ms` of the time there is; false, and none used, if there is less.
    bool take(double ms) {
        if (due_ms_ < ms) return false;
        due_ms_ -= ms;
        return true;
    }
    // Uses every whole retrace there is, and returns how many.
    int take_retraces() {
        const int n = int(due_ms_ / game::kRetraceMs);
        due_ms_ = std::max(0.0, due_ms_ - n * game::kRetraceMs);
        return n;
    }
    // Something new starts now: none of the time before it counts.
    void restart() { due_ms_ = 0; }

private:
    double due_ms_ = 0;
};

// How long an event is up: a sound its own time, a screen its retraces. One
// the original showed without a wait went by as fast as the machine drew the
// next; it is held `step_hold` retraces (chr --step-hold).
inline double event_ms(const game::ShownEvent& e, int step_hold) {
    if (!e.screen) return e.wait_ms;
    return (e.retraces > 0 ? e.retraces : step_hold) * game::kRetraceMs;
}

}  // namespace chr::platform
