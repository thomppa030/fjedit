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
    tabs.document.set_indent({.tabs = true, .width = 4});
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

TEST_CASE("Enter keeps the line's indent", "[editing][indent]") {
    TestView view("    foo();|");
    view.run("edit.newline");
    REQUIRE(view.shown() == "    foo();\n    |");
}

TEST_CASE("Enter after an opener goes a level deeper", "[editing][indent]") {
    TestView view("  if (a) {|");
    view.document.set_indent({.tabs = false, .width = 4});
    view.run("edit.newline");
    REQUIRE(view.shown() == "  if (a) {\n      |");
}

TEST_CASE("Enter between brackets puts the closer on a line of its own", "[editing][indent]") {
    TestView view("void f() {|}");
    view.run("edit.newline");
    REQUIRE(view.shown() == "void f() {\n    |\n}");
    view.run("edit.undo");
    REQUIRE(view.shown() == "void f() {|}");
}

TEST_CASE("Enter after an opener in a string stays level", "[editing][indent]") {
    TestView view("  s = \"{|\";");
    view.run("edit.newline");
    REQUIRE(view.shown() == "  s = \"{\n  |\";");
}

TEST_CASE("Enter inside the indent keeps only the indent before the caret", "[editing][indent]") {
    TestView view("  |  foo");
    view.run("edit.newline");
    REQUIRE(view.shown() == "  \n  |  foo");
}

TEST_CASE("Enter indents with tabs when the file does", "[editing][indent]") {
    TestView view("\tif (a) {|");
    view.document.set_indent({.tabs = true, .width = 4});
    view.run("edit.newline");
    REQUIRE(view.shown() == "\tif (a) {\n\t\t|");
}

TEST_CASE("Enter at every caret", "[editing][indent]") {
    TestView view("a {|\n  b|");
    view.document.set_indent({.tabs = false, .width = 4});
    view.run("edit.newline");
    REQUIRE(view.shown() == "a {\n    |\n  b\n  |");
}

TEST_CASE("A closer typed on a line of spaces goes back to its opener's indent", "[editing][indent]") {
    TestView view("  f() {\n    g();\n    |");
    view.type("}");
    REQUIRE(view.shown() == "  f() {\n    g();\n  }|");
}

TEST_CASE("A closer after other text, or with no opener, stays where it is typed", "[editing][indent]") {
    TestView after("  {\n    x|");
    after.type("}");
    REQUIRE(after.shown() == "  {\n    x}|");

    TestView alone("    |");
    alone.type(")");
    REQUIRE(alone.shown() == "    )|");
}

TEST_CASE("A closer passes over brackets in strings to find its opener", "[editing][indent]") {
    TestView view("call(\n  \"(\",\n      |");
    view.type(")");
    REQUIRE(view.shown() == "call(\n  \"(\",\n)|");
}

TEST_CASE("Typing on after Enter is one step to undo", "[editing][indent]") {
    TestView view("x {|");
    view.run("edit.newline");
    view.type("y();");
    view.run("edit.undo");
    REQUIRE(view.shown() == "x {|");
}

namespace {

// `marked` in a view whose typing closes pairs, in the language of `file`.
struct Pairing : TestView {
    explicit Pairing(std::string_view marked, std::string_view file = "x.cpp") : TestView(marked, file) {
        close_pairs = true;
    }
};

} // namespace

TEST_CASE("An opener types its closer, the caret between them", "[editing][pairs]") {
    Pairing end("f|");
    end.type("(");
    REQUIRE(end.shown() == "f(|)");
    Pairing space("a| b");
    space.type("{");
    REQUIRE(space.shown() == "a{|} b");
    Pairing closer("g(|)");
    closer.type("(");
    REQUIRE(closer.shown() == "g((|))");
    Pairing semicolon("x|;");
    semicolon.type("[");
    REQUIRE(semicolon.shown() == "x[|];");
}

TEST_CASE("An opener before other text types only itself", "[editing][pairs]") {
    Pairing view("|foo");
    view.type("(");
    REQUIRE(view.shown() == "(|foo");
}

TEST_CASE("Nothing closes in a string or a comment", "[editing][pairs]") {
    Pairing string("s = \"a| b\";");
    string.type("(");
    REQUIRE(string.shown() == "s = \"a(| b\";");
    Pairing open("s = \"a|");
    open.type("(");
    REQUIRE(open.shown() == "s = \"a(|");
    Pairing comment("x; // see|");
    comment.type("(");
    REQUIRE(comment.shown() == "x; // see(|");
}

TEST_CASE("A quote closes, but not just after a word unless it is a string's prefix", "[editing][pairs]") {
    Pairing double_quote("x = |;");
    double_quote.type("\"");
    REQUIRE(double_quote.shown() == "x = \"|\";");
    Pairing single_quote("x = |;");
    single_quote.type("'");
    REQUIRE(single_quote.shown() == "x = '|';");
    Pairing word("it|");
    word.type("'");
    REQUIRE(word.shown() == "it'|");
    Pairing digit("n = 1|");
    digit.type("'");
    REQUIRE(digit.shown() == "n = 1'|");
    Pairing prefix("s = u8|");
    prefix.type("\"");
    REQUIRE(prefix.shown() == "s = u8\"|\"");
}

TEST_CASE("Only the language's quotes close, and plain text's brackets", "[editing][pairs]") {
    Pairing json_single("{|}", "x.json");
    json_single.type("'");
    REQUIRE(json_single.shown() == "{'|}");
    Pairing json_double("{|}", "x.json");
    json_double.type("\"");
    REQUIRE(json_double.shown() == "{\"|\"}");
    Pairing plain("a |", "notes.txt");
    plain.type("\"(");
    REQUIRE(plain.shown() == "a \"(|)");
}

TEST_CASE("A closer steps over its like while every bracket around it has a pair", "[editing][pairs]") {
    Pairing call("f(x|)");
    call.type(")");
    REQUIRE(call.shown() == "f(x)|");
    Pairing block("f() {|}");
    block.type("}");
    REQUIRE(block.shown() == "f() {}|");
    // The outer ( has no pair: the ) typed closes the inner, and the one
    // there the outer.
    Pairing missing("if (a && (b|)");
    missing.type(")");
    REQUIRE(missing.shown() == "if (a && (b)|)");
}

TEST_CASE("A quote steps over the end of the string it is in", "[editing][pairs]") {
    Pairing view("s = \"abc|\";");
    view.type("\"");
    REQUIRE(view.shown() == "s = \"abc\"|;");
}

TEST_CASE("Typing a call through leaves the text as typed, one step to undo", "[editing][pairs]") {
    Pairing view("|");
    view.type("f(a, \"b\");");
    REQUIRE(view.shown() == "f(a, \"b\");|");
    view.run("edit.undo");
    REQUIRE(view.shown() == "|");
}

TEST_CASE("Backspace in an empty pair takes both", "[editing][pairs]") {
    Pairing brackets("f(|)");
    brackets.run("edit.backspace");
    REQUIRE(brackets.shown() == "f|");
    Pairing quotes("x = \"|\";");
    quotes.run("edit.backspace");
    REQUIRE(quotes.shown() == "x = |;");
    Pairing filled("f(|x)");
    filled.run("edit.backspace");
    REQUIRE(filled.shown() == "f|x)");
    TestView off("f(|)");
    off.run("edit.backspace");
    REQUIRE(off.shown() == "f|)");
}

TEST_CASE("An opener or a quote typed over a selection wraps it, still selected", "[editing][pairs]") {
    Pairing view("x = [foo];");
    view.type("(");
    REQUIRE(view.shown() == "x = ([foo]);");
    view.type("\"");
    REQUIRE(view.shown() == "x = (\"[foo]\");");
    view.run("edit.undo");
    REQUIRE(view.shown() == "x = [foo];");
}

TEST_CASE("Each caret closes and steps over on its own", "[editing][pairs]") {
    Pairing both("a|\nb|");
    both.type("(");
    REQUIRE(both.shown() == "a(|)\nb(|)");
    both.type(")");
    REQUIRE(both.shown() == "a()|\nb()|");
    Pairing mixed("f(|)\ng|");
    mixed.type(")");
    REQUIRE(mixed.shown() == "f()|\ng)|");
}

TEST_CASE("With pairs closing off, an opener types only itself", "[editing][pairs]") {
    TestView view("f|");
    view.type("(");
    REQUIRE(view.shown() == "f(|");
}

TEST_CASE("Enter goes as deep as the file indents", "[editing][indent]") {
    TestView view("a {\n  b {|");
    view.run("edit.newline");
    REQUIRE(view.shown() == "a {\n  b {\n    |");
}
