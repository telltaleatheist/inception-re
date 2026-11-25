// Where a Town's saved games are read and written (docs/formats/save.md). The
// Town says which slot, and never touches a file itself: the platform gives
// it a store whose every action is done once, however often the Town is
// played again (platform/save_files.h, docs/resuming.md).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace chr::game {

class SaveStore {
public:
    virtual ~SaveStore() = default;
    // GameN's bytes, or nothing when it can't be read.
    virtual std::optional<std::vector<std::uint8_t>> read(int slot) = 0;
    // Whether the file was written whole.
    virtual bool write(int slot, std::span<const std::uint8_t> file) = 0;
};

}  // namespace chr::game
