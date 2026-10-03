#include "view.hpp"

using namespace fjedit;

TEST_CASE("Select all takes the whole text", "[select]") {
    TestView view("a|b\nc|d");
    view.run("select.all");
    REQUIRE(view.shown() == "[ab\ncd]");
}

TEST_CASE("Select line takes the whole line, and the next each time again", "[select]") {
    TestView view("one\nt|wo\nthree");
    view.run("select.line");
    REQUIRE(view.shown() == "one\n[two\n]three");
    view.run("select.line");
    REQUIRE(view.shown() == "one\n[two\nthree]");
}

TEST_CASE("The next match first takes the word at each caret", "[select]") {
    TestView view("sp|in = spin + spin;");
    view.run("select.next_match");
    REQUIRE(view.shown() == "[spin] = spin + spin;");
}

TEST_CASE("The next match adds the next place the text occurs, round to the start", "[select]") {
    TestView view("spin = spin + [spin];");
    view.run("select.next_match");
    REQUIRE(view.shown() == "[spin] = spin + [spin];");
    REQUIRE(view.selections.primary().span() == Span{{0, 0}, {0, 4}});
    view.run("select.next_match");
    REQUIRE(view.shown() == "[spin] = [spin] + [spin];");
    view.run("select.next_match");
    REQUIRE(view.selections.size() == 3);
}

TEST_CASE("Every match from a caret takes whole words like the one there", "[select]") {
    TestView view("p|os = position + pos;");
    view.run("select.all_matches");
    REQUIRE(view.shown() == "[pos] = position + [pos];");
}

TEST_CASE("Every match from a selection takes the text wherever it occurs", "[select]") {
    TestView view("[pos] = position;");
    view.run("select.all_matches");
    REQUIRE(view.shown() == "[pos] = [pos]ition;");
}

TEST_CASE("Cursors are added above and below, as far along", "[select]") {
    TestView view("abcdef\nab\nabc|def\nabcdef");
    view.run("cursor.add_above");
    REQUIRE(view.shown() == "abcdef\nab|\nabc|def\nabcdef");
    view.run("cursor.add_above");
    REQUIRE(view.shown() == "abc|def\nab|\nabc|def\nabcdef");
    view.run("cursor.add_above");
    REQUIRE(view.selections.size() == 3);
    view.run("cursor.add_below");
    REQUIRE(view.shown() == "abc|def\nab|\nabc|def\nabc|def");
}

TEST_CASE("Escape leaves one cursor, then drops the selection", "[select]") {
    TestView view("[ab] [cd]");
    view.run("cursor.single");
    REQUIRE(view.shown() == "ab [cd]");
    view.run("cursor.single");
    REQUIRE(view.shown() == "ab cd|");
}

TEST_CASE("A column takes the same stretch of each line, past short ones", "[select]") {
    const Buffer buffer("abcdef\na\nabcdef\nabcd");
    const MonospaceLayout layout;
    const Selections column = column_selection(buffer, layout, 0, 2.0f, 3, 4.0f);
    REQUIRE(std::vector<Selection>(column.all().begin(), column.all().end()) ==
            std::vector<Selection>{{.anchor = {0, 2}, .caret = {0, 4}},
                                   {.anchor = {2, 2}, .caret = {2, 4}},
                                   {.anchor = {3, 2}, .caret = {3, 4}}});
    REQUIRE(column.primary().caret == Position{3, 4});
}

TEST_CASE("A line reaching just to the column gets a caret at its end", "[select]") {
    const Buffer buffer("abcdef\nab\nabcdef");
    const Selections column = column_selection(buffer, MonospaceLayout(), 0, 2.0f, 2, 4.0f);
    REQUIRE(column.all()[1] == Selection::at({1, 2}));
}
