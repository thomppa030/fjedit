#pragma once

// The code view: one view of a document, drawn into a rectangle with ftk's UI
// core in the theme's code colours (sheet 39), taking the keyboard through a
// keymap, and the mouse. A document may have several views, a split: each
// keeps its own selections, scroll and closed folds, and catches up with the
// edits made in another through the document's note of them.
//
//     fjedit::EditorView view(document);
//     // each frame:
//     fjedit::code_view("Code", document, view, keymap, rect);

#include "fjedit/core/document.hpp"
#include "fjedit/core/folds.hpp"
#include "fjedit/core/keymap.hpp"
#include "fjedit/core/language_service.hpp"
#include "fjedit/core/selections.hpp"
#include "fjedit/core/vim/engine.hpp"
#include "fjedit/core/vim/maps.hpp"
#include "fjedit/widget/completion.hpp"
#include "fjedit/widget/find.hpp"
#include "fjedit/widget/go_to_line.hpp"
#include "fjedit/widget/markdown.hpp"

#include "ftk/ui/core/rect.hpp"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit {

/// Sheet 39's metrics: code in the mono face at 13 on lines 20 apart, line
/// numbers at 12.
inline constexpr float CODE_TEXT = 13.0f;
inline constexpr float CODE_LINE = 20.0f;
inline constexpr float GUTTER_TEXT = 12.0f;

/// How far apart a view's lines are at `text_size`: CODE_LINE at CODE_TEXT,
/// scaled with it to a whole pixel.
[[nodiscard]] float code_line(float text_size);

/// How a view numbers its lines (sheet 47, R2).
enum class LineNumbers : uint8_t {
    /// Each line its own number, from 1.
    Absolute,
    /// Each line how many rows it is from the caret's, a closed fold one
    /// row, as moving up and down counts them; the caret's line its own
    /// number, drawn against the gutter's left.
    Relative,
};

/// The number the gutter shows beside `line` with the caret on
/// `caret_line`, as `numbers` has it, `folds` closed.
[[nodiscard]] std::size_t line_number_shown(LineNumbers numbers, const Folds& folds, std::size_t line,
                                            std::size_t caret_line);

/// One view of a document: what it selects, where it is scrolled, and the
/// keys it waits on.
struct EditorView {
    /// A view of `document` as it is now, one caret at its start.
    explicit EditorView(const Document& document) : seen(document.version()) {}

    Selections selections;
    /// The folds it has closed; a caret that comes into one opens it.
    Folds folds;
    /// Its find, and the plate it floats on over the view (sheet 39, F1).
    FindState find;
    /// Its go to line, on a plate at the view's top centre.
    GoToLineState go_to_line;
    /// How far the text is scrolled, in layout units: down past the top of
    /// the first row, and along past the start of every line.
    glm::vec2 scroll{0.0f};
    /// The keys pressed so far of a binding that takes several, since when
    /// they wait for the rest (with Vim on, timeoutlen at most), and the
    /// mode keys are looked up in.
    std::vector<Chord> pending;
    double pending_since{0.0};
    std::string mode{Keymap::EDITING};
    /// Vim on this view: its modes, and the keys read in them (sheet 47).
    /// None for the standard keys.
    std::optional<vim::Engine> vim;
    /// keys.vim's maps in play on this view, the keys typed going through
    /// them before the keymap's bindings and Vim.
    vim::Maps maps;
    /// Since when keys wait for the rest of a map or a binding; and whether
    /// the plate of the keys that can come next shows, which holds them
    /// until the next key (sheet 47, S2).
    std::optional<double> waiting_since;
    bool showing_next_keys{false};
    /// The title of a command the one drawing the view runs, for that
    /// plate; the view knows its own and the editor's.
    std::function<std::string(std::string_view)> command_title;
    /// What the one drawing the view knows of its files that Vim's `:` line
    /// says (the file's name, whether closing loses changes), set before it
    /// draws the view.
    vim::FileFacts file_facts;
    /// The document's version the selections have caught up with.
    uint64_t seen{0};
    /// When the carets last moved, which they blink from.
    double moved{0.0};
    /// The primary caret is to be scrolled into sight.
    bool reveal{true};
    /// The minimap is shown down the view's right side, before the
    /// scrollbar.
    bool minimap{true};
    /// The code's text size; its lines, its line numbers and the marks on
    /// it scale with it.
    float text_size{CODE_TEXT};
    /// The text size it was last drawn at: a new size keeps the line at its
    /// top there.
    float drawn_text_size{0.0f};
    /// Notches of the wheel turned with Ctrl over it, not yet asked for as
    /// steps of text size: a touchpad's small ones add up to a step.
    float zoom{0.0f};
    /// How the gutter numbers the lines.
    LineNumbers line_numbers{LineNumbers::Absolute};
    /// How many rows it keeps in sight above and below the primary caret
    /// as the keys and commands move it, where the text has them; with
    /// more than fit, the caret stays in the middle. A click or a drag never
    /// scrolls for them.
    std::size_t around_caret{0};
    /// Commands to run on its next draw, by name, as a menu or a palette
    /// asks for them: the core's and the view's own (view_commands()).
    std::vector<std::string> queued;
    /// It takes the keyboard on its next draw: a file just opened.
    bool focus{false};
    /// What knows the document's language, and the path it knows the
    /// document by: the problems it finds are drawn on the code (sheet 39,
    /// G1). None when nothing does.
    LanguageService* service{nullptr};
    std::filesystem::path path;

    /// The word the mouse rests on, and what the language service says it
    /// is: its hover card (sheet 39).
    struct Hovering {
        /// The word, the place in it the mouse is over, and since when.
        std::optional<Span> word{};
        Position at{};
        double since{0.0};
        /// What the service was asked once the mouse had rested long
        /// enough, and the card read from its answer.
        Asked<std::optional<Hover>> asked{};
        std::vector<MarkdownBlock> card{};
        /// Where the card was drawn: the mouse may move onto it from the
        /// word without it going.
        std::optional<ftk::ui::core::Rect> card_rect{};
        /// Asked for by the keys (Vim's K) for the word at the caret: it
        /// stays, whatever the mouse does, until the caret, the text or the
        /// scroll moves.
        bool by_keys{false};
    };
    Hovering hovering{};
    /// A name being renamed where it stands (sheet 48): where the service
    /// says the symbol stands is asked, then its uses are typed over
    /// together until Enter or Esc.
    struct Renaming {
        /// The name, where the rename was asked, and the selections then.
        std::string name;
        Position at{};
        Selections before{};
        /// How many steps the history had done when the typing began,
        /// which taking it back returns to.
        std::size_t steps{0};
        /// Where the symbol stands, asked at the text's version `asked_at`.
        Asked<std::vector<Span>> asked{};
        uint64_t asked_at{0};
        /// Once begun: the uses, where they are in the text at `version`,
        /// and the one the rename was asked at.
        std::vector<Span> uses{};
        std::size_t primary{0};
        uint64_t version{0};
        bool begun{false};
    };
    std::optional<Renaming> renaming{};
    /// A colour value's colour being picked from its swatch (sheet 56, P1):
    /// the kit's picker open under the swatch and the value written over as
    /// it is dragged, as one step held open, until Enter or a click outside
    /// keeps it or Esc puts it back.
    struct Picking {
        /// Where the value is in the text at `version`, what it was written
        /// as, and the selections, when the pick began.
        Span span{};
        uint64_t version{0};
        std::string was{};
        Selections before{};
        /// How many steps the history had done when the pick began, which
        /// putting the value back returns to.
        std::size_t steps{0};
        /// The colour picked so far, sRGB, and whether the picker shows
        /// alpha: the value's colour had some.
        glm::vec4 rgba{0.0f, 0.0f, 0.0f, 1.0f};
        bool alpha{false};
        /// Where the picker opens, until it does.
        std::optional<glm::vec2> open_at{};
        /// How the value is written as the colour last picked, asked and not
        /// yet answered.
        Asked<ColourForms> asked{};
    };
    std::optional<Picking> picking{};
    /// Its completion list, while one is open (sheet 39, P1).
    CompletionState completing{};
    /// Where what is named at the caret or a Ctrl click is defined, asked
    /// and not yet answered; the format of the whole file, the same.
    Asked<std::vector<Location>> going{};
    Asked<std::optional<VersionedEdit>> formatting{};

    /// What a press of the mouse began, which dragging goes on with.
    struct Drag {
        enum class By : uint8_t {
            /// From the caret, a character at a time.
            Character,
            /// By whole words: a double click.
            Word,
            /// By whole rows, a closed fold's lines with its first: a triple
            /// click, or a press on the line numbers.
            Line,
            /// A column, with Alt; a click that doesn't drag adds a caret.
            Column,
        };
        By by{By::Character};
        bool active{false};
        /// It has moved since the press.
        bool moved{false};
        /// What the press took: its anchor, or the word or line it selected.
        Span origin{};
        /// A column's start: its line, and how far along it.
        std::size_t line{0};
        float x{0.0f};
        /// The selections before an Alt press, which a click adds a caret to.
        Selections before{};
    };
    Drag drag{};
};

/// What a code view did in a frame.
/// A rename typed where the name stands: the name `from` at `at` to become
/// `to`, everywhere it is used.
struct Rename {
    Position at;
    std::string from;
    std::string to;

    bool operator==(const Rename&) const = default;
};

struct CodeViewResult {
    /// Its text changed.
    bool edited{false};
    /// It has the keyboard.
    bool focused{false};
    /// Where what was asked about is defined, in another file: for the one
    /// drawing the view to open there.
    std::optional<Location> go{};
    /// A rename typed in place and ended with Enter, its typing taken back:
    /// for the one drawing the view to ask the service to make everywhere.
    std::optional<Rename> rename{};
    /// With Vim on, what its keys asked of the program (vim::Engine::take()),
    /// in the order they were typed: `:` commands that are the program's, and
    /// a file's marks set and gone to. For the one drawing the view to answer.
    std::vector<vim::Asked> asked{};
    /// Steps the text is asked to grow by, Ctrl and the wheel over the view,
    /// a notch a step; fewer than none to shrink. For the one drawing the
    /// view, which chooses its text size.
    int zoom{0};
};

/// Draws `view` of `document` filling `rect`: the code, then the minimap
/// and a scrollbar down its right side. It takes the mouse there and the
/// keyboard while it has it, running commands through `keymap`; with its
/// find open, the keymap's finding bindings come first. With Vim on, the
/// keymap's bindings for Vim's mode come first and Vim reads the rest of
/// the keys, by the characters they type. `label` names it to
/// tests and screen readers and is never drawn, and holds the ids of the
/// kit's pieces it draws. Kit items placed after it over `rect` take the
/// pointer there before it does.
CodeViewResult code_view(std::string_view label, Document& document, EditorView& view, const Keymap& keymap,
                         ftk::ui::core::Rect rect);

/// What the status bar says while `view` picks a colour (sheet 56, P1):
/// that Esc puts the value back as it was written and Enter keeps the new
/// one. Nothing while it picks none.
[[nodiscard]] std::optional<std::string> picking_hint(const EditorView& view);

} // namespace fjedit
