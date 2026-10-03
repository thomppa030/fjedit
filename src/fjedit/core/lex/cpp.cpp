// C and C++'s words, on the shared C-like lexer, with raw strings, string
// prefixes and attributes.

#include "fjedit/core/lex/c_like.hpp"
#include "fjedit/core/lex/lexers.hpp"

#include <algorithm>
#include <array>

namespace fjedit::lex {

namespace {

constexpr auto KEYWORDS = std::to_array<std::string_view>({
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch",
    "char", "char16_t", "char32_t", "char8_t", "class", "co_await", "co_return", "co_yield", "compl", "concept",
    "const", "const_cast", "consteval", "constexpr", "constinit", "continue", "decltype", "default", "delete", "do",
    "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "final", "float", "for",
    "friend", "goto", "if", "import", "inline", "int", "long", "module", "mutable", "namespace", "new", "noexcept",
    "not", "not_eq", "nullptr", "operator", "or", "or_eq", "override", "private", "protected", "public", "register",
    "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast",
    "struct", "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename",
    "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
});
static_assert(std::ranges::is_sorted(KEYWORDS));

} // namespace

const Lexer& cpp() {
    static const CLikeLexer lexer({.keywords = KEYWORDS, .cpp_strings = true, .attributes = true});
    return lexer;
}

} // namespace fjedit::lex
