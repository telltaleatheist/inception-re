// The 'Mech stats screen (docs/town_loop.md, "The 'Mech stats screen"): a
// 'Mech's armour, heat, pips for its engine, gyro, sensors and heat sinks,
// its weapons and actuators, over BTSTATS.CMP. The text goes through the text
// screen; the rest is this scene, in game terms. Where each part goes and
// how it looks are the renderer's (platform/stats_layer.h).
#pragma once

#include <array>
#include <optional>

namespace chr::game {

struct StatsScene {
    // 15ab:174c: one location's bar, in points: the structure at the bottom
    // and the armour above it.
    struct Bar {
        int armour = 0;
        int structure = 0;
        bool operator==(const Bar&) const = default;
    };
    static constexpr int kBars = 11;  // LA LT LL H CT RA RT RL, rear LT CT RT
    std::array<Bar, kBars> bars{};

    // The heat bar: the heat (0-30) at the bottom, the rest of 30 above it.
    // The heat part blinks between two looks; `heat_flash` is the second.
    static constexpr int kHeatMax = 30;
    int heat = 0;
    bool heat_flash = false;

    // 15ab:1858: a row of `count` pips, the first `whole` of them whole and
    // the rest lost (`whole` may be out of range, as the original's is).
    struct Pips {
        int count = 0;
        int whole = 0;
        bool operator==(const Pips&) const = default;
    };
    Pips engine, gyro, sensors, heat_sinks;

    // The screen's own colours: nothing until it first sets them (the 16th
    // frame); then the step of its pulsing colour, -1 for the colour the
    // screen's table starts with.
    std::optional<int> pulse;

    bool operator==(const StatsScene&) const = default;
};

}  // namespace chr::game
