#include "fjedit/core/motion.hpp"

#include "print.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fjedit;

TEST_CASE("Characters are spaces, word or punctuation", "[motion]") {
    REQUIRE(char_kind(' ') == CharKind::Space);
    REQUIRE(char_kind('\t') == CharKind::Space);
    REQUIRE(char_kind('a') == CharKind::Word);
    REQUIRE(char_kind('Z') == CharKind::Word);
    REQUIRE(char_kind('7') == CharKind::Word);
    REQUIRE(char_kind('_') == CharKind::Word);
    REQUIRE(char_kind(0xE4) == CharKind::Word);
    REQUIRE(char_kind(':') == CharKind::Punctuation);
    REQUIRE(char_kind('(') == CharKind::Punctuation);
}

TEST_CASE("A step is one character, across lines at their ends", "[motion]") {
    const Buffer buffer("a\xC3\xA4" "b\nc");
    REQUIRE(char_right(buffer, {0, 1}) == Position{0, 3});
    REQUIRE(char_left(buffer, {0, 3}) == Position{0, 1});
    REQUIRE(char_right(buffer, {0, 4}) == Position{1, 0});
    REQUIRE(char_left(buffer, {1, 0}) == Position{0, 4});
    REQUIRE(char_right(buffer, {1, 1}) == Position{1, 1});
    REQUIRE(char_left(buffer, {0, 0}) == Position{0, 0});
}

TEST_CASE("Words end at the end of a run, after the spaces before it", "[motion]") {
    const Buffer buffer("  auto pos = transform().position;");
    REQUIRE(word_right(buffer, {0, 0}) == Position{0, 6});
    REQUIRE(word_right(buffer, {0, 6}) == Position{0, 10});
    REQUIRE(word_right(buffer, {0, 10}) == Position{0, 12});
    REQUIRE(word_right(buffer, {0, 22}) == Position{0, 25});
    REQUIRE(word_right(buffer, {0, 25}) == Position{0, 33});
}

TEST_CASE("Words begin at the start of a run, before the spaces after it", "[motion]") {
    const Buffer buffer("  auto pos = transform().position;");
    REQUIRE(word_left(buffer, {0, 34}) == Position{0, 33});
    REQUIRE(word_left(buffer, {0, 33}) == Position{0, 25});
    REQUIRE(word_left(buffer, {0, 25}) == Position{0, 22});
    REQUIRE(word_left(buffer, {0, 11}) == Position{0, 7});
    REQUIRE(word_left(buffer, {0, 6}) == Position{0, 2});
    REQUIRE(word_left(buffer, {0, 2}) == Position{0, 0});
}

TEST_CASE("Moving by words crosses lines at their ends", "[motion]") {
    const Buffer buffer("one\n  two");
    REQUIRE(word_right(buffer, {0, 3}) == Position{1, 0});
    REQUIRE(word_right(buffer, {1, 0}) == Position{1, 5});
    REQUIRE(word_left(buffer, {1, 0}) == Position{0, 3});
}

TEST_CASE("Letters past ASCII are part of words", "[motion]") {
    const Buffer buffer("gr\xC3\xB6\xC3\x9F" "e x");
    REQUIRE(word_right(buffer, {0, 0}) == Position{0, 7});
    REQUIRE(word_left(buffer, {0, 7}) == Position{0, 0});
}

TEST_CASE("Home goes to the first character, then to the very start", "[motion]") {
    const Buffer buffer("    return a;");
    REQUIRE(line_start(buffer, {0, 9}) == Position{0, 4});
    REQUIRE(line_start(buffer, {0, 4}) == Position{0, 0});
    REQUIRE(line_start(buffer, {0, 0}) == Position{0, 4});
    REQUIRE(line_end(buffer, {0, 2}) == Position{0, 13});
}

TEST_CASE("Up and down find the nearest place along the line", "[motion]") {
    const Buffer buffer("long line here\nab\n\tx\nlast line");
    const MonospaceLayout layout(4);
    REQUIRE(lines_down(buffer, layout, {0, 9}, 9.0f, 1) == Position{1, 2});
    REQUIRE(lines_down(buffer, layout, {1, 2}, 9.0f, 1) == Position{2, 2});
    REQUIRE(lines_down(buffer, layout, {0, 0}, 4.0f, 2) == Position{2, 1});
    REQUIRE(lines_down(buffer, layout, {0, 0}, 1.0f, 2) == Position{2, 0});
    REQUIRE(lines_up(buffer, layout, {3, 9}, 9.0f, 3) == Position{0, 9});
}

TEST_CASE("Up from the first line and down from the last reach the text's ends", "[motion]") {
    const Buffer buffer("first\nlast");
    const MonospaceLayout layout;
    REQUIRE(lines_up(buffer, layout, {0, 3}, 3.0f, 1) == Position{0, 0});
    REQUIRE(lines_down(buffer, layout, {1, 1}, 1.0f, 1) == Position{1, 4});
    REQUIRE(lines_down(buffer, layout, {0, 1}, 1.0f, 20) == Position{1, 4});
}

TEST_CASE("A monospace layout counts cells, tabs to the next stop", "[motion]") {
    const Buffer buffer("\tab\xC3\xA4");
    const MonospaceLayout layout(4);
    REQUIRE(layout.x_of(buffer, {0, 0}) == 0.0f);
    REQUIRE(layout.x_of(buffer, {0, 1}) == 4.0f);
    REQUIRE(layout.x_of(buffer, {0, 3}) == 6.0f);
    REQUIRE(layout.x_of(buffer, {0, 5}) == 7.0f);
    REQUIRE(layout.position_at(buffer, 0, 1.9f) == Position{0, 0});
    REQUIRE(layout.position_at(buffer, 0, 2.0f) == Position{0, 1});
    REQUIRE(layout.position_at(buffer, 0, 6.4f) == Position{0, 3});
    REQUIRE(layout.position_at(buffer, 0, 99.0f) == Position{0, 5});
}
