#pragma once

// Vim on one view of a document: its modes, and what each key typed does in
// the mode the view is in. It takes the keys one at a time, as the keyboard
// gives them (a character typed, or a key that types none), and acts at
// every caret through the CommandContext the view lends it. The view feeds
// it, draws its carets in the shape the mode gives, and runs the commands
// it names for others to run.
//
//     fjedit::vim::Engine vim;
//     for (const fjedit::Chord& key : keys) {
//         if (std::optional<std::string> asked = vim.take(key, context)) run(*asked);
//     }
//
// In normal mode each caret is an empty selection whose caret sits before
// the character Vim's block covers, and never past a line's last character
// unless the line is empty.

#include "fjedit/core/commands.hpp"
#include "fjedit/core/keymap.hpp"
#include "fjedit/core/vim/motions.hpp"
#include "fjedit/core/vim/registers.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fjedit::vim {

enum class Mode : uint8_t {
    /// Keys are commands, each caret on a character.
    Normal,
    /// Keys type text, each caret between characters.
    Insert,
    /// Keys type text over the characters at the carets, `R`.
    Replace,
    /// Keys move one end of each selection, `v`; an operator acts on the
    /// selections. They take the characters at both ends.
    Visual,
    /// The same with whole lines, `V`.
    VisualLine,
    /// A block of columns, `Ctrl-V`: a selection on each of its lines.
    VisualBlock,
};

/// Whether `mode` is one of the visual modes.
[[nodiscard]] constexpr bool visual(Mode mode) {
    return mode == Mode::Visual || mode == Mode::VisualLine || mode == Mode::VisualBlock;
}

/// How a view draws the carets in a mode (sheet 47, C1).
enum class CaretShape : uint8_t {
    /// The whole of the character's cell, the character cut out of it:
    /// normal mode.
    Block,
    /// A 2 px bar before the character, blinking: insert mode.
    Bar,
    /// A line under the character: replace mode.
    Underline,
    /// The lower half of the character's cell: an operator waiting for its
    /// motion.
    HalfBlock,
};

class Engine {
public:
    /// An engine in normal mode, keeping what is yanked and deleted in
    /// `registers`, which the engines of other views may share.
    explicit Engine(std::shared_ptr<Registers> registers = std::make_shared<Registers>())
        : registers_(std::move(registers)) {}

    /// Takes `key` in the mode it is in, acting at every selection of
    /// `context`. Returns a command it names for the view or the program to
    /// run (view_commands(), the program's), when it runs none itself.
    std::optional<std::string> take(const Chord& key, CommandContext& context);

    /// The selections put where the mode keeps them, after something other
    /// than the keys moved them: in normal mode each caret on a character
    /// rather than past a line's last; a selection begins visual mode when
    /// `by_mouse` made it (a drag, a double click), and otherwise becomes a
    /// caret at its start (a definition gone to). In visual mode, carets
    /// alone end it, and selections the mouse made become its own.
    void settle(CommandContext& context, bool by_mouse = false);

    [[nodiscard]] Mode mode() const { return mode_; }
    [[nodiscard]] CaretShape caret_shape() const;

    /// In visual mode, where each block caret is: on the character at the
    /// end of each selection that moves, one for a block. Empty in the other
    /// modes, whose carets are the selections' own.
    [[nodiscard]] std::vector<Position> cursors() const;

    /// The keys of a command typed so far, in Vim's notation, as its
    /// showcmd shows them (`2d`, `g`); empty while none waits.
    [[nodiscard]] std::string pending() const;

    /// Whether the next key is the character a command waits for, as `f`,
    /// `t` and `r` wait for theirs, or a register's name after `"`: a key no
    /// binding may take.
    [[nodiscard]] bool wants_character() const;

    /// Drops a command half typed, as a binding run instead of it does. A
    /// binding run as Ctrl-O's one command is that command: the mode goes
    /// back to the one Ctrl-O left.
    void cancel(CommandContext& context);

    /// What is yanked and deleted.
    [[nodiscard]] Registers& registers() { return *registers_; }

private:
    // Where a visual selection begins and where its caret is, a character
    // each (a block's corners), and the goal along the line the caret keeps.
    struct VisualEnds {
        Position anchor;
        Position caret;
        std::optional<float> goal;
    };

    void take_normal(const Chord& key, CommandContext& context);
    void take_insert(const Chord& key, CommandContext& context);
    void take_replace(const Chord& key, CommandContext& context);
    void take_visual(const Chord& key, CommandContext& context);
    // Out of insert or replace mode to normal mode, each caret onto the
    // character before it.
    void leave_insert(CommandContext& context);
    // `text` typed at every caret: put in, or in replace mode written over
    // the characters there, its line breaks put in still.
    void type_text(std::string_view text, CommandContext& context);
    // Back to the mode Ctrl-O left, its command done.
    void end_one_command(CommandContext& context);
    // Visual mode's operators and its other commands, `name` with the
    // character `argument` it takes, a count of `count` and the register
    // `reg`; false for a name that is none of them.
    bool act_on_visual(std::string_view name, std::string_view argument, std::size_t count, char reg,
                       CommandContext& context);

    // Into visual mode `mode` from normal mode or another visual mode.
    void enter_visual(Mode mode, CommandContext& context);
    // The selections visual mode's ends make.
    void show_visual(CommandContext& context);
    // Out to normal mode, each caret where its selection's was; the ends
    // kept for gv.
    void leave_visual(CommandContext& context);
    // Visual mode's ends for `selections` as something else made them.
    static std::vector<VisualEnds> ends_of(const Buffer& buffer, const Selections& selections);

    std::shared_ptr<Registers> registers_;
    Mode mode_{Mode::Normal};
    // The keys of the normal-mode command typed so far, and whether they
    // are an operator waiting for what it acts on.
    std::vector<Chord> keys_;
    bool operator_waits_{false};
    // What f, t, F or T last looked for, which ; and , look for again.
    std::optional<CharSearch> search_;
    // Visual mode's ends, and the selections it last made from them: others
    // than those were made by the mouse or an edit.
    std::vector<VisualEnds> visual_;
    Selections shown_;
    // The last visual selection and its mode, which gv selects again.
    std::vector<VisualEnds> last_visual_;
    Mode last_visual_mode_{Mode::Visual};
    // An insert begun from a block, which leaves one caret when it ends.
    bool block_insert_{false};
    // In replace mode, what each caret wrote over, for Backspace to put back.
    std::vector<std::vector<std::string>> overwritten_;
    // Ctrl-R in insert mode waits for a register's name.
    bool register_next_{false};
    // Ctrl-O runs one command in normal mode, then goes back to the mode
    // it came from. For each caret, the line it was at the end of then.
    bool one_command_{false};
    Mode resume_{Mode::Insert};
    std::vector<std::optional<std::size_t>> left_line_ends_;
};

} // namespace fjedit::vim
