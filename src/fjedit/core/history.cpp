#include "fjedit/core/history.hpp"

#include <cstddef>
#include <iterator>
#include <utility>

namespace fjedit {

void History::record(RecordedEdit edit) {
    if (done_ < steps_.size()) {
        if (saved_ > done_) saved_ = NOWHERE;
        steps_.resize(done_);
    }

    const bool joins = open_ && !steps_.empty() &&
                       (held_ || (edit.kind != EditKind::Other && steps_.back().kind == edit.kind));
    if (joins) {
        Step& step = steps_.back();
        step.made.push_back(std::move(edit.made));
        step.undo.push_back(std::move(edit.undo));
        step.after = std::move(edit.after);
    } else {
        Step step;
        step.made.push_back(std::move(edit.made));
        step.undo.push_back(std::move(edit.undo));
        step.before = std::move(edit.before);
        step.after = std::move(edit.after);
        step.kind = edit.kind;
        steps_.push_back(std::move(step));
        done_ = steps_.size();
    }
    open_ = held_ || edit.kind != EditKind::Other;
}

std::optional<History::Move> History::undo() {
    if (!can_undo()) return std::nullopt;
    const Step& step = steps_[--done_];
    end_step();
    return Move{.edits = {step.undo.rbegin(), step.undo.rend()}, .selections = step.before};
}

std::optional<History::Move> History::redo() {
    if (!can_redo()) return std::nullopt;
    const Step& step = steps_[done_++];
    end_step();
    return Move{.edits = step.made, .selections = step.after};
}

std::optional<History::Move> History::take_back(std::size_t steps) {
    if (done_ <= steps) return std::nullopt;
    Move move{.edits = {}, .selections = steps_[steps].before};
    for (std::size_t i = done_; i-- > steps;) {
        move.edits.insert(move.edits.end(), steps_[i].undo.rbegin(), steps_[i].undo.rend());
    }
    // A save among the steps forgotten can't be come back to.
    if (saved_ != NOWHERE && saved_ > steps) saved_ = NOWHERE;
    steps_.resize(steps);
    done_ = steps;
    end_step();
    return move;
}

void History::join(std::size_t steps) {
    if (done_ <= steps + 1) return;
    Step& joined = steps_[steps];
    for (std::size_t i = steps + 1; i < done_; ++i) {
        Step& step = steps_[i];
        joined.made.insert(joined.made.end(), std::make_move_iterator(step.made.begin()),
                           std::make_move_iterator(step.made.end()));
        joined.undo.insert(joined.undo.end(), std::make_move_iterator(step.undo.begin()),
                           std::make_move_iterator(step.undo.end()));
        joined.after = std::move(step.after);
    }
    joined.kind = EditKind::Other;
    // A save between the steps joined can't be come back to; one after them
    // moves down with them.
    const std::size_t gone = done_ - steps - 1;
    if (saved_ != NOWHERE && saved_ > steps && saved_ < done_) {
        saved_ = NOWHERE;
    } else if (saved_ != NOWHERE && saved_ >= done_) {
        saved_ -= gone;
    }
    steps_.erase(steps_.begin() + static_cast<std::ptrdiff_t>(steps + 1), steps_.begin() + static_cast<std::ptrdiff_t>(done_));
    done_ = steps + 1;
    end_step();
}

void History::mark_saved() {
    saved_ = done_;
    end_step();
}

} // namespace fjedit
