#include "fjedit/core/find.hpp"

#include "print.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace fjedit;

TEST_CASE("Every place a word occurs, in order", "[find]") {
    const Buffer buffer("speed = spin_speed * speed;\nspeed");
    REQUIRE(find_all(buffer, "speed") ==
            std::vector<Span>{{{0, 0}, {0, 5}}, {{0, 13}, {0, 18}}, {{0, 21}, {0, 26}}, {{1, 0}, {1, 5}}});
}

TEST_CASE("Matches don't overlap: the search goes on from a match's end", "[find]") {
    REQUIRE(find_all(Buffer("aaaa"), "aa") == std::vector<Span>{{{0, 0}, {0, 2}}, {{0, 2}, {0, 4}}});
    REQUIRE(find_all(Buffer("aaa"), "aa") == std::vector<Span>{{{0, 0}, {0, 2}}});
}

TEST_CASE("Whole words don't match inside longer ones", "[find]") {
    const Buffer buffer("pos = position + pos_2 + pos");
    REQUIRE(find_all(buffer, "pos", {.whole_word = true}) == std::vector<Span>{{{0, 0}, {0, 3}}, {{0, 25}, {0, 28}}});
    REQUIRE(find_all(buffer, "pos").size() == 4);
}

TEST_CASE("Case can be ignored, for A to Z", "[find]") {
    const Buffer buffer("Tick TICK tick");
    REQUIRE(find_all(buffer, "tick").size() == 1);
    REQUIRE(find_all(buffer, "tick", {.match_case = false}).size() == 3);
}

TEST_CASE("A needle across lines matches across lines", "[find]") {
    const Buffer buffer("a {\n}\nb {\n}");
    REQUIRE(find_all(buffer, "{\n}") == std::vector<Span>{{{0, 2}, {1, 1}}, {{2, 2}, {3, 1}}});
    REQUIRE(find_all(buffer, "\nb") == std::vector<Span>{{{1, 1}, {2, 1}}});
}

TEST_CASE("An empty needle occurs nowhere", "[find]") {
    REQUIRE(find_all(Buffer("abc"), "").empty());
    REQUIRE_FALSE(find_next(Buffer("abc"), "x", {0, 0}).has_value());
}

TEST_CASE("The next match goes round to the start", "[find]") {
    const Buffer buffer("ab ab ab");
    REQUIRE(find_next(buffer, "ab", {0, 1}) == Span{{0, 3}, {0, 5}});
    REQUIRE(find_next(buffer, "ab", {0, 7}) == Span{{0, 0}, {0, 2}});
}

TEST_CASE("Matches start on whole characters", "[find]") {
    // The second byte of "ä" is 0xA4, and so is the needle's only byte.
    REQUIRE(find_all(Buffer("\xC3\xA4"), "\xA4").empty());
}

TEST_CASE("The word at a place is the run of word characters around it", "[find]") {
    const Buffer buffer("  spin_speed * 2;");
    REQUIRE(word_at(buffer, {0, 5}) == Span{{0, 2}, {0, 12}});
    REQUIRE(word_at(buffer, {0, 2}) == Span{{0, 2}, {0, 12}});
    REQUIRE(word_at(buffer, {0, 12}) == Span{{0, 2}, {0, 12}});
    REQUIRE(word_at(buffer, {0, 13}).empty());
}
