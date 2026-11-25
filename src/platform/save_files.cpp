#include "platform/save_files.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

#include <fcntl.h>
#if defined(_WIN32)
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif

#include "game/save.h"

namespace chr::platform {

namespace fs = std::filesystem;

namespace {

// Has the disk take what was written to the file.
bool flush_to_disk(const fs::path& path) {
#if defined(_WIN32)
    int fd = -1;
    if (_wsopen_s(&fd, path.c_str(), _O_WRONLY | _O_BINARY, _SH_DENYNO, 0) != 0) return false;
    const bool ok = _commit(fd) == 0;
    return _close(fd) == 0 && ok;
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) return false;
    bool ok = false;
#if defined(F_FULLFSYNC)
    ok = ::fcntl(fd, F_FULLFSYNC) == 0;  // not every file system has it
#endif
    if (!ok) ok = ::fsync(fd) == 0;
    return ::close(fd) == 0 && ok;
#endif
}

// The file a path leads to: a link's own target (there or not), through any
// more links. Nothing when the links never end.
std::optional<fs::path> through_links(fs::path path) {
    for (int links = 0; links < 40; ++links) {
        std::error_code ec;
        if (!fs::is_symlink(fs::symlink_status(path, ec))) return path;
        const fs::path to = fs::read_symlink(path, ec);
        if (ec) return std::nullopt;
        path = path.parent_path() / to;  // an absolute `to` replaces it all
    }
    return std::nullopt;
}

}  // namespace

std::optional<std::vector<std::uint8_t>> SaveFiles::read(int slot) {
    std::ifstream in(dir_ / game::save::file_name(slot), std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

bool SaveFiles::write(int slot, std::span<const std::uint8_t> file) {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    const auto target = through_links(dir_ / game::save::file_name(slot));
    if (!target) return false;
    // A file that may not be written (read-only, or not a file) is left alone,
    // though its folder would let it be replaced.
    const fs::file_status there = fs::status(*target, ec);
    if (fs::exists(there)) {
        if (!fs::is_regular_file(there)) return false;
        if (!std::fstream(*target, std::ios::in | std::ios::out | std::ios::binary)) return false;
    }
    fs::path part = *target;
    part += ".part";
    const fs::file_status left = fs::symlink_status(part, ec);  // by a write cut short: written over
    if (fs::exists(left) && !fs::is_regular_file(left)) return false;
    if (!may(Step::Open)) return false;
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const auto written = [&] {
        if (!may(Step::Write)) return false;
        out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
        out.close();
        if (!out) return false;
        if (!may(Step::Flush) || !flush_to_disk(part)) return false;
        // Who may read and write it stays the same.
        if (fs::exists(there)) fs::permissions(part, there.permissions(), ec);
        if (!may(Step::Replace)) return false;
        fs::rename(part, *target, ec);
        return !ec;
    };
    if (written()) return true;
    out.close();
    fs::remove(part, ec);
    return false;
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

void SaveJournal::settle() {
    actions_.clear();
    next_ = 0;
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
