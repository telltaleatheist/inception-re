#include "platform/sitting.h"

namespace chr::platform {

void Sitting::or_no_town(const std::function<void()>& what) {
    try {
        what();
    } catch (...) {
        town_.reset();
        throw;
    }
}

void Sitting::add(const game::ScriptItem& item) {
    items_.push_back(item);
    time_ += item.idle;
    to_play_ = true;
}

void Sitting::add_time(int n) {
    if (!items_.empty() && items_.back().idle > 0) items_.back().idle += n;
    else items_.push_back({0, n});
    time_ += n;
    to_play_ = true;
}

void Sitting::play() {
    saves_->rewind();
    to_play_ = false;
    or_no_town([this] {
        town_ = kept_ ? kept_->copy() : begin_();
        game::ScriptKeys keys(items_);
        keys.set_poke([this](bool character, int r, int b, int v) {
            if (r == -4) town_->poke_position(b, v);
            else if (r == -3) town_->poke_heat(b, v);
            else if (r == -2) town_->poke_random(v);
            else if (r < 0) town_->poke_cbills(v);
            else if (character) town_->poke_character(r, b, v);
            else town_->poke_mech(r, b, v);
        });
        town_->run(keys);
    });
}

void Sitting::key(int k) { take({k, 0}); }

void Sitting::pass() {
    or_no_town([this] { town_->idle_pass(); });
    add_time(1);
    to_play_ = false;
}

void Sitting::take(const game::ScriptItem& item) {
    const bool live = waiting_at_main_loop() && !item.is_set() && !item.is_poke() && !item.tune_end;
    add(item);
    if (!live) return play();
    to_play_ = false;
    or_no_town([&] {
        game::ScriptKeys none({});
        if (item.idle == 0) town_->key(item.key, none);
        for (int i = 0; i < item.idle && town_->at_main_loop(); ++i) town_->idle_pass();
    });
}

bool Sitting::keep_place() {
    if (!waiting_at_main_loop()) return false;
    town_->forget_shown();
    saves_->settle();
    kept_ = town_->copy();
    items_.clear();
    time_ = 0;
    return true;
}

}  // namespace chr::platform
