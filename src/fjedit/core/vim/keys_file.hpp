#pragma once

// keys.vim read (decision 7 of the Vim plan): the maps one makes oneself in
// Vim's map syntax, fjedit's own commands bound to keys with `bind`, Vim's
// settings set, and the leader. Nothing in it runs here: the engine plays
// the maps, the keymap takes the binds, and the program applies the
// settings. A line it can't read is an error with its number, and the rest
// still read.
//
//     const fjedit::vim::KeysFile keys = fjedit::vim::read_keys_file(R"(
//         let mapleader = " "
//         nnoremap <leader>w <Cmd>w<CR>
//         xnoremap < <gv
//         bind normal <leader>fg files.find
//     )");

#include "fjedit/core/keymap.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit::vim {

/// The modes a map is made in, as Vim's map commands name them: `n`, `x`
/// (and `v`: fjedit has no select mode), `o` while an operator waits for
/// its motion, `i`, and `c` on the command line. A set of them, or'd.
enum class MapModes : uint8_t {
    None = 0,
    Normal = 1,
    Visual = 2,
    OperatorPending = 4,
    Insert = 8,
    Command = 16,
};
[[nodiscard]] constexpr MapModes operator|(MapModes a, MapModes b) {
    return static_cast<MapModes>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
/// Whether `set` holds any of `modes`.
[[nodiscard]] constexpr bool any_of(MapModes set, MapModes modes) {
    return (static_cast<uint8_t>(set) & static_cast<uint8_t>(modes)) != 0;
}

/// What a map does, part by part: keys played into the engine, or a `:`
/// line run, as `<Cmd>…<CR>` writes one.
struct MapStep {
    std::vector<Chord> keys;
    /// What `<Cmd>` ran to its `<CR>`; empty for keys.
    std::string command;
    bool is_command{false};

    bool operator==(const MapStep&) const = default;
};

/// One map: the keys that make it in its modes, and what it does (no steps
/// for `<Nop>`). A recursive one (`map`, not `noremap`) plays its keys
/// through the maps again. One `<nowait>` runs without waiting for a longer
/// map its keys begin.
struct Mapping {
    MapModes modes{MapModes::None};
    std::vector<Chord> keys;
    std::vector<MapStep> does;
    bool recursive{false};
    bool waits{true};
    std::size_t line{0};
};

/// A `bind` line: fjedit's `command` run by `keys` in the keymap mode
/// `mode` names: one of Vim's (`normal`, `visual`, `insert`, `command`) or
/// fjedit's own (`completing`, `finding`, `program`). The program checks
/// the command has that name.
struct Bind {
    std::string mode;
    std::vector<Chord> keys;
    std::string command;
    std::size_t line{0};
};

/// A `set` line, what follows `set` as the `:` line's :set takes it.
struct SetLine {
    std::string argument;
    std::size_t line{0};
};

/// A line that couldn't be read, by its number from 1.
struct KeysError {
    std::size_t line{0};
    std::string message;

    bool operator==(const KeysError&) const = default;
};

struct KeysFile {
    std::vector<Mapping> maps;
    std::vector<Bind> binds;
    std::vector<SetLine> settings;
    std::vector<KeysError> errors;
};

/// `text`, a keys.vim, read line by line. `<leader>` is the leader in force
/// at its line (Space until `let mapleader` says otherwise), as Vim reads
/// it when the map is made.
[[nodiscard]] KeysFile read_keys_file(std::string_view text);

} // namespace fjedit::vim
