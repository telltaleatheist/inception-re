// The original's pace. It never reads a clock: its waits count vertical
// retraces (273d:0006 waits for one through the VGA/EGA status port 3DA),
// so it runs at the display's refresh rate: about 70 per second on a VGA
// card in its 320x200 mode, 60 on an EGA card. The timer chip is set to
// about 291 Hz (284b:0048) only for the music driver. docs/town_loop.md, "Pace".
//
// The game logic counts passes, ticks and retraces, never time, except where
// the original's wait is none of those: an effect's length (game/sound.h)
// and the title tune's (game/music.h) are milliseconds. Scripts and tests
// give the counts directly, so a run does not depend on the machine; the
// window's loop gives each its time (platform/pace.h).
#pragma once

namespace chr::game {

// A VGA card's refresh rate in the 320x200 modes (the rebuild's reference,
// and what DOSBox shows). An EGA card refreshes at about 60.
inline constexpr double kVgaRefreshHz = 70.086;

// One retrace: 14.268 ms.
inline constexpr double kRetraceMs = 1000.0 / kVgaRefreshHz;

}  // namespace chr::game
