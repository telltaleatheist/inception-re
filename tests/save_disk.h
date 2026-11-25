// Saves in memory, for the tests of what is done to them.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "game/save_store.h"

namespace chrtest {

using Bytes = std::vector<std::uint8_t>;

// Saves in memory, with everything done to them.
struct Disk : chr::game::SaveStore {
    std::map<int, Bytes> files;
    std::set<int> full;                // the writes (by count) that fail
    std::vector<std::string> actions;  // "r1", "w1": the slot
    int writes = 0;
    std::optional<Bytes> read(int slot) override {
        actions.push_back("r" + std::to_string(slot + 1));
        const auto it = files.find(slot);
        if (it == files.end()) return std::nullopt;
        return it->second;
    }
    bool write(int slot, std::span<const std::uint8_t> file) override {
        actions.push_back("w" + std::to_string(slot + 1));
        if (full.contains(writes++)) return false;
        files[slot] = Bytes(file.begin(), file.end());
        return true;
    }
};

inline bool did(const Disk& d, const std::vector<std::string>& actions) { return d.actions == actions; }

}  // namespace chrtest
