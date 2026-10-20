#pragma once

// keys.vim's maps in play on one view (decision 7 of the Vim plan): each key
// typed goes through the maps of the mode the view is in as it comes, as
// Vim plays its typeahead, and what a map does takes its keys' place. Keys
// that begin a longer map wait for the rest, `timeoutlen` at most, then go
// as they stand. What comes out is played into a Player: past the keymap's
// bindings into the engine, or a `<Cmd>` line run. A macro played goes into
// the typeahead too, ahead of what was typed after it, as Vim plays one.
//
//     vim::Maps maps;
//     maps.use(std::make_shared<std::vector<vim::Mapping>>(read_keys_file(text).maps));
//     for (const Chord& key : typed) maps.take(key, now, player);
//     if (maps.deadline() && now >= *maps.deadline()) maps.time_out(player);

#include "fjedit/core/keymap.hpp"
#include "fjedit/core/vim/keys_file.hpp"

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit::vim {

/// What the maps play their keys into.
struct MapPlayer {
    /// The mode the next key is typed in.
    std::function<MapModes()> mode;
    /// Whether the next key is a character a command takes (an `f`'s),
    /// which no map takes.
    std::function<bool()> wants_character;
    /// A key on, mapped or as typed.
    std::function<void(const Chord&)> key;
    /// A `<Cmd>` line to run, between the keys around it.
    std::function<void(std::string_view)> command;
    /// What went wrong, as Vim says it ("E223: recursive mapping").
    std::function<void(std::string_view)> error;
    /// Keys typed, as the maps take them in one go: a key, or the keys of a
    /// map, before what they play. What a macro being recorded keeps.
    std::function<void(std::span<const Chord>)> typed;
};

class Maps {
public:
    /// The maps to play, shared by every view's; none plays keys as typed.
    void use(std::shared_ptr<const std::vector<Mapping>> maps) { maps_ = std::move(maps); }

    /// How long keys that begin a longer map wait for the rest.
    void wait_for(double seconds) { wait_ = seconds; }

    /// `key`, typed at `now`, through the maps into `player`.
    void take(const Chord& key, double now, const MapPlayer& player);

    /// When the keys waiting for a longer map go as they stand; none while
    /// none wait.
    [[nodiscard]] std::optional<double> deadline() const { return deadline_; }

    /// Past the deadline: the keys waiting played as they stand, the map
    /// they make run when they make one.
    void time_out(const MapPlayer& player);

    /// The keys waiting for a longer map, for the status bar to show.
    [[nodiscard]] std::vector<Chord> waiting() const;

    /// The keys waiting let go, played nowhere: Esc over the keys that can
    /// come next.
    void drop() {
        held_.clear();
        typeahead_.clear();
        deadline_.reset();
    }

    /// `keys` played ahead of all that waits to be played, through the maps
    /// as typed keys go: a macro. At once, or while keys are being played,
    /// next.
    void play_ahead(std::span<const Chord> keys, double now, const MapPlayer& player);

    /// What waits to be played that wasn't typed let go, a macro's or a
    /// map's: a command in it failed, or Ctrl-C stopped it, as Vim lets go of
    /// what it had yet to type. Keys typed stay.
    void drop_played();

    /// Whether keys a frame played as many of as it plays wait for the next:
    /// a long macro, which holds up no frame and which Ctrl-C can stop.
    [[nodiscard]] bool more() const { return held_.empty() && !typeahead_.empty(); }

    /// The keys left for this frame played, as many as a frame plays.
    void play_on(double now, const MapPlayer& player) { play(player, false, now); }

    /// The maps of `mode` whose keys begin with `keys` and go on past them,
    /// for a list of what can come next.
    [[nodiscard]] std::vector<const Mapping*> longer(MapModes mode, const std::vector<Chord>& keys) const;

private:
    // A key of the typeahead, whether the maps may take it (not one a
    // noremap put in), and whether it was typed (not a map's or a macro's).
    // Or a <Cmd> line.
    struct Queued {
        Chord key;
        bool remap{true};
        bool typed{false};
        std::optional<std::string> command;
    };

    // The typeahead played into `player` as far as it goes, and as far as a
    // frame plays; with `timed_out`, keys waiting for a longer map go as
    // they stand.
    void play(const MapPlayer& player, bool timed_out, double now);
    void play_keys(const MapPlayer& player, bool timed_out, double now);

    std::shared_ptr<const std::vector<Mapping>> maps_;
    double wait_{1.0};
    // What is yet to be played, and the keys taken from it that a map
    // begins with, waiting for the rest.
    std::deque<Queued> typeahead_;
    std::vector<Queued> held_;
    std::optional<double> deadline_;
    // Keys are being played: those a macro plays go in ahead and are played
    // next.
    bool playing_{false};
};

} // namespace fjedit::vim
