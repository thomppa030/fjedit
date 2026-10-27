#pragma once

// What can be done to the text in a view, each under a name that a key, a
// menu or a palette calls it by. A command acts at every selection.
//
//     fjedit::CommandContext context{.document = document, .selections = selections, .layout = layout};
//     fjedit::run_command("caret.word_right", context);
//
// Names are `group.action`: `caret.*` moves carets, `select.*` extends or
// picks selections, `cursor.*` adds and removes carets, `edit.*` changes the
// text, `fold.*` opens and closes folds. A keymap also names what a view does
// itself, `find.*` and `goto.*` (view_commands()), which run_command() leaves
// to the view.

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/document.hpp"
#include "fjedit/core/find.hpp"
#include "fjedit/core/folds.hpp"
#include "fjedit/core/layout.hpp"
#include "fjedit/core/selections.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit {

/// The system's clipboard, as the program reaches it.
struct Clipboard {
    std::function<std::string()> get;
    std::function<void(std::string)> set;
};

/// What a command acts on: one view of a document, and what the view lends
/// it.
/// The rows of the text a view has in sight, for what moves by them or
/// scrolls the view (Vim's H, Ctrl-D, zz). A command may change `top`; the
/// view then scrolls there.
struct InView {
    /// The first row wholly in sight; a closed fold's lines are one row.
    std::size_t top{0};
    /// How many rows are wholly in sight.
    std::size_t rows{1};
    /// How many rows the view keeps in sight above and below the caret.
    std::size_t around{0};
};

struct CommandContext {
    /// The text the command edits, with its history, colours and indent.
    Document& document;
    Selections& selections;
    const Layout& layout;
    /// Cut, copy and paste do nothing without one.
    Clipboard* clipboard{nullptr};
    /// The folds the view has closed, caught up with the document; the
    /// fold commands do nothing without them.
    Folds* folds{nullptr};
    /// What `edit.type` types.
    std::string_view text{};
    /// Typing an opener or a quote types its closer too, wraps a selection
    /// in the pair, and steps over a closer already there; Backspace takes
    /// an empty pair whole. Off, typing types only what it is given.
    bool close_pairs{false};
    /// The rows the view has in sight; what moves by them does nothing
    /// without them.
    InView* in_view{nullptr};
};

struct Command {
    /// What keys and menus call it by.
    std::string_view name;
    /// What a menu or a palette shows.
    std::string_view title;
    void (*run)(CommandContext& context);
};

/// Every command, in the order a palette lists them.
[[nodiscard]] std::span<const Command> commands();

/// The command called `name`, or null when there is none.
[[nodiscard]] const Command* find_command(std::string_view name);

/// Runs the command called `name`; false when there is none.
bool run_command(std::string_view name, CommandContext& context);

/// What a view does itself rather than to its text, named in a keymap as a
/// command is: its find, go to line.
struct ViewCommand {
    std::string_view name;
    std::string_view title;
};

/// Every view command, in the order a palette lists them.
[[nodiscard]] std::span<const ViewCommand> view_commands();

/// Whether `name` is a view command.
[[nodiscard]] bool is_view_command(std::string_view name);

/// Makes `edit`, changes that come from elsewhere than the keys (a
/// completion chosen, a format, a rename), as one step of the history. The
/// selections are carried through it as through an edit made elsewhere
/// (Selections::map()), or become a caret at `caret` after it when given.
/// Returns where the changes' text is once made (spans_after()).
std::vector<Span> apply_edit(CommandContext& context, Edit edit, std::optional<Position> caret = std::nullopt);

/// Replaces each of `matches`, places find_all() found `needle` at with
/// `options`, with what replacement() gives for `with`, as one step of the
/// history. The selections are carried through it as through an edit made
/// elsewhere (Selections::map()). Returns where the replacements are.
std::vector<Span> replace_matches(CommandContext& context, std::span<const Span> matches, std::string_view needle,
                                  std::string_view with, FindOptions options);

/// The selections a column dragged from `anchor_x` on `anchor_line` to
/// `caret_x` on `caret_line` makes (Alt and the mouse): one on each line
/// between, from the one place along it to the other. A line ending before
/// the column begins takes no part; the caret's line always does, and holds
/// the primary.
[[nodiscard]] Selections column_selection(const Buffer& buffer, const Layout& layout, std::size_t anchor_line,
                                          float anchor_x, std::size_t caret_line, float caret_x);

} // namespace fjedit
