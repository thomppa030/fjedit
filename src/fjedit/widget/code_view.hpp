#pragma once

// The code view: one view of a document, drawn into a rectangle with ftk's UI
// core in the theme's code colours (sheet 39), taking the keyboard through a
// keymap, and the mouse. A document may have several views, a split: each
// keeps its own selections and scroll, and catches up with the edits made in
// another through the document's note of them.
//
//     fjedit::EditorView view(document);
//     // each frame:
//     fjedit::code_view("Code", document, view, keymap, rect);

#include "fjedit/core/document.hpp"
#include "fjedit/core/keymap.hpp"
#include "fjedit/core/selections.hpp"

#include "ftk/ui/core/rect.hpp"

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit {

/// Sheet 39's metrics: code in the mono face at 13 on lines 20 apart, line
/// numbers at 12.
inline constexpr float CODE_TEXT = 13.0f;
inline constexpr float CODE_LINE = 20.0f;
inline constexpr float GUTTER_TEXT = 12.0f;

/// One view of a document: what it selects, where it is scrolled, and the
/// keys it waits on.
struct EditorView {
    /// A view of `document` as it is now, one caret at its start.
    explicit EditorView(const Document& document) : seen(document.version()) {}

    Selections selections;
    /// How far the text is scrolled, in layout units: down past the top of
    /// the first line, and along past the start of every line.
    glm::vec2 scroll{0.0f};
    /// The keys pressed so far of a binding that takes several, and the mode
    /// keys are looked up in.
    std::vector<Chord> pending;
    std::string mode{Keymap::EDITING};
    /// The document's version the selections have caught up with.
    uint64_t seen{0};
    /// When the carets last moved, which they blink from.
    double moved{0.0};
    /// The primary caret is to be scrolled into sight.
    bool reveal{true};
};

/// What a code view did in a frame.
struct CodeViewResult {
    /// Its text changed.
    bool edited{false};
    /// It has the keyboard.
    bool focused{false};
};

/// Draws `view` of `document` filling `rect`, and takes the mouse there and
/// the keyboard while it has it, running commands through `keymap`. `label`
/// names it to tests and screen readers and is never drawn.
CodeViewResult code_view(std::string_view label, Document& document, EditorView& view, const Keymap& keymap,
                         ftk::ui::core::Rect rect);

} // namespace fjedit
