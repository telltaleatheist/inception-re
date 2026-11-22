// Draws the 'Mech stats screen's scene (game::StatsScene): BTSTATS.CMP over
// the whole screen, the armour and heat bars and the pips, in the screen's
// own colours. How it looks is the renderer's choice; this draws the
// original's (15ab:1afe, its bars 15ab:174c and pips 15ab:1858, each a
// 273d:01fb box). The text is the text screen's, drawn after it in the same
// colours.
#pragma once

#include "formats/palette.h"
#include "formats/tables.h"
#include "game/mech_stats.h"
#include "platform/renderer.h"

namespace chr::platform {

// The colours the screen shows: the standard ones until it sets its own
// (DG:1348, colour 4 by the pulse's step from DG:1378).
formats::Palette16 stats_colours(const game::StatsScene& scene, const formats::Tables& tables,
                                 const formats::Palette16& standard);

// `standard` is the palette the picture was decoded with; `colours` is
// stats_colours().
bool draw_stats(Renderer& renderer, const game::StatsScene& scene, const formats::Tables& tables,
                const formats::Palette16& standard, const formats::Palette16& colours);

}  // namespace chr::platform
