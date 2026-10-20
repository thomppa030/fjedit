#include "fjedit/core/vim/maps.hpp"

#include <algorithm>
#include <iterator>
#include <span>

namespace fjedit::vim {

namespace {

// A map that maps itself on and on is stopped after so many, as Vim's
// maxmapdepth stops it.
constexpr int MAX_DEPTH = 1000;

// The most keys a frame plays; a long macro goes on in the frames after.
constexpr std::size_t FRAME_KEYS = 2000;

} // namespace

void Maps::take(const Chord& key, double now, const MapPlayer& player) {
    typeahead_.push_back({.key = key, .remap = true, .typed = true, .command = {}});
    play(player, false, now);
}

void Maps::play_ahead(std::span<const Chord> keys, double now, const MapPlayer& player) {
    // Ahead of the keys held for a longer map too, which came after.
    typeahead_.insert(typeahead_.begin(), std::make_move_iterator(held_.begin()), std::make_move_iterator(held_.end()));
    held_.clear();
    deadline_.reset();
    std::vector<Queued> ahead;
    for (const Chord& key : keys) ahead.push_back({.key = key, .remap = true, .typed = false, .command = {}});
    typeahead_.insert(typeahead_.begin(), std::make_move_iterator(ahead.begin()), std::make_move_iterator(ahead.end()));
    if (!playing_) play(player, false, now);
}

void Maps::drop_played() {
    std::erase_if(held_, [](const Queued& held) { return !held.typed; });
    while (!typeahead_.empty() && !typeahead_.front().typed) typeahead_.pop_front();
    if (held_.empty()) deadline_.reset();
}

void Maps::time_out(const MapPlayer& player) {
    play(player, true, 0.0);
}

std::vector<Chord> Maps::waiting() const {
    std::vector<Chord> out;
    for (const Queued& held : held_) out.push_back(held.key);
    return out;
}

std::vector<const Mapping*> Maps::longer(MapModes mode, const std::vector<Chord>& keys) const {
    std::vector<const Mapping*> out;
    if (!maps_) return out;
    for (const Mapping& map : *maps_) {
        if (any_of(map.modes, mode) && map.keys.size() > keys.size() && std::ranges::equal(keys, std::span(map.keys).first(keys.size()))) {
            out.push_back(&map);
        }
    }
    return out;
}

void Maps::play(const MapPlayer& player, bool timed_out, double now) {
    playing_ = true;
    play_keys(player, timed_out, now);
    playing_ = false;
}

void Maps::play_keys(const MapPlayer& player, bool timed_out, double now) {
    static const std::vector<Mapping> NONE;
    const std::vector<Mapping>& maps = maps_ ? *maps_ : NONE;
    // The keys typed that the maps take, for a macro being recorded.
    const auto typed = [&](std::span<const Chord> keys) {
        if (player.typed && !keys.empty()) player.typed(keys);
    };
    int depth = 0;
    std::size_t played = 0;
    while (true) {
        if (held_.empty()) {
            deadline_.reset();
            if (typeahead_.empty() || played == FRAME_KEYS) return;
            ++played;
            Queued next = std::move(typeahead_.front());
            typeahead_.pop_front();
            if (next.command) {
                player.command(*next.command);
                continue;
            }
            // A key no map may take goes on as it is.
            if (!next.remap || maps.empty() || player.wants_character()) {
                if (next.typed) typed(std::span(&next.key, 1));
                player.key(next.key);
                continue;
            }
            held_.push_back(std::move(next));
        }
        // The map the keys held make in the mode the next key is typed in,
        // and whether a longer one begins with them.
        const MapModes mode = player.mode();
        const Mapping* exact = nullptr;
        bool longer = false;
        for (const Mapping& map : maps) {
            if (!any_of(map.modes, mode) || map.keys.size() < held_.size()) continue;
            const bool begins = std::equal(held_.begin(), held_.end(), map.keys.begin(),
                                           [](const Queued& held, const Chord& key) { return held.key == key; });
            if (!begins) continue;
            if (map.keys.size() == held_.size()) {
                exact = &map;
            } else {
                longer = true;
            }
        }
        // Keys that begin a longer map wait for the rest: the next key typed
        // ahead, or one to come until the wait runs out.
        if (longer && (exact == nullptr || exact->waits) && !timed_out) {
            if (!typeahead_.empty() && typeahead_.front().remap && !typeahead_.front().command) {
                held_.push_back(std::move(typeahead_.front()));
                typeahead_.pop_front();
                continue;
            }
            if (typeahead_.empty()) {
                deadline_ = now + wait_;
                return;
            }
        }
        if (exact != nullptr) {
            std::vector<Chord> typed_keys;
            for (const Queued& held : held_) {
                if (held.typed) typed_keys.push_back(held.key);
            }
            held_.clear();
            typed(typed_keys);
            if (++depth > MAX_DEPTH) {
                player.error("E223: recursive mapping");
                typeahead_.clear();
                return;
            }
            // What it does in its keys' place, ahead of the rest. Where a
            // recursive map does what begins with its own keys, that first
            // key isn't mapped again, as in Vim.
            std::vector<Queued> does;
            for (const MapStep& step : exact->does) {
                if (step.is_command) {
                    does.push_back({.key = {}, .remap = false, .typed = false, .command = step.command});
                    continue;
                }
                for (const Chord& key : step.keys) {
                    does.push_back({.key = key, .remap = exact->recursive, .typed = false, .command = {}});
                }
            }
            const bool begins_with_itself =
                does.size() >= exact->keys.size() &&
                std::equal(exact->keys.begin(), exact->keys.end(), does.begin(),
                           [](const Chord& key, const Queued& done) { return !done.command && done.key == key; });
            if (exact->recursive && begins_with_itself) does.front().remap = false;
            typeahead_.insert(typeahead_.begin(), std::make_move_iterator(does.begin()), std::make_move_iterator(does.end()));
            continue;
        }
        // No map: the first key held goes as typed, the rest are matched
        // again from where it leaves them.
        Queued first = std::move(held_.front());
        typeahead_.insert(typeahead_.begin(), std::make_move_iterator(std::next(held_.begin())),
                          std::make_move_iterator(held_.end()));
        held_.clear();
        if (first.typed) typed(std::span(&first.key, 1));
        player.key(first.key);
    }
}

} // namespace fjedit::vim
