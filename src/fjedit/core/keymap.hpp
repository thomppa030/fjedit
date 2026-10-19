#pragma once

// Which command a key runs. A binding is a sequence of keys (most are one;
// "Ctrl K Ctrl C" is two, as are a modal layer's "d d"), held in a mode:
// plain editing is one, and a modal layer adds its own. While the keys
// pressed so far begin a longer binding, the view waits for the next.
//
//     const fjedit::Keymap keys = fjedit::Keymap::standard();
//     pressed.push_back(chord);
//     const fjedit::KeyMatch match = keys.find(fjedit::Keymap::EDITING, pressed);
//     if (match.command) { run_command(*match.command, context); pressed.clear(); }
//     else if (!match.waiting) pressed.clear();

#include "ftk/ui/core/input.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit {

/// A key, with the modifiers held as it is pressed; or, for a layer that
/// reads keys by what they type (Vim's `$`, `{`, `/`, wherever a keyboard
/// puts them), the character typed.
struct Chord {
    ftk::ui::Key key{};
    ftk::ui::Modifiers modifiers{};
    /// The character typed, as UTF-8; empty for a key.
    std::string text{};

    /// The chord that is `text` typed.
    [[nodiscard]] static Chord typed(std::string_view text) { return {.text = std::string(text)}; }

    bool operator==(const Chord&) const = default;
};

/// A chord as menus show it and keymap files write it: the modifiers in the
/// order Ctrl, Shift, Alt, Super, then the key, apart by spaces ("Ctrl Shift
/// K", "Alt Up", "Ctrl ]"); a character typed as itself.
[[nodiscard]] std::string chord_text(const Chord& chord);

/// The chord written as chord_text() writes it, or nothing when it names no
/// key or a modifier twice. Modifiers may come in any order.
[[nodiscard]] std::optional<Chord> parse_chord(std::string_view text);

/// What the keys pressed so far come to.
struct KeyMatch {
    /// The command they run, when they make a whole binding.
    std::optional<std::string_view> command;
    /// They begin a longer binding: wait for the next key.
    bool waiting{false};
};

class Keymap {
public:
    /// Plain editing, the mode a view starts in.
    static constexpr std::string_view EDITING = "editing";
    /// While a view's find is open: its bindings are looked up before the
    /// view's mode, and win over it.
    static constexpr std::string_view FINDING = "finding";
    /// While a view's completion list is open, the same: before find's.
    static constexpr std::string_view COMPLETING = "completing";
    /// A view with Vim on, in its normal or its insert mode: bindings
    /// there are looked up before Vim reads the keys, and win over it.
    static constexpr std::string_view VIM_NORMAL = "vim.normal";
    static constexpr std::string_view VIM_INSERT = "vim.insert";
    /// In any of Vim's visual modes, the same.
    static constexpr std::string_view VIM_VISUAL = "vim.visual";
    /// Completing with Vim on: as COMPLETING, but Escape is Vim's, which
    /// leaves insert mode and the list with it.
    static constexpr std::string_view VIM_COMPLETING = "vim.completing";

    /// fjedit's own keys, the ones VS Code has.
    [[nodiscard]] static Keymap standard();

    /// Makes `keys`, in `mode`, run `command`. A binding of the same keys
    /// made before is replaced.
    void bind(std::string_view mode, std::vector<Chord> keys, std::string command);

    /// Takes away the binding of `keys` in `mode`, if there is one.
    void unbind(std::string_view mode, std::span<const Chord> keys);

    /// What `pressed`, the keys pressed so far, comes to in `mode`. A whole
    /// binding wins over waiting for a longer one.
    [[nodiscard]] KeyMatch find(std::string_view mode, std::span<const Chord> pressed) const;

    /// The keys that run `command` in `mode`, for a menu or tooltip to show:
    /// the first binding made, or nothing.
    [[nodiscard]] std::optional<std::vector<Chord>> keys_for(std::string_view mode, std::string_view command) const;

private:
    struct Binding {
        std::string mode;
        std::vector<Chord> keys;
        std::string command;
    };

    std::vector<Binding> bindings_;
};

} // namespace fjedit
