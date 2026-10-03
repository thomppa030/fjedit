#pragma once

// What can be done to the text in a view, each under a name that a key, a
// menu or a palette calls it by. A command acts at every selection.
//
//     fjedit::CommandContext context{.buffer = buffer, .history = history,
//                                    .selections = selections, .layout = layout};
//     fjedit::run_command("caret.word_right", context);
//
// Names are `group.action`: `caret.*` moves carets, `select.*` extends
// selections.

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/history.hpp"
#include "fjedit/core/layout.hpp"
#include "fjedit/core/selections.hpp"

#include <span>
#include <string_view>

namespace fjedit {

/// What a command acts on: one view of a document, and what the view lends
/// it.
struct CommandContext {
    Buffer& buffer;
    History& history;
    Selections& selections;
    const Layout& layout;
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

} // namespace fjedit
