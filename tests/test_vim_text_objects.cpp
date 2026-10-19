#include "fjedit/core/vim/text_objects.hpp"

#include "fjedit/core/document.hpp"
#include "fjedit/core/language.hpp"

#include "print.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

using namespace fjedit;

namespace {

// The text `name` takes at `at` in `text`, as C++, `count` of it; "-" when
// it takes none. Each expectation is what Vim takes on the same text.
std::string taken(std::string_view text, Position at, std::string_view name, std::size_t count = 1) {
    const Document document(text, language_for("x.cpp"));
    const std::optional<vim::Taken> object = vim::text_object(document.buffer(), document.syntax(), at, name, count);
    if (!object) return "-";
    return document.buffer().text(object->span);
}

} // namespace

TEST_CASE("iw takes the word or the blanks at the caret; aw the blanks after too", "[vim][text-object]") {
    CHECK(taken("foo bar baz", {0, 5}, "iw") == "bar");
    CHECK(taken("foo bar baz", {0, 5}, "aw") == "bar ");
    // With no blanks after, those before.
    CHECK(taken("foo bar", {0, 5}, "aw") == " bar");
    // On blanks: the blanks; with aw, the word after them too.
    CHECK(taken("a   b c", {0, 2}, "iw") == "   ");
    CHECK(taken("a   b c", {0, 2}, "aw") == "   b");
    // A count goes on over blanks and words in turn.
    CHECK(taken("foo bar", {0, 0}, "iw", 2) == "foo ");
    CHECK(taken("foo.bar", {0, 1}, "iw") == "foo");
    CHECK(taken("x a.b y", {0, 3}, "iW") == "a.b");
    // An indent is never taken with the word after it.
    CHECK(taken("  foo", {0, 3}, "aw") == "foo");
}

TEST_CASE("i( takes what a pair of brackets holds, a( the brackets too", "[vim][text-object]") {
    CHECK(taken("f(a, b)", {0, 3}, "i(") == "a, b");
    CHECK(taken("f(a, b)", {0, 3}, "a(") == "(a, b)");
    CHECK(taken("f(a, b)", {0, 3}, "ib") == "a, b");
    // On a bracket, its own pair.
    CHECK(taken("f(a, b)", {0, 1}, "i(") == "a, b");
    CHECK(taken("f(a, b)", {0, 6}, "i)") == "a, b");
    CHECK(taken("f()", {0, 1}, "i(").empty());
    CHECK(taken("f(a(b))", {0, 4}, "i(", 2) == "a(b)");
    CHECK(taken("v[i + 1]", {0, 3}, "i[") == "i + 1");
    CHECK(taken("a<b, c>d", {0, 3}, "i<") == "b, c");
    // Outside every pair, or with fewer pairs than asked: none.
    CHECK(taken("f(a) b", {0, 5}, "i(") == "-");
    CHECK(taken("f(a)", {0, 2}, "i(", 2) == "-");
    // A bracket in a string doesn't count.
    CHECK(taken("f(\")\", a)", {0, 7}, "i(") == "\")\", a");
}

TEST_CASE("i{ over lines leaves the braces' lines; a{ takes them", "[vim][text-object]") {
    const std::string_view function = "void f() {\n    x();\n}";
    CHECK(taken(function, {1, 5}, "i{") == "    x();\n");
    CHECK(taken(function, {1, 5}, "a{") == "{\n    x();\n}");
    CHECK(taken(function, {1, 5}, "iB") == "    x();\n");
    // An indented closer's indent is left with it.
    CHECK(taken("{\n  a;\n  }", {1, 2}, "i{") == "  a;\n");
    // From the indent of the opener's line, its block.
    CHECK(taken("  {\n  a;\n  }", {0, 0}, "i{") == "  a;\n");
}

TEST_CASE("i\" takes what a pair of quotes on the line holds, a\" the quotes and blanks after", "[vim][text-object]") {
    const std::string_view say = "say \"hi there\" now";
    CHECK(taken(say, {0, 6}, "i\"") == "hi there");
    CHECK(taken(say, {0, 6}, "a\"") == "\"hi there\" ");
    // On either quote, the pair it belongs to.
    CHECK(taken(say, {0, 4}, "i\"") == "hi there");
    CHECK(taken(say, {0, 13}, "i\"") == "hi there");
    // Before the first quote: the first quoted text after.
    CHECK(taken("x = \"a\";", {0, 0}, "i\"") == "a");
    // With nothing after, the blanks before.
    CHECK(taken("x \"a\"", {0, 3}, "a\"") == " \"a\"");
    // Two of an i: the quotes, no blanks.
    CHECK(taken(say, {0, 6}, "i\"", 2) == "\"hi there\"");
    // A quote escaped is passed.
    CHECK(taken("\"a\\\"b\"", {0, 1}, "i\"") == "a\\\"b");
    CHECK(taken("'x'", {0, 1}, "i'") == "x");
    CHECK(taken("no quotes", {0, 1}, "i\"") == "-");
}

TEST_CASE("ip takes the paragraph or the blank lines at the caret, ap the blank lines after too", "[vim][text-object]") {
    const std::string_view text = "a\nb\n\nc";
    CHECK(taken(text, {0, 0}, "ip") == "a\nb");
    CHECK(taken(text, {0, 0}, "ap") == "a\nb\n");
    // With no blank lines after, ap takes those before.
    CHECK(taken(text, {3, 0}, "ap") == "\nc");
    // On blank lines, the blank lines.
    CHECK(taken("a\n\n  \nb", {1, 0}, "ip") == "\n  ");
    const Document document(text, language_for("x.cpp"));
    const std::optional<vim::Taken> lines = vim::text_object(document.buffer(), document.syntax(), {0, 0}, "ip", 1);
    REQUIRE(lines);
    CHECK(lines->linewise);
}

TEST_CASE("A name that is no text object takes nothing", "[vim][text-object]") {
    CHECK(taken("abc", {0, 1}, "iq") == "-");
    CHECK(taken("abc", {0, 1}, "w") == "-");
    CHECK(taken("abc", {0, 1}, "iw", 0) == "-");
}
