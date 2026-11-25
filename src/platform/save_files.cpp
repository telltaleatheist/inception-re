#include "platform/save_files.h"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "game/save.h"

namespace chr::platform {

std::optional<std::vector<std::uint8_t>> SaveFiles::read(int slot) {
    std::ifstream in(dir_ / game::save::file_name(slot), std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

bool SaveFiles::write(int slot, std::span<const std::uint8_t> file) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    std::ofstream out(dir_ / game::save::file_name(slot), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
    out.close();
    return bool(out);
}

// The next action, if it has been done before; null if it is a new one. One
// that isn't what was done before is answered with a failure.
const SaveJournal::Action* SaveJournal::again(bool writes, int slot) {
    static const Action kStrayed;
    if (next_ == actions_.size()) return nullptr;
    const Action& action = actions_[next_++];
    if (action.writes == writes && action.slot == slot) return &action;
    strayed_ = true;
    return &kStrayed;
}

std::optional<std::vector<std::uint8_t>> SaveJournal::read(int slot) {
    const Action* action = again(false, slot);
    if (!action) {
        auto file = saves_->read(slot);
        action = &actions_.emplace_back(Action{false, slot, file.has_value(), file ? std::move(*file) : std::vector<std::uint8_t>{}});
        ++next_;
    }
    if (!action->done) return std::nullopt;
    return action->bytes;
}

bool SaveJournal::write(int slot, std::span<const std::uint8_t> file) {
    const Action* action = again(true, slot);
    if (!action) {
        const bool done = !strayed_ && saves_->write(slot, file);
        action = &actions_.emplace_back(Action{true, slot, done, {file.begin(), file.end()}});
        ++next_;
    } else if (action->writes && !std::equal(file.begin(), file.end(), action->bytes.begin(), action->bytes.end())) {
        differed_ = true;
    }
    return action->done;
}

}  // namespace chr::platform
