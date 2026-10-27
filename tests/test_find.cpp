#include "fjedit/core/find.hpp"

#include "print.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <vector>

using namespace fjedit;

TEST_CASE("Every place a word occurs, in order", "[find]") {
    const Buffer buffer("speed = spin_speed * speed;\nspeed");
    REQUIRE(find_all(buffer, "speed") ==
            std::vector<Span>{{{0, 0}, {0, 5}}, {{0, 13}, {0, 18}}, {{0, 21}, {0, 26}}, {{1, 0}, {1, 5}}});
}

TEST_CASE("A regular expression's match can be one of its groups; what each group took is told", "[find]") {
    const Buffer buffer("foobar x=12");
    const FindOptions group{.match_case = true, .whole_word = false, .regex = true, .group = 1};
    REQUIRE(find_all(buffer, "foo(bar)", group) == std::vector<Span>{{{0, 3}, {0, 6}}});
    REQUIRE(replacement(buffer, {{0, 3}, {0, 6}}, "foo(bar)", "[$1]", group) == "[bar]");
    const FindOptions regex{.match_case = true, .whole_word = false, .regex = true, .group = 0};
    REQUIRE(captured(buffer, {{0, 7}, {0, 11}}, "(\\w)=(\\d+)", regex) == std::vector<std::string>{"x=12", "x", "12"});
    REQUIRE_FALSE(captured(buffer, {{0, 0}, {0, 3}}, "(\\w)=(\\d+)", regex));
}

TEST_CASE("A regular expression's matches that take no text count when asked to", "[find]") {
    const Buffer buffer("ab\ncd");
    const FindOptions none{.match_case = true, .whole_word = false, .regex = true, .group = 0, .empty = false};
    const FindOptions empty{.match_case = true, .whole_word = false, .regex = true, .group = 0, .empty = true};
    REQUIRE(find_all(buffer, "^", none).empty());
    REQUIRE(find_all(buffer, "^", empty) == std::vector<Span>{{{0, 0}, {0, 0}}, {{1, 0}, {1, 0}}});
    REQUIRE(find_all(buffer, "$", empty) == std::vector<Span>{{{0, 2}, {0, 2}}, {{1, 2}, {1, 2}}});
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

TEST_CASE("Ignoring case, a match may begin with its first letter in either case", "[find]") {
    const Buffer buffer("aAaAbAab");
    REQUIRE(find_all(buffer, "Ab", {.match_case = false}) == std::vector<Span>{{{0, 3}, {0, 5}}, {{0, 6}, {0, 8}}});
    REQUIRE(find_all(Buffer("_X _x"), "_x", {.match_case = false}).size() == 2);
}

TEST_CASE("A match across lines may begin on the line the one before ends on", "[find]") {
    REQUIRE(find_all(Buffer("ax\nyx\ny"), "x\ny") == std::vector<Span>{{{0, 1}, {1, 1}}, {{1, 1}, {2, 1}}});
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

TEST_CASE("A search within lines finds only what begins on them", "[find]") {
    const Buffer buffer("ab\nab\nab\nab");
    REQUIRE(find_in_lines(buffer, "ab", 1, 3) == std::vector<Span>{{{1, 0}, {1, 2}}, {{2, 0}, {2, 2}}});
    REQUIRE(find_in_lines(buffer, "b\na", 2, 9) == std::vector<Span>{{{2, 1}, {3, 1}}});
    REQUIRE(find_in_lines(buffer, "ab", 3, 3).empty());
}

TEST_CASE("A regular expression matches within each line", "[find]") {
    const Buffer buffer("int spin_speed = 90;\nfloat Speed = 1.5;\nspeed();");
    const FindOptions regex{.regex = true};
    REQUIRE(find_all(buffer, "[0-9]+(\\.[0-9]+)?", regex) == std::vector<Span>{{{0, 17}, {0, 19}}, {{1, 14}, {1, 17}}});
    REQUIRE(find_all(buffer, "^speed", regex) == std::vector<Span>{{{2, 0}, {2, 5}}});
    REQUIRE(find_all(buffer, "speed", {.match_case = false, .regex = true}).size() == 3);
    // A whole word: spin_speed's speed carries on a word.
    REQUIRE(find_all(buffer, "[sS]peed", {.whole_word = true, .regex = true}) ==
            std::vector<Span>{{{1, 6}, {1, 11}}, {{2, 0}, {2, 5}}});
}

TEST_CASE("Matches that take no text don't count, and a bad expression finds nothing", "[find]") {
    const Buffer buffer("baa\nb");
    REQUIRE(find_all(buffer, "a*", {.regex = true}) == std::vector<Span>{{{0, 1}, {0, 3}}});
    REQUIRE_FALSE(regex_valid("(a"));
    REQUIRE(regex_valid("(a)"));
    REQUIRE(find_all(buffer, "(a", {.regex = true}).empty());
}

TEST_CASE("A replacement puts in what a regular expression captured", "[find]") {
    const Buffer buffer("set(a, 1); set(b, 2);");
    const FindOptions regex{.regex = true};
    const std::string_view needle = "set\\((\\w+), (\\d)\\)";
    const std::vector<Span> found = find_all(buffer, needle, regex);
    REQUIRE(found.size() == 2);
    REQUIRE(replacement(buffer, found[1], needle, "$1 = $2", regex) == "b = 2");
    REQUIRE(replacement(buffer, found[0], needle, "[$&]", regex) == "[set(a, 1)]");
    // Text is put in as it is.
    REQUIRE(replacement(buffer, found[0], "set", "$1", {}) == "$1");
}

namespace {

// Checks find_in_text() finds in `text` what find_all() finds in a Buffer
// of it, with the same lines.
void check_as_lines(std::string_view text, std::string_view needle, FindOptions options) {
    const Buffer buffer(text);
    std::vector<TextMatch> lines;
    for (const Span& span : find_all(buffer, needle, options)) lines.push_back({span, buffer.line(span.from.line)});
    CAPTURE(text, needle, options.match_case, options.whole_word, options.regex);
    CHECK(find_in_text(text, needle, options) == lines);
}

} // namespace

TEST_CASE("A text searched as its bytes gives what its lines give", "[find]") {
    const std::string_view texts[] = {
        "speed = spin_speed * speed;\nspeed",
        "a\r\nspeed\rb Speed\n\nspeed\r\n",
        "\xEF\xBB\xBF"
        "speed x\nspeed",
        "\x80speed\n\xC3speed\xC3\n\xC3\xA4speed speed_x",
        "aaaa\naa\r\naaa",
        "x {\n}\r\ny {\r}\n",
        "",
        "\n\r\n\r",
    };
    const std::string_view needles[] = {"speed", "SPEED", "aa", "d\ns", "{\n}", "\n", "e\r", "a\naa", "\xA4", "x"};
    const std::string_view expressions[] = {"s\\w+d", "^", "a+", "[^ ]+$", "(a"};
    const FindOptions plain[] = {
        {}, {.match_case = false}, {.whole_word = true}, {.match_case = false, .whole_word = true}};
    const FindOptions regex[] = {{.regex = true},
                                 {.match_case = false, .regex = true},
                                 {.whole_word = true, .regex = true},
                                 {.regex = true, .empty = true}};
    for (const std::string_view text : texts) {
        for (const std::string_view needle : needles) {
            for (const FindOptions& options : plain) check_as_lines(text, needle, options);
        }
        for (const std::string_view expression : expressions) {
            for (const FindOptions& options : regex) check_as_lines(text, expression, options);
        }
    }
}

TEST_CASE("A text's matches come with their lines, broken where a buffer breaks them", "[find]") {
    REQUIRE(find_in_text("\xEF\xBB\xBFint a;\r\nspeed\rb speed\n", "speed") ==
            std::vector<TextMatch>{{{{1, 0}, {1, 5}}, "speed"}, {{{2, 2}, {2, 7}}, "b speed"}});
}

TEST_CASE("A search of a text stops at its limit", "[find]") {
    REQUIRE(find_in_text("x x x x", "x", {}, 2).size() == 2);
    REQUIRE(find_in_text("x\nx\nx", "x", {.regex = true}, 2).size() == 2);
    REQUIRE(find_in_text("x\nx\nx", "x\n", {}, 1).size() == 1);
    REQUIRE(find_in_text("x x", "x", {}, 0).empty());
}
