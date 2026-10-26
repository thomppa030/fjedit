#pragma once

// Each language's lexer. Inside fjedit-core only: a program finds them
// through language_for().

#include "fjedit/core/syntax.hpp"

namespace fjedit::lex {

/// C and C++.
[[nodiscard]] const Lexer& cpp();

/// GLSL, and FJSL: GLSL with fjell's shader words and hints.
[[nodiscard]] const Lexer& glsl();
[[nodiscard]] const Lexer& fjsl();

/// FJSS stylesheets.
[[nodiscard]] const Lexer& fjss();

/// Widget markup.
[[nodiscard]] const Lexer& markup();

/// JSON, comments allowed.
[[nodiscard]] const Lexer& json();

} // namespace fjedit::lex
