#include "view.hpp"

using namespace fjedit;

TEST_CASE("Typing goes in at every caret and is one step", "[editing]") {
    TestView view("a|b\nc|d");
    view.type("XY");
    REQUIRE(view.shown() == "aXY|b\ncXY|d");
    view.run("edit.undo");
    REQUIRE(view.shown() == "a|b\nc|d");
    view.run("edit.redo");
    REQUIRE(view.shown() == "aXY|b\ncXY|d");
}

TEST_CASE("Typing replaces what is selected, and undo brings it back", "[editing]") {
    TestView view("[foo] = [bar];");
    view.type("xy");
    REQUIRE(view.shown() == "xy| = xy|;");
    view.run("edit.undo");
    REQUIRE(view.shown() == "[foo] = [bar];");
}

TEST_CASE("A new line splits the line at each caret", "[editing]") {
    TestView view("ab|cd");
    view.run("edit.newline");
    REQUIRE(view.shown() == "ab\n|cd");
}

TEST_CASE("Backspace deletes a character, or the break at a line's start", "[editing]") {
    TestView view("a|bc\n|de");
    view.run("edit.backspace");
    REQUIRE(view.shown() == "|bc|de");
}

TEST_CASE("Backspace takes whole characters", "[editing]") {
    TestView view("x\xC3\xA4|y");
    view.run("edit.backspace");
    REQUIRE(view.shown() == "x|y");
}

TEST_CASE("Deletions that overlap are made as one", "[editing]") {
    TestView view("foo bar|baz|");
    view.run("edit.delete_word_left");
    REQUIRE(view.shown() == "foo |");
}

TEST_CASE("Delete takes the character after, or joins the next line", "[editing]") {
    TestView view("ab|\ncd");
    view.run("edit.delete");
    REQUIRE(view.shown() == "ab|cd");
    view.run("edit.delete_word_right");
    REQUIRE(view.shown() == "ab|");
}

TEST_CASE("Deleting with a selection deletes the selection", "[editing]") {
    TestView view("a[bc]d");
    view.run("edit.delete");
    REQUIRE(view.shown() == "a|d");
}

TEST_CASE("Deleting lines leaves the caret on the line after, as far along", "[editing]") {
    TestView middle("one\nt|wo\nthree");
    middle.run("edit.delete_lines");
    REQUIRE(middle.shown() == "one\nt|hree");

    TestView last("one\ntw|o");
    last.run("edit.delete_lines");
    REQUIRE(last.shown() == "on|e");

    TestView only("a|bc");
    only.run("edit.delete_lines");
    REQUIRE(only.shown() == "|");
}

TEST_CASE("Tab within a line indents to the next stop", "[editing]") {
    TestView spaces("ab|c");
    spaces.run("edit.tab");
    REQUIRE(spaces.shown() == "ab  |c");

    TestView tabs("ab|c");
    tabs.indent.tabs = true;
    tabs.run("edit.tab");
    REQUIRE(tabs.shown() == "ab\t|c");
}

TEST_CASE("Tab across lines indents every line with text", "[editing]") {
    TestView view("[a\n\nb]");
    view.run("edit.tab");
    REQUIRE(view.shown() == "[    a\n\n    b]");
}

TEST_CASE("Outdent takes a level of indent off each line", "[editing]") {
    TestView one("    a|\n\tb");
    one.run("edit.outdent");
    REQUIRE(one.shown() == "a|\n\tb");

    TestView both("[    a\n\tb  ]");
    both.run("edit.outdent");
    REQUIRE(both.shown() == "[a\nb  ]");
}

TEST_CASE("Lines move up and down, the selections with them", "[editing]") {
    TestView view("one\ntw|o\nthree");
    view.run("edit.move_lines_down");
    REQUIRE(view.shown() == "one\nthree\ntw|o");
    view.run("edit.move_lines_down");
    REQUIRE(view.shown() == "one\nthree\ntw|o");
    view.run("edit.move_lines_up");
    view.run("edit.move_lines_up");
    REQUIRE(view.shown() == "tw|o\none\nthree");
    view.run("edit.undo");
    REQUIRE(view.shown() == "one\ntw|o\nthree");
}

TEST_CASE("Lines are copied below with the selection, or above without it", "[editing]") {
    TestView down("a|b\nc");
    down.run("edit.copy_lines_down");
    REQUIRE(down.shown() == "ab\na|b\nc");

    TestView up("x\na|b\nc");
    up.run("edit.copy_lines_up");
    REQUIRE(up.shown() == "x\na|b\nab\nc");
}

TEST_CASE("A new line above or below keeps the line's indent", "[editing]") {
    TestView below("  ab|cd");
    below.run("edit.insert_line_below");
    REQUIRE(below.shown() == "  abcd\n  |");

    TestView above("  ab|cd");
    above.run("edit.insert_line_above");
    REQUIRE(above.shown() == "  |\n  abcd");
}

TEST_CASE("Copy and paste the selection", "[editing]") {
    TestView view("[abc] d|");
    view.run("edit.copy");
    REQUIRE(view.clipboard_text == "abc");
    view.selections = Selections(Selection::at({0, 5}));
    view.run("edit.paste");
    REQUIRE(view.shown() == "abc dabc|");
}

TEST_CASE("Cut with nothing selected takes the whole line", "[editing]") {
    TestView view("one\nt|wo\nthree");
    view.run("edit.cut");
    REQUIRE(view.clipboard_text == "two\n");
    REQUIRE(view.shown() == "one\nt|hree");
}

TEST_CASE("Cut takes the selection", "[editing]") {
    TestView view("a[bc]d");
    view.run("edit.cut");
    REQUIRE(view.clipboard_text == "bc");
    REQUIRE(view.shown() == "a|d");
}

TEST_CASE("Pasting as many lines as there are carets puts one at each", "[editing]") {
    TestView view("|\n|\n|");
    view.clipboard_text = "red\ngreen\nblue\n";
    view.run("edit.paste");
    REQUIRE(view.shown() == "red|\ngreen|\nblue|");
}

TEST_CASE("Pasting other text puts all of it at every caret", "[editing]") {
    TestView view("a|\nb|");
    view.clipboard_text = "1\n2\n3";
    view.run("edit.paste");
    REQUIRE(view.shown() == "a1\n2\n3|\nb1\n2\n3|");
}
