#include "fjedit/core/vim/engine.hpp"
#include "fjedit/core/vim/notation.hpp"

#include "view.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

using namespace fjedit;
using ftk::ui::Key;

namespace {

// A view with Vim on it, keys typed in Vim's notation. Each expectation is
// what Vim does with the same keys on the same text.
struct Vim {
    TestView view;
    vim::Engine engine;

    explicit Vim(std::string_view marked) : view(marked) {}

    // What the engine acts through.
    CommandContext context() {
        view.folds.catch_up(view.document);
        return {.document = view.document, .selections = view.selections, .layout = view.layout,
                .clipboard = &view.clipboard, .folds = &view.folds, .text = {}};
    }

    void keys(std::string_view typed) {
        const std::optional<std::vector<Chord>> chords = vim::parse_notation(typed);
        REQUIRE(chords);
        for (const Chord& key : *chords) {
            CommandContext acting = context();
            (void)engine.take(key, acting);
        }
    }

    // Selections made as the mouse or a command makes them, then settled.
    void select(Selection selection, bool by_mouse) {
        view.selections = Selections(selection);
        CommandContext acting = context();
        engine.settle(acting, by_mouse);
    }

    [[nodiscard]] std::string shown() const { return view.shown(); }
};

} // namespace

TEST_CASE("Keys are read in Vim's notation, and written back in it", "[vim][notation]") {
    const auto ciw = vim::parse_notation("ciw<Esc>");
    REQUIRE(ciw);
    CHECK(*ciw == std::vector<Chord>{Chord::typed("c"), Chord::typed("i"), Chord::typed("w"), Chord{.key = Key::Escape}});
    CHECK(vim::parse_notation("<C-r>") == std::vector<Chord>{{.key = Key::R, .modifiers = {.ctrl = true}}});
    CHECK(vim::parse_notation("<C-R>") == vim::parse_notation("<c-r>"));
    CHECK(vim::parse_notation("<S-Tab>") == std::vector<Chord>{{.key = Key::Tab, .modifiers = {.shift = true}}});
    CHECK(vim::parse_notation("<Space>w<lt>") ==
          std::vector<Chord>{Chord::typed(" "), Chord::typed("w"), Chord::typed("<")});
    CHECK(vim::parse_notation("<esc><CR><Enter>") ==
          std::vector<Chord>{{.key = Key::Escape}, {.key = Key::Enter}, {.key = Key::Enter}});
    CHECK(vim::parse_notation("<C-Space>") == std::vector<Chord>{{.key = Key::Space, .modifiers = {.ctrl = true}}});
    // A < that opens no name is the character; a modifier on no name is
    // refused.
    CHECK(vim::parse_notation("<x>")->size() == 3);
    CHECK_FALSE(vim::parse_notation("<C-nothing>"));
    CHECK(vim::parse_notation("\xC3\xA4") == std::vector<Chord>{Chord::typed("\xC3\xA4")});

    CHECK(vim::notation(Chord::typed(" ")) == "<Space>");
    CHECK(vim::notation(Chord::typed("<")) == "<lt>");
    CHECK(vim::notation(Chord::typed("$")) == "$");
    CHECK(vim::notation({.key = Key::R, .modifiers = {.ctrl = true}}) == "<C-r>");
    CHECK(vim::notation({.key = Key::J, .modifiers = {.alt = true}}) == "<A-j>");
    CHECK(vim::notation({.key = Key::Escape}) == "<Esc>");
    CHECK(vim::notation({.key = Key::Enter}) == "<CR>");
    CHECK(vim::notation({.key = Key::LeftBracket, .modifiers = {.ctrl = true}}) == "<C-[>");
}

TEST_CASE("Normal mode starts, and its motions move the caret", "[vim][engine]") {
    Vim v("|foo bar baz");
    CHECK(v.engine.mode() == vim::Mode::Normal);
    CHECK(v.engine.caret_shape() == vim::CaretShape::Block);
    v.keys("w");
    CHECK(v.shown() == "foo |bar baz");
    v.keys("$");
    CHECK(v.shown() == "foo bar ba|z");
    v.keys("0");
    CHECK(v.shown() == "|foo bar baz");
    v.keys("2w");
    CHECK(v.shown() == "foo bar |baz");
    v.keys("b");
    CHECK(v.shown() == "foo |bar baz");
    v.keys("e");
    CHECK(v.shown() == "foo ba|r baz");
}

TEST_CASE("The caret stays on a character: w on the last word, l at a line's end", "[vim][engine]") {
    Vim last("foo |bar");
    last.keys("w");
    CHECK(last.shown() == "foo ba|r");
    Vim end("a|b");
    end.keys("5l");
    CHECK(end.shown() == "a|b");
    end.keys("<Space>");
    CHECK(end.shown() == "a|b");
}

TEST_CASE("j and k keep the column, through shorter lines and back", "[vim][engine]") {
    Vim v("ab|cd\nx\nabcd");
    v.keys("j");
    CHECK(v.shown() == "abcd\n|x\nabcd");
    v.keys("j");
    CHECK(v.shown() == "abcd\nx\nab|cd");
    v.keys("2k");
    CHECK(v.shown() == "ab|cd\nx\nabcd");
    // $ keeps to the end of every line after it.
    Vim ends("a|b\nabcd\nxy");
    ends.keys("$j");
    CHECK(ends.shown() == "ab\nabc|d\nxy");
    ends.keys("j");
    CHECK(ends.shown() == "ab\nabcd\nx|y");
}

TEST_CASE("G and gg go to a line by its number, the last and first without one", "[vim][engine]") {
    Vim v("|a\nb\n  c");
    v.keys("G");
    CHECK(v.shown() == "a\nb\n  |c");
    v.keys("gg");
    CHECK(v.shown() == "|a\nb\n  c");
    v.keys("2G");
    CHECK(v.shown() == "a\n|b\n  c");
    v.keys("-");
    CHECK(v.shown() == "|a\nb\n  c");
    v.keys("2+");
    CHECK(v.shown() == "a\nb\n  |c");
}

TEST_CASE("Space and Backspace go on and back across line breaks", "[vim][engine]") {
    Vim v("a|b\nc");
    v.keys("<Space>");
    CHECK(v.shown() == "ab\n|c");
    v.keys("<BS>");
    CHECK(v.shown() == "a|b\nc");
}

TEST_CASE("f, t, F and T find on the line, ; and , again", "[vim][engine]") {
    Vim v("|a,b,c");
    v.keys("f,");
    CHECK(v.shown() == "a|,b,c");
    v.keys(";");
    CHECK(v.shown() == "a,b|,c");
    v.keys(",");
    CHECK(v.shown() == "a|,b,c");
    Vim till("|a,b,c");
    till.keys("t,");
    CHECK(till.shown() == "|a,b,c");
    till.keys(";");
    CHECK(till.shown() == "a,|b,c");
}

TEST_CASE("% goes to the paired bracket, } and { between paragraphs", "[vim][engine]") {
    Vim v("|f(a) {\n}");
    v.keys("%");
    CHECK(v.shown() == "f(a|) {\n}");
    v.keys("%");
    CHECK(v.shown() == "f|(a) {\n}");
    Vim paragraphs("|a\n\nb");
    paragraphs.keys("}");
    CHECK(paragraphs.shown() == "a\n|\nb");
    paragraphs.keys("}");
    CHECK(paragraphs.shown() == "a\n\n|b");
    paragraphs.keys("{");
    CHECK(paragraphs.shown() == "a\n|\nb");
}

TEST_CASE("i, a, I and A type where they say, and Esc steps back onto a character", "[vim][engine]") {
    Vim i("ab|c");
    i.keys("ix");
    CHECK(i.engine.mode() == vim::Mode::Insert);
    CHECK(i.engine.caret_shape() == vim::CaretShape::Bar);
    i.keys("<Esc>");
    CHECK(i.engine.mode() == vim::Mode::Normal);
    CHECK(i.shown() == "ab|xc");

    Vim a("a|bc");
    a.keys("ax<Esc>");
    CHECK(a.shown() == "ab|xc");

    Vim ends("  a|b");
    ends.keys("Ax<Esc>");
    CHECK(ends.shown() == "  ab|x");
    ends.keys("Iy<Esc>");
    CHECK(ends.shown() == "  |yabx");
}

TEST_CASE("o opens a line indented as this one, undone in one with what was typed", "[vim][engine]") {
    Vim v("  a|b");
    v.keys("oxy<Esc>");
    CHECK(v.shown() == "  ab\n  x|y");
    v.keys("u");
    CHECK(v.shown() == "  a|b");
    v.keys("Oz<Esc>");
    CHECK(v.shown() == "  |z\n  ab");
}

TEST_CASE("Insert mode's keys edit as they do without Vim", "[vim][engine]") {
    Vim v("a|b");
    v.keys("i<BS><Esc>");
    CHECK(v.shown() == "|b");
    v.keys("i<CR><Esc>");
    CHECK(v.shown() == "\n|b");
}

TEST_CASE("u undoes and Ctrl-R redoes", "[vim][engine]") {
    Vim v("|a");
    v.keys("ix<Esc>");
    CHECK(v.shown() == "|xa");
    v.keys("u");
    CHECK(v.shown() == "|a");
    v.keys("<C-r>");
    CHECK(v.view.buffer().text() == "xa");
}

TEST_CASE("Keys act at every caret, and Esc leaves the primary", "[vim][engine]") {
    Vim v("|a\n|b");
    v.keys("ix<Esc>");
    CHECK(v.shown() == "|xa\n|xb");
    v.keys("$");
    CHECK(v.shown() == "x|a\nx|b");
    v.keys("<Esc>");
    CHECK(v.shown() == "xa\nx|b");
}

TEST_CASE("A command half typed waits and shows its keys; Esc drops it", "[vim][engine]") {
    Vim v("|a\nb\nc");
    v.keys("2");
    CHECK(v.engine.pending() == "2");
    v.keys("g");
    CHECK(v.engine.pending() == "2g");
    v.keys("g");
    CHECK(v.engine.pending().empty());
    CHECK(v.shown() == "a\n|b\nc");
    // A count dropped by Esc counts for nothing after it.
    Vim dropped("|abc");
    dropped.keys("2<Esc>l");
    CHECK(dropped.shown() == "a|bc");
    // Counts stop growing rather than overflow.
    dropped.keys("99999999999w");
    CHECK(dropped.shown() == "ab|c");
}

TEST_CASE("f waits for a character, and a command dropped counts for nothing", "[vim][engine]") {
    Vim v("|abcb");
    v.keys("2f");
    CHECK(v.engine.wants_character());
    v.keys("b");
    CHECK_FALSE(v.engine.wants_character());
    CHECK(v.shown() == "abc|b");
    v.keys("3");
    v.engine.cancel();
    CHECK(v.engine.pending().empty());
    v.keys("h");
    CHECK(v.shown() == "ab|cb");
}

TEST_CASE("dw deletes to the next word, and on a line's last word keeps the break", "[vim][operator]") {
    Vim v("|foo bar\nbaz");
    v.keys("dw");
    CHECK(v.shown() == "|bar\nbaz");
    Vim last("foo |bar\nbaz");
    last.keys("dw");
    CHECK(last.shown() == "foo| \nbaz");
}

TEST_CASE("dd deletes lines, a count more; the last line's leaves the caret on the line before", "[vim][operator]") {
    Vim v("a\n|b\nc\nd");
    v.keys("2dd");
    CHECK(v.shown() == "a\n|d");
    v.keys("dd");
    CHECK(v.shown() == "|a");
    Vim lines("|a\nb\nc");
    lines.keys("dj");
    CHECK(lines.shown() == "|c");
}

TEST_CASE("An exclusive motion ending at a line's start from its first takes whole lines", "[vim][operator]") {
    Vim v("|a\nb\n\nc");
    v.keys("d}");
    CHECK(v.shown() == "|\nc");
}

TEST_CASE("cw changes to the word's end, undone in one with what was typed", "[vim][operator]") {
    Vim v("|foo bar");
    v.keys("cwx<Esc>");
    CHECK(v.shown() == "|x bar");
    v.keys("u");
    CHECK(v.shown() == "|foo bar");
}

TEST_CASE("Operators take text objects", "[vim][operator]") {
    Vim brackets("f(|a, b)");
    brackets.keys("di(");
    CHECK(brackets.shown() == "f(|)");
    Vim quotes("say \"h|i\" now");
    quotes.keys("ci\"yo<Esc>");
    CHECK(quotes.shown() == "say \"y|o\" now");
    Vim word("foo |bar baz");
    word.keys("daw");
    CHECK(word.shown() == "foo |baz");
}

TEST_CASE("yy and p put lines below, P above, the caret on the first one's text", "[vim][operator]") {
    Vim v("|a\nb");
    v.keys("yyp");
    CHECK(v.shown() == "a\n|a\nb");
    v.keys("P");
    CHECK(v.shown() == "a\n|a\na\nb");
    // Text put before the caret leaves it on the last character put.
    Vim words("|ab cd");
    words.keys("ywP");
    CHECK(words.shown() == "ab| ab cd");
}

TEST_CASE("x, X, D, C, s, S and Y are operators over a motion", "[vim][operator]") {
    Vim x("a|bc");
    x.keys("x");
    CHECK(x.shown() == "a|c");
    x.keys("X");
    CHECK(x.shown() == "|c");
    Vim d("a|bc");
    d.keys("D");
    CHECK(d.shown() == "|a");
    Vim c("a|bc");
    c.keys("Cx<Esc>");
    CHECK(c.shown() == "a|x");
    Vim s("|ab");
    s.keys("sx<Esc>");
    CHECK(s.shown() == "|xb");
    Vim big_s("  |ab\ncd");
    big_s.keys("Sx<Esc>");
    CHECK(big_s.shown() == "  |x\ncd");
    Vim y("a|bc");
    y.keys("Yp");
    CHECK(y.shown() == "abb|cc");
    // x on an empty line takes nothing and leaves the register.
    Vim empty("|a\n\n");
    empty.keys("xjxp");
    CHECK(empty.shown() == "\n|a\n");
}

TEST_CASE("J joins lines with a space, none before )", "[vim][operator]") {
    Vim v("|a\n  b\nc");
    v.keys("J");
    CHECK(v.shown() == "a| b\nc");
    Vim three("|a\n  b\nc");
    three.keys("3J");
    CHECK(three.shown() == "a b| c");
    Vim paren("|f(\n  )");
    paren.keys("J");
    CHECK(paren.shown() == "f(|)");
}

TEST_CASE("r replaces characters, ~ turns their case", "[vim][operator]") {
    Vim r("|abc");
    r.keys("rx");
    CHECK(r.shown() == "|xbc");
    r.keys("2rz");
    CHECK(r.shown() == "z|zc");
    // Not that many characters: nothing.
    r.keys("5rq");
    CHECK(r.shown() == "z|zc");
    Vim t("|abc");
    t.keys("~");
    CHECK(t.shown() == "A|bc");
    t.keys("2~");
    CHECK(t.shown() == "AB|C");
}

TEST_CASE(">> and << shift lines, = indents them as their brackets say", "[vim][operator]") {
    Vim s("|a\nb");
    s.keys(">>");
    CHECK(s.shown() == "    |a\nb");
    s.keys("<<");
    CHECK(s.shown() == "|a\nb");
    s.keys(">j");
    CHECK(s.shown() == "    |a\n    b");
    // = indents by the document's level, here four spaces, whatever the
    // closer's two would have it read as.
    Vim e("|f() {\nx;\n  }");
    e.view.document.set_indent({.tabs = false, .width = 4});
    e.keys("=G");
    CHECK(e.shown() == "|f() {\n    x;\n}");
}

TEST_CASE("gU, gu and g~ change the case of what they take", "[vim][operator]") {
    Vim v("|foo bar");
    v.keys("gUiw");
    CHECK(v.shown() == "|FOO bar");
    v.keys("g~~");
    CHECK(v.shown() == "|foo BAR");
    v.keys("guu");
    CHECK(v.shown() == "|foo bar");
}

TEST_CASE("Counts before and after an operator multiply", "[vim][operator]") {
    Vim v("|a b c d e f");
    v.keys("2d2w");
    CHECK(v.shown() == "|e f");
}

TEST_CASE("A register named before an operator takes its text", "[vim][operator]") {
    Vim v("|a b");
    v.keys("\"adw");
    CHECK(v.shown() == "|b");
    v.keys("\"ap");
    CHECK(v.shown() == "ba| ");
    CHECK(v.engine.registers().get('a', nullptr) == vim::Yanked{.pieces = {"a "}, .linewise = false});
}

TEST_CASE("Ctrl-A and Ctrl-X add to the number at or after the caret", "[vim][operator]") {
    Vim v("x |9 y");
    v.keys("<C-a>");
    CHECK(v.shown() == "x 1|0 y");
    v.keys("11<C-x>");
    CHECK(v.shown() == "x -|1 y");
    Vim hex("|0x0f");
    hex.keys("<C-a>");
    CHECK(hex.shown() == "0x1|0");
}

TEST_CASE("Operators act at every caret, and each caret puts its own piece", "[vim][operator]") {
    Vim v("|ab cd\n|ef gh");
    v.keys("dw");
    CHECK(v.shown() == "|cd\n|gh");
    v.keys("yiw$p");
    CHECK(v.shown() == "cdc|d\nghg|h");
}

TEST_CASE("An operator waiting for its motion shows the lower half of a block", "[vim][operator]") {
    Vim v("|a");
    v.keys("d");
    CHECK(v.engine.caret_shape() == vim::CaretShape::HalfBlock);
    v.keys("<Esc>");
    CHECK(v.engine.caret_shape() == vim::CaretShape::Block);
    CHECK(v.shown() == "|a");
}

TEST_CASE("v and a motion select to the caret, its character taken; d deletes them", "[vim][visual]") {
    Vim v("|abc def");
    v.keys("vl");
    CHECK(v.engine.mode() == vim::Mode::Visual);
    CHECK(v.shown() == "[ab]c def");
    CHECK(v.engine.cursors() == std::vector<Position>{{0, 1}});
    v.keys("d");
    CHECK(v.engine.mode() == vim::Mode::Normal);
    CHECK(v.shown() == "|c def");
    // Backwards, both ends' characters are taken.
    Vim back("ab|c");
    back.keys("vhd");
    CHECK(back.shown() == "|a");
}

TEST_CASE("V takes whole lines", "[vim][visual]") {
    Vim v("a\n|b\nc");
    v.keys("Vj");
    CHECK(v.engine.mode() == vim::Mode::VisualLine);
    CHECK(v.shown() == "a\n[b\nc]");
    v.keys("d");
    CHECK(v.shown() == "|a");
}

TEST_CASE("Ctrl-V takes a block; an operator over it leaves one caret at its top left", "[vim][visual]") {
    Vim v("|abc\ndef\nghi");
    v.keys("<C-v>jl");
    CHECK(v.engine.mode() == vim::Mode::VisualBlock);
    CHECK(v.shown() == "[ab]c\n[de]f\nghi");
    CHECK(v.engine.cursors() == std::vector<Position>{{1, 1}});
    v.keys("d");
    CHECK(v.shown() == "|c\nf\nghi");
}

TEST_CASE("A block's I and A insert on each of its lines, and leave one caret", "[vim][visual]") {
    Vim before("|abc\ndef");
    before.keys("<C-v>jIx<Esc>");
    CHECK(before.shown() == "|xabc\nxdef");
    Vim after("|abc\ndef");
    after.keys("<C-v>jAy<Esc>");
    CHECK(after.shown() == "a|ybc\ndyef");
}

TEST_CASE("o swaps a selection's ends; v, V and Ctrl-V switch, the same one leaves", "[vim][visual]") {
    Vim v("a|bc");
    v.keys("vloh");
    CHECK(v.shown() == "[abc]");
    Vim kinds("a|b\nc");
    kinds.keys("vV");
    CHECK(kinds.shown() == "[ab\n]c");
    kinds.keys("v");
    CHECK(kinds.shown() == "a[b]\nc");
    kinds.keys("v");
    CHECK(kinds.engine.mode() == vim::Mode::Normal);
    CHECK(kinds.shown() == "a|b\nc");
}

TEST_CASE("Esc leaves visual mode with the caret where the selection's was", "[vim][visual]") {
    Vim v("|abc");
    v.keys("vlll<Esc>");
    CHECK(v.engine.mode() == vim::Mode::Normal);
    CHECK(v.shown() == "ab|c");
}

TEST_CASE("Visual mode's operators: shift by a count, case, r, J", "[vim][visual]") {
    Vim shift("|a\nb");
    shift.keys("Vj2>");
    CHECK(shift.shown() == "        |a\n        b");
    Vim upper("|abc");
    upper.keys("vlU");
    CHECK(upper.shown() == "|ABc");
    Vim replace("|abc");
    replace.keys("vlrx");
    CHECK(replace.shown() == "|xxc");
    Vim join("|a\nb\nc");
    join.keys("VjjJ");
    CHECK(join.shown() == "a b| c");
}

TEST_CASE("p over a selection puts the register in its place, keeping what it replaced", "[vim][visual]") {
    Vim v("|foo bar");
    v.keys("yiwwviwp");
    CHECK(v.shown() == "foo fo|o");
    CHECK(v.engine.registers().get('"', nullptr) == vim::Yanked{.pieces = {"bar"}, .linewise = false});
}

TEST_CASE("A text object in visual mode selects it; gv selects the last selection again", "[vim][visual]") {
    Vim v("f(a, |b)");
    v.keys("vi(d");
    CHECK(v.shown() == "f(|)");
    Vim again("|abc def");
    again.keys("vl<Esc>wgv");
    CHECK(again.engine.mode() == vim::Mode::Visual);
    CHECK(again.shown() == "[ab]c def");
}

TEST_CASE("A selection the mouse made begins visual mode; one made otherwise is a caret at its start", "[vim][visual]") {
    Vim mouse("abc|");
    mouse.select({.anchor = {0, 0}, .caret = {0, 2}}, true);
    CHECK(mouse.engine.mode() == vim::Mode::Visual);
    CHECK(mouse.shown() == "[ab]c");
    mouse.keys("d");
    CHECK(mouse.shown() == "|c");
    // A click in visual mode leaves it.
    Vim click("|abc");
    click.keys("vl");
    click.select(Selection::at({0, 2}), true);
    CHECK(click.engine.mode() == vim::Mode::Normal);
    Vim other("|abcd");
    other.select({.anchor = {0, 1}, .caret = {0, 3}}, false);
    CHECK(other.engine.mode() == vim::Mode::Normal);
    CHECK(other.shown() == "a|bcd");
}

TEST_CASE("Visual mode acts at every caret", "[vim][visual]") {
    Vim v("|ab\n|cd");
    v.keys("vld");
    CHECK(v.shown() == "|\n|");
}

TEST_CASE("Settling puts a caret past a line's end back on its last character", "[vim][engine]") {
    Vim v("ab|");
    CommandContext context{.document = v.view.document, .selections = v.view.selections, .layout = v.view.layout,
                           .clipboard = &v.view.clipboard, .folds = &v.view.folds, .text = {}};
    v.engine.settle(context);
    CHECK(v.shown() == "a|b");
}
