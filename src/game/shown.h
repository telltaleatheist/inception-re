// What the screen shows while the logic runs on without the player: the
// fight's shots with combat graphics, and the pauses after its messages
// (docs/combat.md 3.14, docs/resuming.md). The logic records each moment
// as an event and runs on; the platform plays them at its own pace when
// the logic next stops for input.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "game/asset_id.h"
#include "game/fight.h"
#include "game/figures.h"
#include "game/map_view.h"
#include "game/sound.h"
#include "game/text.h"

namespace chr::game {

// A place's picture in the upper left box (1000:48b7): O<number>.ANM.
struct PlacePicture {
    int number = 0;
    int frame = 0xffff;  // the frame left showing; 0xffff, the last
    bool operator==(const PlacePicture&) const = default;
};

// A sprite drawn over the view at a point of the 320x200 screen (its
// top-left corner): a missile or an explosion (1e31:1f73).
struct ShownSprite {
    AssetId sprite;
    int x = 0, y = 0;
};

// A shot drawn as a line, from the shooter's muzzle to the target, in
// points of the 320x200 screen (22e8:000c). What it is, not how it looks:
// the renderer draws and colours it (the original look plots it a pixel at
// a time, platform/map_layer.cpp).
struct ShownLine {
    enum class Kind : std::uint8_t {
        HandLaser,  // a person's laser pistol or rifle (weapons 0x0c-0x0d)
        MechBeam,   // a 'Mech's lasers and PPC (weapons 0x0f-0x12)
    };
    enum class Shooter : std::uint8_t { Party, EnemyMech, EnemyPerson };
    int from_x = 0, from_y = 0;
    int to_x = 0, to_y = 0;
    Kind kind = Kind::MechBeam;
    Shooter shooter = Shooter::Party;
    int weapon = 0;  // the weapon's number (its name: text weapon.N)
};

// The screen at one moment: the view and its figures as last shown, the
// sprites drawn over them, the shot lines on top, the text, the boxes
// XORed over the map, and the place picture.
struct Shown {
    MapView view;
    std::vector<Figure> figures;
    std::vector<ShownSprite> sprites;
    std::vector<ShownLine> lines;
    TextScreen text;
    std::vector<XorBox> boxes;
    std::optional<PlacePicture> picture;
};

// One event: a sound played (1000:19bf: its number, and how long the game
// waits for it, 0 with the sound off), or the screen shown
// and held for `frames` display frames (273d:0006 counts them).
struct ShownEvent {
    std::optional<Shown> screen;
    int frames = 0;
    int sound = -1;
    double wait_ms = 0;
};

}  // namespace chr::game
