// A sitting: the Town and the keys it has had, so that it can be played again with the next one
// (docs/resuming.md). No SDL.
#pragma once

#include <deque>
#include <functional>
#include <memory>

#include "game/town.h"
#include "platform/save_files.h"

namespace chr::platform {

class Sitting {
public:
    // `begin` makes the Town a sitting starts with, before any key. `saves` is the one it is given.
    using Begin = std::function<std::unique_ptr<game::Town>()>;
    Sitting(Begin begin, SaveJournal& saves) : begin_(std::move(begin)), saves_(&saves) {}

    // Null before the first play(), and after anything here threw: then only play() has one again.
    const std::unique_ptr<game::Town>& town() const { return town_; }

    // An item for the next play().
    void add(const game::ScriptItem& item);
    // `n` passes or retraces with no key, for the next play().
    void add_time(int n);
    // A new Town from the place kept, or from the start, given every item since.
    void play();
    // A key: at the main loop the Town takes it; where a question waits it is played again with it.
    void key(int k);
    // A pass of the main loop with no key.
    void pass();
    // A script's item, taken as the window's loop would.
    void take(const game::ScriptItem& item);

    // At the main loop, with every item played: the Town is kept as it is here, and play() goes on from
    // it. What it has shown and logged is forgotten, and so are the items and what was read and written
    // up to here. False, and nothing changes, anywhere else.
    bool keep_place();
    // The passes and retraces since the place kept, or the start.
    long time() const { return time_; }

private:
    void or_no_town(const std::function<void()>& what);
    bool waiting_at_main_loop() const { return town_ && !to_play_ && town_->at_main_loop(); }

    Begin begin_;
    SaveJournal* saves_;
    std::unique_ptr<game::Town> town_;
    std::unique_ptr<game::Town> kept_;
    std::deque<game::ScriptItem> items_;  // since the place kept, or the start
    long time_ = 0;
    bool to_play_ = false;  // the last of the items are not in the Town yet
};

}  // namespace chr::platform
