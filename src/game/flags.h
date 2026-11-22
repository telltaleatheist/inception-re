// The script flags: the bytes from F:d30c (docs/scripts.md). The .BLD
// scripts read and write them by number, and the rest of the program keeps
// some of its own state in the same bytes, so they are one array here too.
// The scripts use 00-3f; C-Bills follow at d370 (flag 64).
#pragma once

#include <array>
#include <cstdint>

namespace chr::game {

inline constexpr int kFlagCount = 0x64;
using Flags = std::array<std::uint8_t, kFlagCount>;

namespace flag {
inline constexpr int kMission = 0x00;          // d30c: the training mission (17dc:0629's mode)
inline constexpr int kPassed = 0x01;           // d30d: the last training mission passed
inline constexpr int kTrainingMech = 0x02;     // d30e: the 'Mech chosen at the training center
inline constexpr int kTries = 0x03;            // d30f: failed tries at the training mission
inline constexpr int kOutdoors = 0x04;        // d310
inline constexpr int kRaidMech = 0x05;        // d311: Jason's 'Mech came through the raid
inline constexpr int kCourse = 0x08;          // d314: the course chosen (special 05)
inline constexpr int kEnrolled = 0x09;        // d315: special 05's answer
inline constexpr int kInPlace = 0x0a;         // d316: set on entering a place
inline constexpr int kPlace = 0x0d;           // d319: which place, for talking
inline constexpr int kMembers = 0x0e;         // d31a: members besides Jason (special 14)
inline constexpr int kQuizLock = 0x14;        // d320: set ff by a failed training quiz, counts down
inline constexpr int kMoneyCountdown = 0x17;  // d323: the money step runs when it passes 0
inline constexpr int kLetOut = 0x2d;          // d339
inline constexpr int kLetOutDone = 0x2e;      // d33a
inline constexpr int kOverheadPointer = 0x2f;  // d33b: the overhead map points toward 0a38,8038 (1000:43e2; set by a script)
inline constexpr int kFrobPaid = 0x30;        // d33c: FROB's first payment
inline constexpr int kDiskSeen = 0x32;        // d33e: set at VIEWDISK's end
inline constexpr int kMoneyFloor = 0x33;      // d33f-d340: a word; the training center raises it
inline constexpr int kCacheOpen = 0x36;       // d342: ENTRANCE answered yes: leaving it goes into the cache (docs/cache.md)
inline constexpr int kMapper = 0x31;          // d33d: FROB's terrain mapper: the seen map marks a whole column of an area (1000:010f)
inline constexpr int kTileEvents = 0x3a;      // d346: a map whose tiles have events (map 14, docs/movement.md)
inline constexpr int kCachePuzzle = 0x3e;      // d34a: the puzzle solved (1b5d:0980)
inline constexpr int kCacheItems = 0x3b;      // d347-d349: the item carried in each of three slots, or ff
inline constexpr int kCacheEndmech = 0x3f;     // d34b: ENDMECH's tiles off
inline constexpr int kCacheHint = 0x40;       // d34c: 1b5d:02d2's text seen: the way out answers
inline constexpr int kCacheFindit = 0x41;      // d34d: FINDIT seen (1b5d:055a)
inline constexpr int kCacheLevel = 0x42;      // d34e: on the cache's second level
inline constexpr int kCacheDoors = 0x43;      // d34f-d35a: door d open
inline constexpr int kPictureChance = 0x4f;   // d35b: how often a place's picture shows
}  // namespace flag

// A new game's flags: the money floor is 50 (1000:4f70); the rest are 0.
inline Flags new_game_flags() {
    Flags f{};
    f[flag::kMoneyFloor] = 50;
    return f;
}

inline int flag_word(const Flags& f, int at) { return f[std::size_t(at)] | f[std::size_t(at) + 1] << 8; }

}  // namespace chr::game
