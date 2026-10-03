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
//     if (std::optional<Selections> restored = history.undo(buffer)) selections = *restored;

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
    void close() { open_ = false; }

    [[nodiscard]] bool can_undo() const { return done_ > 0; }
    [[nodiscard]] bool can_redo() const { return done_ < steps_.size(); }

    /// Undoes the last step in `buffer`, the buffer it was recorded for, and
    /// returns the selections from before it; nothing when there is nothing
    /// to undo.
    std::optional<Selections> undo(Buffer& buffer);

    /// Makes the last step undone again in `buffer` and returns the
    /// selections from after it; nothing when there is nothing to redo.
    std::optional<Selections> redo(Buffer& buffer);

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

    std::vector<Step> steps_;
    // The steps before this one are done, the rest undone.
    std::size_t done_{0};
    // How many steps were done when the text was last saved, or NOWHERE once
    // no undo or redo can bring it back.
    std::size_t saved_{0};
    bool open_{false};
};

} // namespace fjedit
