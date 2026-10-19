#include "fjedit/core/vim/motions.hpp"

#include "fjedit/core/document.hpp"
#include "fjedit/core/language.hpp"

#include "print.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fjedit;
using namespace fjedit::vim;

// Each expectation is what Vim does on the same text.

TEST_CASE("w goes to the next word's start, punctuation a word of its own", "[vim][motion]") {
    const Buffer buffer("foo.bar baz");
    CHECK(word_start_after(buffer, {0, 0}, 1, false) == Position{0, 3});
    CHECK(word_start_after(buffer, {0, 3}, 1, false) == Position{0, 4});
    CHECK(word_start_after(buffer, {0, 4}, 1, false) == Position{0, 8});
    CHECK(word_start_after(buffer, {0, 0}, 3, false) == Position{0, 8});
    // W: anything not blank is one WORD.
    CHECK(word_start_after(buffer, {0, 0}, 1, true) == Position{0, 8});
}

TEST_CASE("w stops on an empty line, and at the text's end", "[vim][motion]") {
    const Buffer lines("a\n\nb");
    CHECK(word_start_after(lines, {0, 0}, 1, false) == Position{1, 0});
    CHECK(word_start_after(lines, {1, 0}, 1, false) == Position{2, 0});
    // On the last word, the text's end: normal mode puts the caret back on
    // its last character.
    const Buffer last("foo bar");
    CHECK(word_start_after(last, {0, 4}, 1, false) == Position{0, 7});
    CHECK(word_start_after(last, {0, 6}, 1, false) == Position{0, 7});
}

TEST_CASE("For an operator, w on a line's last word stops at its end", "[vim][motion]") {
    const Buffer buffer("foo bar\nbaz");
    CHECK(word_start_after(buffer, {0, 4}, 1, false, true) == Position{0, 7});
    CHECK(word_start_after(buffer, {0, 4}, 1, false) == Position{1, 0});
    // Only the last count stops: 2 words on from foo reaches baz's line.
    CHECK(word_start_after(buffer, {0, 0}, 2, false, true) == Position{0, 7});
    CHECK(word_start_after(buffer, {0, 0}, 3, false, true) == Position{1, 3});
}

TEST_CASE("e goes to the end of this word, or of the next from a word's end", "[vim][motion]") {
    const Buffer buffer("foo bar");
    CHECK(word_end_after(buffer, {0, 0}, 1, false) == Position{0, 2});
    CHECK(word_end_after(buffer, {0, 2}, 1, false) == Position{0, 6});
    CHECK(word_end_after(buffer, {0, 0}, 2, false) == Position{0, 6});
    // As cw has it: on a word's last character, it stays.
    CHECK(word_end_after(buffer, {0, 2}, 1, false, true) == Position{0, 2});
    CHECK(word_end_after(buffer, {0, 1}, 1, false, true) == Position{0, 2});
    // E: to the end of the WORD.
    CHECK(word_end_after(Buffer("a.b c"), {0, 0}, 1, true) == Position{0, 2});
    CHECK(word_end_after(Buffer("a.b c"), {0, 0}, 1, false) == Position{0, 1});
}

TEST_CASE("b goes to this word's start, or the one before, stopping on empty lines", "[vim][motion]") {
    const Buffer buffer("foo bar");
    CHECK(word_start_before(buffer, {0, 4}, 1, false) == Position{0, 0});
    CHECK(word_start_before(buffer, {0, 5}, 1, false) == Position{0, 4});
    CHECK(word_start_before(buffer, {0, 6}, 2, false) == Position{0, 0});
    CHECK(word_start_before(buffer, {0, 0}, 1, false) == Position{0, 0});
    const Buffer lines("a\n\nb");
    CHECK(word_start_before(lines, {2, 0}, 1, false) == Position{1, 0});
    CHECK(word_start_before(lines, {1, 0}, 1, false) == Position{0, 0});
    CHECK(word_start_before(Buffer("x a.b"), {0, 4}, 1, true) == Position{0, 2});
}

TEST_CASE("ge goes to the end of the word before", "[vim][motion]") {
    const Buffer buffer("foo bar");
    CHECK(word_end_before(buffer, {0, 6}, 1, false) == Position{0, 2});
    CHECK(word_end_before(buffer, {0, 4}, 1, false) == Position{0, 2});
    CHECK(word_end_before(Buffer("a\n\nb"), {2, 0}, 1, false) == Position{1, 0});
}

TEST_CASE("^ and $ go to a line's first text and its last character", "[vim][motion]") {
    const Buffer buffer("    x = 1;\n\n   ");
    CHECK(first_non_blank(buffer, 0) == Position{0, 4});
    CHECK(last_character(buffer, 0) == Position{0, 9});
    CHECK(last_character(buffer, 1) == Position{1, 0});
    CHECK(first_non_blank(buffer, 2) == Position{2, 3});
    CHECK(last_character(Buffer("a\xC3\xA4"), 0) == Position{0, 1});
}

TEST_CASE("} and { go to the empty lines between paragraphs", "[vim][motion]") {
    const Buffer buffer("a\nb\n\nc\n\nd");
    CHECK(paragraph_after(buffer, {0, 0}, 1) == Moved{{2, 0}, MotionKind::Exclusive});
    CHECK(paragraph_after(buffer, {2, 0}, 1) == Moved{{4, 0}, MotionKind::Exclusive});
    CHECK(paragraph_after(buffer, {0, 0}, 2) == Moved{{4, 0}, MotionKind::Exclusive});
    // Past the last, onto the last line's last character, taking it.
    CHECK(paragraph_after(buffer, {4, 0}, 1) == Moved{{5, 0}, MotionKind::Inclusive});
    CHECK(paragraph_before(buffer, {5, 0}, 1) == Moved{{4, 0}, MotionKind::Exclusive});
    CHECK(paragraph_before(buffer, {4, 0}, 1) == Moved{{2, 0}, MotionKind::Exclusive});
    CHECK(paragraph_before(buffer, {1, 0}, 1) == Moved{{0, 0}, MotionKind::Exclusive});
    // More paragraphs asked for than there are: nothing moves.
    CHECK(paragraph_after(buffer, {0, 0}, 5) == Moved{{0, 0}, MotionKind::Exclusive});
}

TEST_CASE("% goes to the bracket paired with the first one on along the line", "[vim][motion]") {
    const Document code("f(a[1], b);", language_for("x.cpp"));
    CHECK(paired_bracket(code.buffer(), code.syntax(), {0, 0}) == Position{0, 9});
    CHECK(paired_bracket(code.buffer(), code.syntax(), {0, 3}) == Position{0, 5});
    CHECK(paired_bracket(code.buffer(), code.syntax(), {0, 9}) == Position{0, 1});
    CHECK_FALSE(paired_bracket(code.buffer(), code.syntax(), {0, 10}));
    // A bracket in a string is passed over.
    const Document quoted("x = \"(\" + (y);", language_for("x.cpp"));
    CHECK(paired_bracket(quoted.buffer(), quoted.syntax(), {0, 0}) == Position{0, 12});
}

TEST_CASE("f and t find a character on along the line, F and T back", "[vim][motion]") {
    const Buffer buffer("a,b,c,d");
    const CharSearch f{.target = ",", .forward = true, .till = false};
    const CharSearch t{.target = ",", .forward = true, .till = true};
    const CharSearch big_f{.target = ",", .forward = false, .till = false};
    const CharSearch big_t{.target = ",", .forward = false, .till = true};
    CHECK(find_in_line(buffer, {0, 0}, f, 1) == Position{0, 1});
    CHECK(find_in_line(buffer, {0, 0}, f, 2) == Position{0, 3});
    CHECK_FALSE(find_in_line(buffer, {0, 0}, f, 4));
    CHECK(find_in_line(buffer, {0, 6}, big_f, 1) == Position{0, 5});
    CHECK(find_in_line(buffer, {0, 6}, big_f, 2) == Position{0, 3});
    // A till right beside its match stays put; ; and , look past it.
    CHECK(find_in_line(buffer, {0, 0}, t, 1) == Position{0, 0});
    CHECK(find_in_line(buffer, {0, 0}, t, 1, true) == Position{0, 2});
    CHECK(find_in_line(buffer, {0, 6}, big_t, 1) == Position{0, 6});
    CHECK(find_in_line(buffer, {0, 6}, big_t, 1, true) == Position{0, 4});
    // Characters past ASCII are found whole.
    const Buffer wide("a\xC3\xA4" "b");
    CHECK(find_in_line(wide, {0, 0}, {.target = "b"}, 1) == Position{0, 3});
    CHECK(find_in_line(wide, {0, 0}, {.target = "\xC3\xA4"}, 1) == Position{0, 1});
    CHECK(find_in_line(wide, {0, 0}, {.target = "b", .till = true}, 1) == Position{0, 1});
}

TEST_CASE("h and l keep to the line", "[vim][motion]") {
    const Buffer buffer("abc\nd");
    CHECK(left_in_line(buffer, {0, 1}, 5) == Position{0, 0});
    CHECK(right_in_line(buffer, {0, 1}, 5) == Position{0, 3});
    const Buffer wide("a\xC3\xA4" "b");
    CHECK(right_in_line(wide, {0, 0}, 1) == Position{0, 1});
    CHECK(right_in_line(wide, {0, 0}, 2) == Position{0, 3});
    CHECK(left_in_line(wide, {0, 3}, 1) == Position{0, 1});
}
