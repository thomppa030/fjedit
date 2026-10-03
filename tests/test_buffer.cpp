#include "fjedit/core/buffer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace fjedit;

static std::vector<std::string> lines(const Buffer& buffer) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < buffer.line_count(); ++i) out.emplace_back(buffer.line(i));
    return out;
}

TEST_CASE("An empty buffer is one empty line", "[buffer]") {
    const Buffer made;
    REQUIRE(lines(made) == std::vector<std::string>{""});
    REQUIRE(made.text().empty());
    REQUIRE(made.end() == Position{0, 0});

    const Buffer read("");
    REQUIRE(lines(read) == std::vector<std::string>{""});
}

TEST_CASE("Lines are split at their breaks and lose them", "[buffer]") {
    const Buffer buffer("int a;\n  a += 1;\nreturn a;");
    REQUIRE(lines(buffer) == std::vector<std::string>{"int a;", "  a += 1;", "return a;"});
    REQUIRE(buffer.end() == Position{2, 9});
}

TEST_CASE("A text ending in a break has an empty last line", "[buffer]") {
    const Buffer buffer("one\ntwo\n");
    REQUIRE(lines(buffer) == std::vector<std::string>{"one", "two", ""});
    REQUIRE(buffer.text() == "one\ntwo\n");
}

TEST_CASE("A file's line breaks come back as they were", "[buffer]") {
    const Buffer lf("a\nb\n");
    REQUIRE(lf.line_ending() == LineEnding::Lf);
    REQUIRE(lf.text() == "a\nb\n");

    const Buffer crlf("a\r\nb\r\n");
    REQUIRE(lines(crlf) == std::vector<std::string>{"a", "b", ""});
    REQUIRE(crlf.line_ending() == LineEnding::CrLf);
    REQUIRE(crlf.text() == "a\r\nb\r\n");
}

TEST_CASE("Mixed breaks come back as the one the file had most of", "[buffer]") {
    const Buffer buffer("a\r\nb\r\nc\nd");
    REQUIRE(lines(buffer) == std::vector<std::string>{"a", "b", "c", "d"});
    REQUIRE(buffer.line_ending() == LineEnding::CrLf);
    REQUIRE(buffer.text() == "a\r\nb\r\nc\r\nd");
}

TEST_CASE("A lone carriage return breaks a line", "[buffer]") {
    const Buffer buffer("a\rb\nc");
    REQUIRE(lines(buffer) == std::vector<std::string>{"a", "b", "c"});
    REQUIRE(buffer.line_ending() == LineEnding::Lf);
}

TEST_CASE("The line ending can be changed for writing", "[buffer]") {
    Buffer buffer("a\nb");
    buffer.set_line_ending(LineEnding::CrLf);
    REQUIRE(buffer.text() == "a\r\nb");
}

TEST_CASE("A byte order mark is kept apart and given back", "[buffer]") {
    const Buffer buffer("\xEF\xBB\xBF" "first\nsecond");
    REQUIRE(buffer.byte_order_mark());
    REQUIRE(buffer.line(0) == "first");
    REQUIRE(buffer.text() == "\xEF\xBB\xBF" "first\nsecond");

    REQUIRE_FALSE(Buffer("first").byte_order_mark());
}

TEST_CASE("A span's text joins its lines with a newline", "[buffer]") {
    const Buffer buffer("alpha\r\nbeta\r\ngamma");
    REQUIRE(buffer.text({{0, 1}, {0, 4}}) == "lph");
    REQUIRE(buffer.text({{0, 3}, {2, 2}}) == "ha\nbeta\nga");
    REQUIRE(buffer.text({{1, 4}, {2, 0}}) == "\n");
    REQUIRE(buffer.text({{1, 2}, {1, 2}}).empty());
}

TEST_CASE("Positions are ordered by line, then byte", "[buffer]") {
    REQUIRE(Position{0, 9} < Position{1, 0});
    REQUIRE(Position{2, 3} < Position{2, 4});
    REQUIRE(Span{{1, 2}, {1, 2}}.empty());
}

TEST_CASE("Clamping puts a position back into the text", "[buffer]") {
    const Buffer buffer("short\nlonger line");
    REQUIRE(buffer.clamp({0, 99}) == Position{0, 5});
    REQUIRE(buffer.clamp({7, 3}) == Position{1, 3});
    REQUIRE(buffer.clamp({9, 99}) == buffer.end());
    REQUIRE(buffer.clamp({1, 4}) == Position{1, 4});
}

TEST_CASE("Clamping never leaves a position inside a character", "[buffer]") {
    // "ä" and "€" are two and three bytes: the position must land before them.
    const Buffer buffer("a\xC3\xA4" "b\xE2\x82\xAC" "c");
    REQUIRE(buffer.clamp({0, 2}) == Position{0, 1});
    REQUIRE(buffer.clamp({0, 3}) == Position{0, 3});
    REQUIRE(buffer.clamp({0, 5}) == Position{0, 4});
    REQUIRE(buffer.clamp({0, 6}) == Position{0, 4});
    REQUIRE(buffer.clamp({0, 7}) == Position{0, 7});
}
