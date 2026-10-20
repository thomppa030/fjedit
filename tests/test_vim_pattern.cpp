#include "fjedit/core/vim/pattern.hpp"

#include "print.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace fjedit;

namespace {

using Texts = std::vector<std::string>;

// The texts `pattern` matches in `text`, in order. Each expectation is what
// Vim's search finds.
Texts found(std::string_view text, std::string_view pattern, vim::PatternSettings settings = {}) {
    const Buffer buffer(text);
    const ftk::Result<vim::Pattern> read = vim::read_pattern(pattern, settings);
    REQUIRE(read);
    Texts out;
    for (const Span& match : find_all(buffer, read->regex, read->options)) out.push_back(buffer.text(match));
    return out;
}

// The error reading `pattern` gives; empty when it reads.
std::string refused(std::string_view pattern) {
    const ftk::Result<vim::Pattern> read = vim::read_pattern(pattern);
    return read ? std::string() : read.error();
}

} // namespace

TEST_CASE("Magic: what wants a backslash and what is itself", "[vim][pattern]") {
    CHECK(found("a+b aab", "a\\+b") == Texts{"aab"});
    CHECK(found("a+b aab", "a+b") == Texts{"a+b"});
    CHECK(found("color colour", "colou\\=r") == Texts{"color", "colour"});
    CHECK(found("color colour", "colou\\?r") == Texts{"color", "colour"});
    CHECK(found("ab abab", "\\(ab\\)\\{2}") == Texts{"abab"});
    CHECK(found("x(1) y", "(1)") == Texts{"(1)"});
    CHECK(found("foo bar baz", "bar\\|baz") == Texts{"bar", "baz"});
    CHECK(found("a.c abc", "a\\.c") == Texts{"a.c"});
    CHECK(found("a.c abc", "a.c") == Texts{"a.c", "abc"});
    CHECK(found("a{2} aa", "a{2}") == Texts{"a{2}"});
    CHECK(found("a/b", "a\\/b") == Texts{"a/b"});
}

TEST_CASE("Counts in braces, lazily after a minus", "[vim][pattern]") {
    CHECK(found("aaa", "a\\{-1,}") == Texts{"a", "a", "a"});
    CHECK(found("aaaa", "a\\{,2}") == Texts{"aa", "aa"});
    CHECK(found("aaaa", "a\\{3,}") == Texts{"aaaa"});
    CHECK(found("ab", "a\\{}b") == Texts{"ab"});
    CHECK(found("aaab", "a\\{2,3\\}b") == Texts{"aaab"});
}

TEST_CASE("A word's start and end, and Vim's classes", "[vim][pattern]") {
    CHECK(found("cat concat cats", "\\<cat\\>") == Texts{"cat"});
    CHECK(found("cat concat cats", "\\<cat") == Texts{"cat", "cat"});
    CHECK(found("a1 b22 c", "\\a\\d\\+") == Texts{"a1", "b22"});
    CHECK(found("x\ty z", "\\s") == Texts{"\t", " "});
    CHECK(found("Foo_bar 9x", "\\h\\w*") == Texts{"Foo_bar", "x"});
    CHECK(found("aB", "\\u") == Texts{"B"});
    CHECK(found("aB", "\\l") == Texts{"a"});
    CHECK(found("0x1f g", "\\x\\+") == Texts{"0", "1f"});
}

TEST_CASE("Very magic, magic, nomagic and very nomagic switch what is special, even part way", "[vim][pattern]") {
    CHECK(found("ab abab", "\\v(ab){2}") == Texts{"abab"});
    CHECK(found("foo bar", "\\vfoo|bar") == Texts{"foo", "bar"});
    CHECK(found("cat cats", "\\v<cat>") == Texts{"cat"});
    CHECK(found("a.c abc", "\\Ma.c") == Texts{"a.c"});
    CHECK(found("a.c abc", "\\Ma\\.c") == Texts{"a.c", "abc"});
    CHECK(found("a*b aab", "\\Va*b") == Texts{"a*b"});
    CHECK(found("a.b axb", "\\Va.b") == Texts{"a.b"});
    CHECK(found("x+y xxy", "\\vx+\\my") == Texts{"xxy"});
}

TEST_CASE("^ and $ hold only where Vim reads them; a quantifier at the start is itself", "[vim][pattern]") {
    CHECK(found("a^ b", "a^") == Texts{"a^"});
    CHECK(found("ab\nb", "^b") == Texts{"b"});
    CHECK(found("a$b ab", "a$b") == Texts{"a$b"});
    CHECK(found("ab\nab", "b$") == Texts{"b", "b"});
    CHECK(found("ab\nab", "\\(b$\\)") == Texts{"b", "b"});
    CHECK(found("*a", "*a") == Texts{"*a"});
}

TEST_CASE("Case follows ignorecase and smartcase, unless the pattern says", "[vim][pattern]") {
    CHECK(found("Spin spin", "spin") == Texts{"spin"});
    CHECK(found("Spin spin", "spin", {.ignore_case = true, .smart_case = false}) == Texts{"Spin", "spin"});
    CHECK(found("Spin spin", "Spin", {.ignore_case = true, .smart_case = true}) == Texts{"Spin"});
    CHECK(found("Spin spin", "spin", {.ignore_case = true, .smart_case = true}) == Texts{"Spin", "spin"});
    CHECK(found("Spin spin", "\\cspin") == Texts{"Spin", "spin"});
    CHECK(found("Spin spin", "\\Cspin", {.ignore_case = true, .smart_case = false}) == Texts{"spin"});
    // A letter after a backslash is no upper case letter for smartcase.
    CHECK(found("Ab ab", "\\Sb", {.ignore_case = true, .smart_case = true}) == Texts{"Ab", "ab"});
}

TEST_CASE("Where zs and ze stand, the match begins and ends", "[vim][pattern]") {
    CHECK(found("foobar foobaz", "foo\\zsbar") == Texts{"bar"});
    CHECK(found("foobar foobaz", "foo\\zebar") == Texts{"foo"});
    CHECK(found("foobar", "f\\zsoo\\zebar") == Texts{"oo"});
    // Vim's groups keep their numbers past the group \zs makes.
    const Buffer buffer("foo");
    const ftk::Result<vim::Pattern> read = vim::read_pattern("\\(f\\)\\zs\\(o\\+\\)");
    REQUIRE(read);
    const std::vector<Span> matches = find_all(buffer, read->regex, read->options);
    REQUIRE(matches == std::vector<Span>{{{0, 1}, {0, 3}}});
    CHECK(vim::captured(buffer, matches[0], *read) == std::vector<std::string>{"oo", "f", "oo"});
}

TEST_CASE("Collections, a closing bracket first and an opening one never closed taken as themselves", "[vim][pattern]") {
    CHECK(found("a] b", "[]a]") == Texts{"a", "]"});
    CHECK(found("a-b", "[a\\-]") == Texts{"a", "-"});
    CHECK(found("x1y", "[[:digit:]]") == Texts{"1"});
    CHECK(found("a\tb", "[\\t]") == Texts{"\t"});
    CHECK(found("[x", "[x") == Texts{"[x"});
    CHECK(found("abc", "[^b]") == Texts{"a", "c"});
    CHECK(found("a!b", "[\\x21]") == Texts{"!"});
}

TEST_CASE("A back reference matches what its group took; a letter past ASCII is whole", "[vim][pattern]") {
    CHECK(found("aa ab", "\\(a\\)\\1") == Texts{"aa"});
    CHECK(found("\xC3\xA4\xC3\xA4 \xC3\xA4", "\xC3\xA4\\+") == Texts{"\xC3\xA4\xC3\xA4", "\xC3\xA4"});
}

TEST_CASE("What find can't do is refused, worded as Vim words it", "[vim][pattern]") {
    CHECK_FALSE(refused("a\\nb").empty());
    CHECK_FALSE(refused("\\_s").empty());
    CHECK_FALSE(refused("\\%5l").empty());
    CHECK_FALSE(refused("\\(a\\)\\@=").empty());
    CHECK(refused("\\(a").starts_with("E54"));
    CHECK(refused("a\\)").starts_with("E55"));
    CHECK(refused("~").starts_with("E33"));
    CHECK(refused("a**").starts_with("E61"));
    CHECK(refused("\\1").starts_with("E65"));
    CHECK(refused("a\\{x}").starts_with("E554"));
    CHECK(refused("a\\zsb\\|c") != "");
    CHECK(refused("\\%(a\\|b\\)c").empty());
}

TEST_CASE("A replacement puts in what was taken and changes its case", "[vim][pattern]") {
    const std::vector<std::string> taken{"Spin speed", "spin", "speed"};
    CHECK(vim::expand_replacement("\\2 \\1", taken) == "speed spin");
    CHECK(vim::expand_replacement("[&]", taken) == "[Spin speed]");
    CHECK(vim::expand_replacement("[\\0]", taken) == "[Spin speed]");
    CHECK(vim::expand_replacement("\\u\\1", taken) == "Spin");
    CHECK(vim::expand_replacement("\\U\\1\\E-\\2", taken) == "SPIN-speed");
    CHECK(vim::expand_replacement("\\L&", taken) == "spin speed");
    CHECK(vim::expand_replacement("a\\rb\\tc", taken) == "a\nb\tc");
    CHECK(vim::expand_replacement("\\&\\\\", taken) == "&\\");
    CHECK(vim::expand_replacement("~!", taken, "before") == "before!");
    CHECK(vim::expand_replacement("\\~", taken, "before") == "~");
}
