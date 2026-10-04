#pragma once

// What can be done to the text in a view, each under a name that a key, a
// menu or a palette calls it by. A command acts at every selection.
//
//     fjedit::CommandContext context{.document = document, .selections = selections, .layout = layout};
//     fjedit::run_command("caret.word_right", context);
//
// Names are `group.action`: `caret.*` moves carets, `select.*` extends or
// picks selections, `cursor.*` adds and removes carets, `edit.*` changes the
// text, `fold.*` opens and closes folds.

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/document.hpp"
#include "fjedit/core/find.hpp"
#include "fjedit/core/folds.hpp"
#include "fjedit/core/layout.hpp"
#include "fjedit/core/selections.hpp"

#include <cstddef>
#include <functional>
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
