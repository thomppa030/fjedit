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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fjedit::vim {

enum class Mode : uint8_t {
    /// Keys are commands, each caret on a character.
    Normal,
    /// Keys type text, each caret between characters.
    Insert,
};

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
    /// Takes `key` in the mode it is in, acting at every selection of
    /// `context`. Returns a command it names for the view or the program to
    /// run (view_commands(), the program's), when it runs none itself.
    std::optional<std::string> take(const Chord& key, CommandContext& context);

    /// Each caret put where the mode keeps it, after something other than
    /// the keys moved them (the mouse, an edit made in another view): in
    /// normal mode, on a character rather than past a line's last.
    void settle(CommandContext& context) const;

    [[nodiscard]] Mode mode() const { return mode_; }
    [[nodiscard]] CaretShape caret_shape() const;

    /// The keys of a command typed so far, in Vim's notation, as its
    /// showcmd shows them (`2d`, `g`); empty while none waits.
    [[nodiscard]] std::string pending() const;

    /// Whether the next key is the character a command waits for, as `f`
    /// and `t` wait for theirs: a key no binding may take.
    [[nodiscard]] bool wants_character() const;

    /// Drops a command half typed, as a binding run instead of it does.
    void cancel() { keys_.clear(); }

private:
    void take_normal(const Chord& key, CommandContext& context);
    void take_insert(const Chord& key, CommandContext& context);

    Mode mode_{Mode::Normal};
    // The keys of the normal-mode command typed so far.
    std::vector<Chord> keys_;
    // What f, t, F or T last looked for, which ; and , look for again.
    std::optional<CharSearch> search_;
};

} // namespace fjedit::vim
