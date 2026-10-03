#include "fjedit/core/selections.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <numeric>

namespace fjedit {

namespace {

// Whether `next`, starting no earlier than `before`, belongs with it: it
// overlaps it, or one of the two is a caret on the other or at its end. Two
// selections that only touch stay apart (two words picked side by side).
bool merges(const Selection& before, const Selection& next) {
    const Span a = before.span();
    const Span b = next.span();
    if (b.from < a.to) return true;
    return b.from == a.to && (before.empty() || next.empty());
}

// One selection over both, selected in the direction `like` is.
Selection joined(const Selection& a, const Selection& b, const Selection& like) {
    const Position from = std::min(a.span().from, b.span().from);
    const Position to = std::max(a.span().to, b.span().to);
    return like.reversed() ? Selection{.anchor = to, .caret = from} : Selection{.anchor = from, .caret = to};
}

} // namespace

Selections::Selections(Selection only) : selections_{only} {}

void Selections::add(Selection selection) {
    selections_.push_back(selection);
    primary_ = selections_.size() - 1;
    normalize();
}

void Selections::set(std::vector<Selection> selections, std::size_t primary) {
    assert(!selections.empty() && primary < selections.size());
    selections_ = std::move(selections);
    primary_ = primary;
    normalize();
}

void Selections::keep_primary() {
    selections_ = {primary()};
    primary_ = 0;
}

void Selections::map(const Edit& edit) {
    for (Selection& selection : selections_) {
        if (selection.empty()) {
            selection = Selection::at(fjedit::map(edit, selection.caret, Bias::Before));
            continue;
        }
        const Span span = selection.span();
        const Position to = fjedit::map(edit, span.to, Bias::Before);
        // Ends that cross were both inside replaced text: a caret before the new text.
        const Position from = std::min(fjedit::map(edit, span.from, Bias::After), to);
        selection = selection.reversed() ? Selection{.anchor = to, .caret = from} : Selection{.anchor = from, .caret = to};
    }
    normalize();
}

void Selections::clamp(const Buffer& buffer) {
    for (Selection& selection : selections_) {
        selection.anchor = buffer.clamp(selection.anchor);
        selection.caret = buffer.clamp(selection.caret);
    }
    normalize();
}

void Selections::normalize() {
    std::vector<std::size_t> order(selections_.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const Span sa = selections_[a].span();
        const Span sb = selections_[b].span();
        return sa.from != sb.from ? sa.from < sb.from : sa.to < sb.to;
    });

    std::vector<Selection> kept;
    kept.reserve(selections_.size());
    // Where the primary went, once it has been met.
    std::size_t primary = std::numeric_limits<std::size_t>::max();
    for (const std::size_t index : order) {
        const Selection& next = selections_[index];
        const bool is_primary = index == primary_;
        if (!kept.empty() && merges(kept.back(), next)) {
            // A merge keeps the primary's direction when the primary is in it.
            const bool holds_primary = primary == kept.size() - 1;
            const Selection& like = is_primary ? next : kept.back();
            kept.back() = joined(kept.back(), next, like);
            if (is_primary || holds_primary) primary = kept.size() - 1;
            continue;
        }
        kept.push_back(next);
        if (is_primary) primary = kept.size() - 1;
    }
    selections_ = std::move(kept);
    primary_ = primary;
}

} // namespace fjedit
