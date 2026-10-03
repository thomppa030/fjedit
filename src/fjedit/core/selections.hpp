#pragma once

// Where the carets are in one view of a buffer, and what they select. A view
// has at least one; with several, an edit acts at each. They are kept in
// order of where they start, and two that come to overlap become one.
//
//     fjedit::Selections selections(fjedit::Selection::at({3, 0}));
//     selections.add({.anchor = {7, 2}, .caret = {7, 9}});
//     for (const fjedit::Selection& selection : selections.all()) draw(selection.span());

#include "fjedit/core/buffer.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace fjedit {

struct Selection {
    /// Where it was begun: the end that stays while it is extended.
    Position anchor;
    /// Where the caret is: the end that moves.
    Position caret;
    /// How far along its line the caret tries to stay while it moves up and
    /// down, so it comes back there past shorter lines; unset until it moves
    /// up or down, and unset again by any other move.
    std::optional<float> goal{};

    /// A caret at `at` that selects nothing.
    [[nodiscard]] static Selection at(Position at) { return {.anchor = at, .caret = at}; }

    [[nodiscard]] bool empty() const { return anchor == caret; }
    /// Whether the caret is before the anchor: selected backwards.
    [[nodiscard]] bool reversed() const { return caret < anchor; }
    /// The text it selects, from its first end to its last.
    [[nodiscard]] Span span() const {
        return reversed() ? Span{.from = caret, .to = anchor} : Span{.from = anchor, .to = caret};
    }

    bool operator==(const Selection&) const = default;
};

class Selections {
public:
    /// One caret at the start of the text.
    Selections() = default;
    explicit Selections(Selection only);

    /// Every selection, in order of where it starts.
    [[nodiscard]] std::span<const Selection> all() const { return selections_; }
    [[nodiscard]] std::size_t size() const { return selections_.size(); }

    /// The one the view keeps in sight and a command for one caret acts on:
    /// the one added last, unless set().
    [[nodiscard]] const Selection& primary() const { return selections_[primary_]; }
    [[nodiscard]] std::size_t primary_index() const { return primary_; }

    /// Adds `selection` as the primary. Selections it overlaps, and carets on
    /// it or at its ends, are merged into it.
    void add(Selection selection);

    /// Replaces them all with `selections`, which must not be empty, and
    /// makes `selections[primary]` the primary. Overlapping ones merge.
    void set(std::vector<Selection> selections, std::size_t primary);

    /// Leaves only the primary: what Escape does with several.
    void keep_primary();

    /// Each selection carried through `edit`, made in their buffer by
    /// something other than them: another view, a reload, a format. A
    /// selection doesn't grow to take in text put in at its ends, a caret
    /// stays before text put in where it is, and a selection whose text was
    /// replaced shrinks to a caret before the new text.
    void map(const Edit& edit);

    /// Every end moved into `buffer` (Buffer::clamp()), for a text that
    /// changed under them without an edit to follow.
    void clamp(const Buffer& buffer);

    bool operator==(const Selections&) const = default;

private:
    // Puts them in order and merges those that overlap, following the
    // primary to where it ends up.
    void normalize();

    std::vector<Selection> selections_{Selection{}};
    std::size_t primary_{0};
};

} // namespace fjedit
