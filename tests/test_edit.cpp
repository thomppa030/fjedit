#include "fjedit/core/buffer.hpp"

#include "print.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <random>
#include <string>
#include <vector>

using namespace fjedit;

static Change insert(Position at, std::string text) {
    return {.span = {at, at}, .text = std::move(text)};
}

static Change replace(Position from, Position to, std::string text) {
    return {.span = {from, to}, .text = std::move(text)};
}

TEST_CASE("Text goes in, comes out and is replaced within a line", "[edit]") {
    Buffer buffer("float speed;");
    (void)buffer.apply({{insert({0, 6}, "spin_")}});
    REQUIRE(buffer.text() == "float spin_speed;");

    (void)buffer.apply({{replace({0, 0}, {0, 5}, "double")}});
    REQUIRE(buffer.text() == "double spin_speed;");

    (void)buffer.apply({{replace({0, 7}, {0, 12}, "")}});
    REQUIRE(buffer.text() == "double speed;");
}

TEST_CASE("A break typed into a line splits it", "[edit]") {
    Buffer buffer("if (a) { b(); }");
    (void)buffer.apply({{insert({0, 8}, "\n    ")}});
    REQUIRE(buffer.line_count() == 2);
    REQUIRE(buffer.line(0) == "if (a) {");
    REQUIRE(buffer.line(1) == "     b(); }");
}

TEST_CASE("Deleting a break joins two lines", "[edit]") {
    Buffer buffer("first\nsecond\nthird");
    (void)buffer.apply({{replace({0, 5}, {1, 0}, " ")}});
    REQUIRE(buffer.text() == "first second\nthird");
}

TEST_CASE("Text replacing several lines with several lines", "[edit]") {
    Buffer buffer("a\nbbb\nccc\nd");
    (void)buffer.apply({{replace({1, 1}, {2, 2}, "X\nY\nZ")}});
    REQUIRE(buffer.text() == "a\nbX\nY\nZc\nd");
}

TEST_CASE("Breaks in new text are taken whatever their kind", "[edit]") {
    Buffer buffer("ab");
    (void)buffer.apply({{insert({0, 1}, "1\r\n2\r3\n")}});
    REQUIRE(buffer.line_count() == 4);
    REQUIRE(buffer.line(0) == "a1");
    REQUIRE(buffer.line(3) == "b");
}

TEST_CASE("Several changes on one line are made as one edit", "[edit]") {
    // Two cursors typing the same letter.
    Buffer buffer("a = b;\nc = d;");
    const Edit edit{{insert({0, 1}, "x"), insert({0, 5}, "x"), insert({1, 1}, "x")}};
    (void)buffer.apply(edit);
    REQUIRE(buffer.text() == "ax = bx;\ncx = d;");

    const std::vector<Span> after = spans_after(edit);
    REQUIRE(after == std::vector<Span>{{{0, 1}, {0, 2}}, {{0, 6}, {0, 7}}, {{1, 1}, {1, 2}}});
}

TEST_CASE("A change that adds lines moves the changes after it", "[edit]") {
    Buffer buffer("one two\nthree");
    const Edit edit{{insert({0, 3}, "\n\n"), replace({0, 4}, {0, 7}, "2"), insert({1, 0}, "3: ")}};
    (void)buffer.apply(edit);
    REQUIRE(buffer.text() == "one\n\n 2\n3: three");

    const std::vector<Span> after = spans_after(edit);
    REQUIRE(after[0] == Span{{0, 3}, {2, 0}});
    REQUIRE(after[1] == Span{{2, 1}, {2, 2}});
    REQUIRE(after[2] == Span{{3, 0}, {3, 3}});
}

TEST_CASE("Applying what an edit returns undoes it", "[edit]") {
    const std::string original = "void tick(float dt) {\n  time_ += dt;\n}\n";
    Buffer buffer(original);
    const Edit edit{{replace({0, 10}, {0, 15}, "double"), insert({1, 2}, "// step\n  "), replace({1, 5}, {2, 1}, "")}};
    const Edit undo = buffer.apply(edit);
    const std::string edited = buffer.text();
    REQUIRE(edited != original);

    const Edit redo = buffer.apply(undo);
    REQUIRE(buffer.text() == original);

    (void)buffer.apply(redo);
    REQUIRE(buffer.text() == edited);
}

TEST_CASE("Every edit moves the version on", "[edit]") {
    Buffer buffer("x");
    REQUIRE(buffer.version() == 0);
    const Edit undo = buffer.apply({{insert({0, 1}, "y")}});
    (void)buffer.apply(undo);
    REQUIRE(buffer.version() == 2);
}

TEST_CASE("Positions before a change stay, those after it move with it", "[edit][map]") {
    const Edit edit{{replace({1, 2}, {1, 4}, "LONGER")}};
    REQUIRE(map(edit, {0, 9}, Bias::After) == Position{0, 9});
    REQUIRE(map(edit, {1, 1}, Bias::After) == Position{1, 1});
    REQUIRE(map(edit, {1, 7}, Bias::Before) == Position{1, 11});
    REQUIRE(map(edit, {2, 3}, Bias::Before) == Position{2, 3});
}

TEST_CASE("Positions after a change that adds lines move down", "[edit][map]") {
    const Edit edit{{insert({0, 2}, "\nmiddle\n")}};
    REQUIRE(map(edit, {0, 5}, Bias::Before) == Position{2, 3});
    REQUIRE(map(edit, {1, 0}, Bias::Before) == Position{3, 0});
}

TEST_CASE("Where text goes in, the bias picks the side", "[edit][map]") {
    const Edit edit{{insert({0, 4}, "abc")}};
    REQUIRE(map(edit, {0, 4}, Bias::Before) == Position{0, 4});
    REQUIRE(map(edit, {0, 4}, Bias::After) == Position{0, 7});
}

TEST_CASE("A replaced span's start stays before it and its end follows it", "[edit][map]") {
    const Edit edit{{replace({0, 2}, {0, 6}, "x")}};
    REQUIRE(map(edit, {0, 2}, Bias::After) == Position{0, 2});
    REQUIRE(map(edit, {0, 6}, Bias::Before) == Position{0, 3});
    REQUIRE(map(edit, {0, 4}, Bias::Before) == Position{0, 2});
    REQUIRE(map(edit, {0, 4}, Bias::After) == Position{0, 3});
}

TEST_CASE("Text deleted from under a position leaves it where the text was", "[edit][map]") {
    const Edit edit{{replace({0, 3}, {2, 1}, "")}};
    REQUIRE(map(edit, {1, 4}, Bias::Before) == Position{0, 3});
    REQUIRE(map(edit, {1, 4}, Bias::After) == Position{0, 3});
    REQUIRE(map(edit, {2, 5}, Bias::Before) == Position{0, 7});
}

TEST_CASE("At a replaced span's end where the next change inserts, the bias decides", "[edit][map]") {
    const Edit edit{{replace({0, 0}, {0, 2}, "A"), insert({0, 2}, "BB")}};
    REQUIRE(map(edit, {0, 2}, Bias::Before) == Position{0, 1});
    REQUIRE(map(edit, {0, 2}, Bias::After) == Position{0, 3});
}

TEST_CASE("Two insertions at one place go in in order, and After passes both", "[edit][map]") {
    Buffer buffer("ab");
    const Edit edit{{insert({0, 1}, "1"), insert({0, 1}, "2")}};
    (void)buffer.apply(edit);
    REQUIRE(buffer.text() == "a12b");
    REQUIRE(map(edit, {0, 1}, Bias::Before) == Position{0, 1});
    REQUIRE(map(edit, {0, 1}, Bias::After) == Position{0, 3});
}

// Many random edits on a text with every kind of line, each undone again:
// the new text lands where spans_after() says, positions outside every
// change map onto the same characters, and undoing gives the text back.
TEST_CASE("Random edits land where they say and undo exactly", "[edit][random]") {
    std::mt19937 random(12345);
    auto below = [&](std::size_t n) { return static_cast<std::size_t>(random() % n); };
    const std::vector<std::string> pieces = {"a", "bc", "\n", "  ", "\xC3\xA4", "{}", "\n\n", "xyz", ""};

    for (int round = 0; round < 300; ++round) {
        std::string original;
        for (std::size_t i = below(40); i > 0; --i) original += pieces[below(pieces.size())];
        Buffer buffer(original);

        // Up to six spans, in order, apart, each on character boundaries.
        std::vector<Position> ends;
        for (std::size_t i = 2 * (1 + below(6)); i > 0; --i) {
            const std::size_t line = below(buffer.line_count());
            ends.push_back(buffer.clamp({line, below(buffer.line(line).size() + 1)}));
        }
        std::sort(ends.begin(), ends.end());
        Edit edit;
        for (std::size_t i = 0; i + 1 < ends.size(); i += 2) {
            std::string text;
            for (std::size_t k = below(4); k > 0; --k) text += pieces[below(pieces.size())];
            edit.changes.push_back({.span = {ends[i], ends[i + 1]}, .text = text});
        }

        // A character outside every change, to find again after.
        const Buffer before = buffer;
        const Edit undo = buffer.apply(edit);
        const std::vector<Span> after = spans_after(edit);
        for (std::size_t i = 0; i < edit.changes.size(); ++i) {
            REQUIRE(buffer.text(after[i]) == Buffer(edit.changes[i].text).text());
        }
        for (std::size_t line = 0; line < before.line_count(); ++line) {
            for (std::size_t byte = 0; byte < before.line(line).size(); ++byte) {
                const Position at{line, byte};
                const bool changed = std::any_of(edit.changes.begin(), edit.changes.end(), [&](const Change& c) {
                    return c.span.from <= at && at < c.span.to;
                });
                if (changed || before.clamp(at) != at) continue;
                const Position now = map(edit, at, Bias::After);
                REQUIRE(buffer.line(now.line).substr(now.byte, 1) == before.line(line).substr(byte, 1));
            }
        }

        (void)buffer.apply(undo);
        REQUIRE(buffer.text() == original);
    }
}
