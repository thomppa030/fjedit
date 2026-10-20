#pragma once

// What can be undone in a buffer, step by step. A step is one edit or more,
// with the selections from before the first and after the last: a word typed
// is one step, not one per letter. The editor says when a step ends, with
// close(): when the caret moves by itself, on a pause in typing, on saving.
//
//     const Selections before = selections;
//     const Edit undo = buffer.apply(edit);
//     selections = ...;
//     history.record({.made = edit, .undo = undo, .before = before, .after = selections,
//                     .kind = EditKind::Typing});
//
//     if (std::optional<History::Move> undone = history.undo()) {
//         for (const Edit& e : undone->edits) buffer.apply(e);
//         selections = undone->selections;
//     }

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/selections.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace fjedit {

/// What an edit was, which says whether it joins the step before it.
enum class EditKind : uint8_t {
    /// Text typed: joins an open step of typing.
    Typing,
    /// Text deleted a character or a word at a time: joins an open step of
    /// deleting.
    Deleting,
    /// Anything else, a paste, a line moved, a replace-all: a step of its own.
    Other,
};

/// An edit just made, as History::record() takes it.
struct RecordedEdit {
    /// The edit as it was made.
    Edit made;
    /// What Buffer::apply() returned for it.
    Edit undo;
    /// The selections before it and after it.
    Selections before;
    Selections after;
    EditKind kind{EditKind::Other};
};

class History {
public:
    /// Records an edit just made in the buffer. It joins the open step when
    /// both are typing or both deleting, and begins a new step otherwise.
    /// Steps that could have been redone are gone.
    void record(RecordedEdit edit);

    /// Ends the open step, so the next edit begins one of its own.
    void close() { end_step(); }

    /// Makes the next edit begin a step that every edit after it joins,
    /// whatever their kinds, until the step ends: close(), an undo, a redo
    /// or a save. A change that deletes and then types, as a modal
    /// editor's `cw`, is undone as one.
    void hold() {
        end_step();
        held_ = true;
    }

    /// How many steps are done: what take_back() takes the text back to.
    [[nodiscard]] std::size_t done() const { return done_; }

    [[nodiscard]] bool can_undo() const { return done_ > 0; }
    [[nodiscard]] bool can_redo() const { return done_ < steps_.size(); }

    /// What undoing or redoing a step takes: its edits, to be made in order
    /// in the text they were recorded for, and the selections it leaves.
    struct Move {
        std::vector<Edit> edits;
        Selections selections;
    };

    /// Takes the last step done back: the edits that undo it, and the
    /// selections from before it. Nothing when there is nothing to undo.
    std::optional<Move> undo();

    /// Takes the last step undone forward again: its edits, and the
    /// selections from after it. Nothing when there is nothing to redo.
    std::optional<Move> redo();

    /// Takes back every step done after the first `steps` (done() when the
    /// change began), as undo() would, and forgets them, so nothing redoes
    /// them: a change tried and given up, such as a name typed over in
    /// place. The selections are those from before the first. Nothing when
    /// no step was done after them.
    std::optional<Move> take_back(std::size_t steps);

    /// Makes the steps done after the first `steps` (done() when a command
    /// began) one step, undone and redone together: what one command made
    /// through others, such as Vim's :g and :normal.
    void join(std::size_t steps);

    /// Notes that the text as it is now is what its file holds, and ends the
    /// open step.
    void mark_saved();

    /// Whether the text is as it was when last saved, or as it was loaded:
    /// undoing an edit back to there makes it unchanged again.
    [[nodiscard]] bool at_saved() const { return saved_ == done_; }

private:
    struct Step {
        std::vector<Edit> made;
        std::vector<Edit> undo;
        Selections before;
        Selections after;
        EditKind kind{EditKind::Other};
    };

    static constexpr std::size_t NOWHERE = std::numeric_limits<std::size_t>::max();

    void end_step() {
        open_ = false;
        held_ = false;
    }

    std::vector<Step> steps_;
    // The steps before this one are done, the rest undone.
    std::size_t done_{0};
    // How many steps were done when the text was last saved, or NOWHERE once
    // no undo or redo can bring it back.
    std::size_t saved_{0};
    bool open_{false};
    // hold() was asked: the open step takes every edit until it ends.
    bool held_{false};
};

} // namespace fjedit
