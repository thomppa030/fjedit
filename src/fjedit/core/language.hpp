#pragma once

// The languages fjedit knows, and which a file is written in by its name.

#include "fjedit/core/syntax.hpp"

#include <string_view>

namespace fjedit {

struct Language {
    /// What the status bar shows: "C++", "FJSL".
    std::string_view name;
    const Lexer& lexer;
    /// What begins a comment that runs to the end of the line; empty when
    /// the language has none.
    std::string_view line_comment;
};

/// The language `file_name` is written in, by its extension (case aside);
/// plain text for one fjedit doesn't know.
[[nodiscard]] const Language& language_for(std::string_view file_name);

/// Text with no colours.
[[nodiscard]] const Language& plain_text();

} // namespace fjedit
