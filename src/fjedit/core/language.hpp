#pragma once

// The languages fjedit knows, and which a file is written in by its name.

#include "fjedit/core/structure.hpp"
#include "fjedit/core/syntax.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace fjedit {

struct Language {
    /// What the status bar shows: "C++", "FJSL".
    std::string_view name;
    const Lexer& lexer;
    /// What begins a comment that runs to the end of the line; empty when
    /// the language has none.
    std::string_view line_comment;
    Folding folding{Folding::Brackets};
    /// What a word takes besides letters, digits, `_` and bytes past ASCII,
    /// as completion reads the word being typed: FJSS's names run through
    /// `-` (`justify-content`, `--accent`).
    std::string_view word_also{};
    /// The quotes typing closes as it closes brackets ("\"'"); none where
    /// the text has no strings.
    std::string_view quotes{};
    /// Words a string's opening quote may follow as part of the string, a
    /// C++ prefix (`u8"…"`, `R"(…)"`): a quote typed after one closes as
    /// one typed after a space does.
    std::span<const std::string_view> string_prefixes{};
};

/// Whether `c` belongs to a word of `language`.
[[nodiscard]] bool word_byte(const Language& language, char c);

/// The language `file_name` is written in, by its extension (case aside);
/// plain text for one fjedit doesn't know.
[[nodiscard]] const Language& language_for(std::string_view file_name);

/// The extensions of the files fjedit reads as `language`, lower case and
/// without their dot ("frag", "glsl"); none for plain text.
[[nodiscard]] std::vector<std::string_view> extensions_of(const Language& language);

/// Text with no colours.
[[nodiscard]] const Language& plain_text();

} // namespace fjedit
