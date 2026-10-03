#pragma once

// Each language's lexer. Inside fjedit-core only: a program finds them
// through language_for().

#include "fjedit/core/syntax.hpp"

namespace fjedit::lex {

/// C and C++.
[[nodiscard]] const Lexer& cpp();

} // namespace fjedit::lex
