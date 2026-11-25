// The player's saved games on disk, and the journal that keeps a Town played
// again from touching them again. No SDL.
#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

#include "game/save_store.h"

namespace chr::platform {

// GameN in a folder.
class SaveFiles : public game::SaveStore {
public:
    explicit SaveFiles(std::filesystem::path dir) : dir_(std::move(dir)) {}
    std::optional<std::vector<std::uint8_t>> read(int slot) override;
    bool write(int slot, std::span<const std::uint8_t> file) override;

private:
    std::filesystem::path dir_;
};

// Each read and write of a sitting, in order. A Town played again from the
// start (docs/resuming.md) does the same ones in the same order: the nth is
// done the first time, and after that answered with what it answered then. It
// lasts as long as the sitting's keys do.
class SaveJournal : public game::SaveStore {
public:
    explicit SaveJournal(game::SaveStore& saves) : saves_(&saves) {}
    // A new Town starts: its reads and writes are the first again.
    void rewind() { next_ = 0; }
    std::optional<std::vector<std::uint8_t>> read(int slot) override;
    bool write(int slot, std::span<const std::uint8_t> file) override;
    // Whether a Town played again ever asked for another read or write than
    // it did the first time, or wrote other bytes. It isn't the game the
    // player played, so the saves are left alone: that read or write fails,
    // and so does every later write. (Other bytes alone: what was written
    // stays, and later writes go on.)
    bool strayed() const { return strayed_ || differed_; }

private:
    struct Action {
        bool writes = false;
        int slot = 0;
        bool done = false;                // a read found the file; a write wrote it
        std::vector<std::uint8_t> bytes;  // read or written
    };
    const Action* again(bool writes, int slot);

    game::SaveStore* saves_;
    std::vector<Action> actions_;
    std::size_t next_ = 0;
    bool strayed_ = false, differed_ = false;
};

}  // namespace chr::platform
