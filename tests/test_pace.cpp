// Real time for the game's waits (platform/pace.h).

#include <cmath>
#include <vector>

#include "formats/tables.h"
#include "platform/pace.h"
#include "test.h"

using namespace chr;
using platform::Pace;

namespace {

constexpr double kTotalMs = 33 * 16 * 8 * 5;  // 21,120 ms: whole passes of 16, 8 and 33 ms

// Passes of the loop that add up to kTotalMs.
std::vector<std::vector<double>> passes() {
    std::vector<std::vector<double>> all;
    for (const double ms : {16.0, 8.0, 33.0, 1.0, 240.0}) {
        all.emplace_back();
        for (double t = 0; t < kTotalMs; t += ms) all.back().push_back(ms);
    }
    // Irregular ones, from a tenth of a millisecond to 77.
    all.emplace_back();
    double t = 0;
    for (unsigned n = 12345; t < kTotalMs - 100; n = n * 1103515245u + 12345u) {
        all.back().push_back(0.1 * double(1 + (n >> 16) % 770));
        t += all.back().back();
    }
    all.back().push_back(kTotalMs - t);
    return all;
}

game::ShownEvent screen(int retraces) {
    static const formats::Tables none{};
    game::ShownEvent e;
    e.screen = game::Shown{{}, {}, {}, {}, game::TextScreen(none), {}, {}};
    e.retraces = retraces;
    return e;
}

game::ShownEvent sound(double ms) {
    game::ShownEvent e;
    e.sound = 1;
    e.wait_ms = ms;
    return e;
}

}  // namespace

TEST(pace_retraces_are_the_same_whatever_the_passes) {
    const int expect = int(kTotalMs * 70.086 / 1000.0);
    for (const auto& steps : passes()) {
        Pace pace;
        int total = 0;
        double t = 0;
        for (const double ms : steps) {
            pace.add(ms);
            t += ms;
            total += pace.take_retraces();
            // Never more than a retrace behind, and never ahead.
            CHECK(std::abs(total + 0.5 - t / game::kRetraceMs) < 0.501);
        }
        CHECK(steps.size() > 80 && total == expect);
    }
}

// One taken at a time, as the town's passes are, is the same.
TEST(pace_retraces_one_at_a_time) {
    for (const auto& steps : passes()) {
        Pace all, each;
        for (const double ms : steps) {
            all.add(ms);
            each.add(ms);
            int n = 0;
            while (each.take(game::kRetraceMs)) ++n;
            CHECK(n == all.take_retraces());
        }
    }
}

// Waits one after another: each ends at the first pass at or after its
// time, counted from the first one's start, so they don't add their
// lateness up.
TEST(pace_waits_end_at_the_first_pass_after) {
    const std::vector<double> waits = {160, 47.25, 12.5, 0, 1000.125, 14, 14, 14, 375, 3.75, 900};
    for (const auto& steps : passes()) {
        Pace pace;
        double t = 0, due = waits[0];
        std::size_t at = 0;
        for (const double ms : steps) {
            const double before = t;
            pace.add(ms);
            t += ms;
            while (at < waits.size() && pace.take(waits[at])) {
                CHECK(t >= due - 1e-6);       // not early
                CHECK(before < due + 1e-6);   // nor a pass late
                if (++at < waits.size()) due += waits[at];
            }
        }
        CHECK(at == waits.size());
    }
}

TEST(pace_events_last_their_own_time) {
    CHECK(std::abs(platform::event_ms(screen(60), 1) - 856.09) < 0.01);
    CHECK(std::abs(platform::event_ms(screen(0), 1) - 14.268) < 0.001);
    CHECK(platform::event_ms(screen(0), 3) == 3 * game::kRetraceMs);
    CHECK(platform::event_ms(sound(47.25), 1) == 47.25);  // not a whole number of retraces
    CHECK(platform::event_ms(sound(0), 5) == 0);
}

TEST(pace_stalls_and_restarts) {
    Pace pace;
    pace.add(60000);  // a minute asleep is a quarter of a second
    CHECK(!pace.take(250.5) && pace.take(250) && !pace.take(0.001));
    pace.add(-5);  // a clock that steps back
    CHECK(!pace.take(0.001) && pace.take(0));
    // What a key starts has none of the time before it.
    pace.add(100);
    pace.restart();
    pace.add(16);
    CHECK(!pace.take(16.5) && pace.take(16));
    // A long wait is as long as it says: only each pass is limited.
    for (int i = 0; i < 3734; ++i) pace.add(16);
    CHECK(!pace.take(59748) && pace.take(59744));
}
