#pragma once

// Keys in Vim's notation, as its maps and help write them: a character as
// itself, a key that types none by its name in angle brackets, modifiers
// before the name (`ciw<Esc>`, `<C-r>`, `<S-Tab>`, `<Space>w`). What the
// keys file is written in, and what the engine shows of a command half
// typed.
//
//     const auto keys = fjedit::vim::parse_notation("d2w<Esc>");

#include "fjedit/core/keymap.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit::vim {

/// `chord` in Vim's notation: a character typed as itself, `<Space>` and
/// `<lt>` for a space and a `<`; a key by its name in brackets, with `C-`,
/// `S-`, `A-` and `D-` (Super) before it, letters in lower case (`<C-r>`,
/// `<A-j>`, `<CR>`, `<Esc>`).
[[nodiscard]] std::string notation(const Chord& chord);

/// The chords `text` writes in Vim's notation. Names in brackets go by any
/// case (`<esc>`, `<C-R>`); `<Space>`, `<lt>`, `<Bar>` and `<Bslash>` are
/// those characters typed, unless a modifier comes with them. A `<` that
/// opens no name it knows is the character. Nothing when a name in brackets
/// has a modifier it doesn't know or no name after its modifiers.
[[nodiscard]] std::optional<std::vector<Chord>> parse_notation(std::string_view text);

} // namespace fjedit::vim
