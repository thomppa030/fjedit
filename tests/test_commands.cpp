#include "view.hpp"

#include <vector>

using namespace fjedit;

TEST_CASE("Every command has a name and a title, and names are unique", "[commands]") {
    for (const Command& command : commands()) {
        REQUIRE_FALSE(command.name.empty());
        REQUIRE_FALSE(command.title.empty());
        REQUIRE(find_command(command.name) == &command);
    }
    REQUIRE(find_command("caret.nowhere") == nullptr);
}

TEST_CASE("A test view shows what it was made from", "[commands]") {
    for (const char* marked : {"|", "a|b\nc|d", "[abc] d", "x [y] |z", "[ab][cd]"}) {
        const TestView view(marked);
        REQUIRE(view.shown() == marked);
    }
}

TEST_CASE("Carets move by characters, words and lines", "[commands]") {
    TestView view("|int spin_speed = 90;\nint points = 10;");
    view.run("caret.word_right");
    REQUIRE(view.shown() == "int| spin_speed = 90;\nint points = 10;");
    view.run("caret.right");
    view.run("caret.line_end");
    REQUIRE(view.carets() == std::vector<Position>{{0, 20}});
    view.run("caret.right");
    REQUIRE(view.carets() == std::vector<Position>{{1, 0}});
    view.run("caret.text_end");
    REQUIRE(view.carets() == std::vector<Position>{{1, 16}});
    view.run("caret.text_start");
    REQUIRE(view.carets() == std::vector<Position>{{0, 0}});
}

TEST_CASE("Selecting keeps the anchor, moving drops it", "[commands]") {
    TestView view("|alpha beta");
    view.run("select.word_right");
    REQUIRE(view.shown() == "[alpha] beta");
    view.run("select.right");
    REQUIRE(view.shown() == "[alpha ]beta");
    view.run("caret.word_right");
    REQUIRE(view.shown() == "alpha beta|");
}

TEST_CASE("Left and right leave a selection at its own end first", "[commands]") {
    TestView forward("a[bcd]ef");
    forward.run("caret.left");
    REQUIRE(forward.shown() == "a|bcdef");

    TestView backward("abcdef");
    backward.selections = Selections({.anchor = {0, 4}, .caret = {0, 1}});
    backward.run("caret.right");
    REQUIRE(backward.shown() == "abcd|ef");
}

TEST_CASE("A caret moving up and down comes back to its goal past short lines", "[commands]") {
    TestView view("a long l|ine\nab\nanother long one");
    view.run("caret.down");
    REQUIRE(view.carets() == std::vector<Position>{{1, 2}});
    view.run("caret.down");
    REQUIRE(view.carets() == std::vector<Position>{{2, 8}});

    // Any other move forgets the goal.
    view.run("caret.left");
    view.run("caret.up");
    view.run("caret.up");
    REQUIRE(view.carets() == std::vector<Position>{{0, 7}});
}

TEST_CASE("A page moves as many lines as the layout shows", "[commands]") {
    TestView view("|0\n1\n2\n3\n4\n5\n6");
    view.run("caret.page_down");
    REQUIRE(view.carets() == std::vector<Position>{{3, 0}});
    view.run("select.page_down");
    REQUIRE(view.selections.primary().span() == Span{{3, 0}, {6, 0}});
    // Up from the selection's start, which is its end on that side.
    view.run("caret.page_up");
    REQUIRE(view.carets() == std::vector<Position>{{0, 0}});
}

TEST_CASE("Every caret moves, and carets that meet become one", "[commands]") {
    TestView view("a|b\nc|d");
    view.run("caret.right");
    REQUIRE(view.shown() == "ab|\ncd|");
    view.run("caret.text_start");
    REQUIRE(view.shown() == "|ab\ncd");
}
