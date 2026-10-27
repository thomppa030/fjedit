#pragma once

// The colour each kind of token is drawn in (sheet 39, K1): the theme's
// code tokens, the code view's and the minimap's alike.

#include "fjedit/core/syntax.hpp"

#include "ftk/ui/theme.hpp"

namespace fjedit {

[[nodiscard]] inline ftk::ui::Color code_colour(TokenKind kind) {
    namespace theme = ftk::theme;
    switch (kind) {
    case TokenKind::Text: return theme::text();
    case TokenKind::Keyword: return theme::code_keyword();
    case TokenKind::Type: return theme::code_type();
    case TokenKind::Function: return theme::code_function();
    case TokenKind::String: return theme::code_string();
    case TokenKind::Number: return theme::code_number();
    case TokenKind::Comment: return theme::code_comment();
    case TokenKind::Meta: return theme::code_meta();
    case TokenKind::Punctuation:
    case TokenKind::Bracket: return theme::code_punctuation();
    case TokenKind::Member:
    case TokenKind::Uniform: return theme::code_member();
    case TokenKind::Builtin: return theme::code_builtin();
    }
    return theme::text();
}

} // namespace fjedit
