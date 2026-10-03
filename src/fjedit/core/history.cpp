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

std::optional<Selections> History::undo(Buffer& buffer) {
    if (!can_undo()) return std::nullopt;
    const Step& step = steps_[--done_];
    for (auto edit = step.undo.rbegin(); edit != step.undo.rend(); ++edit) (void)buffer.apply(*edit);
    open_ = false;
    return step.before;
}

std::optional<Selections> History::redo(Buffer& buffer) {
    if (!can_redo()) return std::nullopt;
    const Step& step = steps_[done_++];
    for (const Edit& edit : step.made) (void)buffer.apply(edit);
    open_ = false;
    return step.after;
}

void History::mark_saved() {
    saved_ = done_;
    open_ = false;
}

} // namespace fjedit
