// Text colours as the logic names them. A colour the game logic picks is a
// role, named by what the text is; the renderer decides how each role looks
// (the original look's table is in platform/text_layer.cpp). A colour that
// comes with the imported data (the window table, colour codes inside a
// text) stays that data's colour number, which a mod changes in the data.
#pragma once

#include <cstdint>

namespace chr::game {

enum class TextRole : std::uint8_t {
    Text,         // ordinary text
    Background,   // behind text the logic writes, and blanked cells
    Heading,      // a list's heading (weapons, crew)
    Label,        // a field's label (weapon and armour, direction, target range)
    Amount,       // a sum of money (C-Bills, cash found, course prices)
    Report,       // an outcome told (who is healed, a 'Mech destroyed)
    Emphasis,     // a word stressed inside a sentence ("not" qualified)
    PartyAction,  // a combat message about the party's shot
    EnemyAction,  // a combat message about an enemy's shot
    Targeted,     // a weapon with a target
    Worn,         // armour below its full value
    Destroyed,    // a destroyed weapon, ruined armour
    Funds,        // the party's money, shown in a shop
    FileName,     // a saved game's file name
    Warning,      // a failure the player must know of (the save failed)
    MapPointer,   // the overhead map's arrow toward a place
    Unease,       // what gives the double agent away (the inspect screen)
};
inline constexpr int kTextRoleCount = int(TextRole::Unease) + 1;

struct TextColour {
    enum class From : std::uint8_t { Data, Role };
    From from = From::Data;
    std::uint8_t value = 0;  // a colour number 0..15 (Data) or a TextRole

    static constexpr TextColour data(int number) { return {From::Data, std::uint8_t(number & 0x0f)}; }
    constexpr TextColour() = default;
    constexpr TextColour(TextRole role) : from(From::Role), value(std::uint8_t(role)) {}
    constexpr TextColour(From f, std::uint8_t v) : from(f), value(v) {}
    constexpr TextRole role() const { return TextRole(value); }
    friend constexpr bool operator==(TextColour, TextColour) = default;
};

}  // namespace chr::game
