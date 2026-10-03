#include "fjedit/core/history.hpp"

#include <utility>

namespace fjedit {

void History::record(RecordedEdit edit) {
    if (done_ < steps_.size()) {
        if (saved_ > done_) saved_ = NOWHERE;
        steps_.resize(done_);
    }

    const bool joins = open_ && !steps_.empty() && edit.kind != EditKind::Other && steps_.back().kind == edit.kind;
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
    open_ = edit.kind != EditKind::Other;
}

std::optional<History::Move> History::undo() {
    if (!can_undo()) return std::nullopt;
    const Step& step = steps_[--done_];
    open_ = false;
    return Move{.edits = {step.undo.rbegin(), step.undo.rend()}, .selections = step.before};
}

std::optional<History::Move> History::redo() {
    if (!can_redo()) return std::nullopt;
    const Step& step = steps_[done_++];
    open_ = false;
    return Move{.edits = step.made, .selections = step.after};
}

void History::mark_saved() {
    saved_ = done_;
    open_ = false;
}

} // namespace fjedit
