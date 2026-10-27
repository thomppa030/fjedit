#pragma once

// What the code says on a status bar (sheets 39 and 47, S2), as data: Vim's
// mode and what it has to say, the problems in the files open, what went
// wrong or was just done, and where the caret is in the file in front and
// how that file is written. A program draws it on a bar of its own, with
// the two parts each bar shows drawn alike by the functions here.
//
//     const CodeStatus status = code.status();
//     draw_vim_status(status);      // after the bar's own items on the left
//     bar.right();
//     draw_place_status(status);

#include "fjedit/core/language_services.hpp"
#include "fjedit/core/structure.hpp"
#include "fjedit/core/vim/engine.hpp"
#include "fjedit/core/vim/keys_file.hpp"

#include "ftk/ui/kit/status_bar.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace fjedit {

struct CodeStatus {
    /// With Vim on in the view in front: its mode as the pill names it, and
    /// the pill's tone (sheet 47, M1); the register a macro is recorded
    /// into; the keys of a command half typed, in Vim's notation; what the
    /// last key had to say; and the first line of keys.vim that couldn't be
    /// used.
    struct Vim {
        std::string mode{};
        ftk::ui::StatusTone tone{ftk::ui::StatusTone::Plain};
        std::optional<char> recording{};
        std::string keys{};
        vim::Message said{};
        std::optional<vim::KeysError> keys_error{};
    };
    std::optional<Vim> vim{};

    /// How many problems the files open have, given once the file in front
    /// has a language service or there are any.
    struct Problems {
        std::size_t errors{0};
        std::size_t warnings{0};
    };
    std::optional<Problems> problems{};
    /// The first of the worst problems in the file in front: its message's
    /// first line, and its file and line ("coin.cpp:26").
    struct Problem {
        std::string message{};
        std::string where{};
    };
    std::optional<Problem> first_problem{};

    /// What went wrong last; empty once something works.
    std::string note{};
    /// What was just done (a rename), until the next key.
    std::string told{};
    /// While a colour is picked from its swatch, what Esc and Enter do.
    std::optional<std::string> hint{};

    /// The file in front: the caret's line and column, counted from 1, the
    /// column in cells as the indent's width lays tabs out; how the file is
    /// indented, whether it begins with a byte order mark, its language,
    /// and each language service it asks with how that service is.
    struct Place {
        std::size_t line{1};
        std::size_t column{1};
        Indent indent{};
        bool byte_order_mark{false};
        std::string language{};
        std::vector<ServiceShown> services{};
    };
    std::optional<Place> place{};
};

/// Draws Vim's part of `status` on the status bar being drawn: the mode's
/// pill, the macro recorded, the keys typed in the mono face, what the last
/// key said and keys.vim's first problem, then a rule. Nothing with Vim off.
void draw_vim_status(const CodeStatus& status);

/// Draws where the caret is on the status bar being drawn: its line and
/// column, how the file is indented and its encoding when
/// `indent_and_encoding`, its language, and a dot per language service.
/// Nothing with no file in front.
void draw_place_status(const CodeStatus& status, bool indent_and_encoding = true);

} // namespace fjedit
