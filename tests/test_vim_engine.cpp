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
    // The rows a view ten rows high has in sight.
    InView sight{.top = 0, .rows = 10, .around = 0};

    explicit Vim(std::string_view marked) : view(marked) {}

    // What the engine acts through.
    CommandContext context() {
        view.folds.catch_up(view.document);
        return {.document = view.document, .selections = view.selections, .layout = view.layout,
                .clipboard = &view.clipboard, .folds = &view.folds, .text = {}, .in_view = &sight};
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

TEST_CASE("Ctrl-W deletes the word before; Ctrl-U the line before, back to its indent, then its start",
          "[vim][engine]") {
    Vim word("foo bar|");
    word.keys("A<C-w><Esc>");
    CHECK(word.shown() == "foo| ");
    Vim line("|  foo bar");
    line.keys("A<C-u><Esc>");
    CHECK(line.shown() == " | ");
    Vim indent("  |x");
    indent.keys("i<C-u><Esc>");
    CHECK(indent.shown() == "|x");
}

TEST_CASE("Ctrl-R types what a register holds; in replace mode it writes over", "[vim][engine]") {
    Vim v("|ab");
    v.keys("ylA<C-r>");
    CHECK(v.engine.pending() == "<C-r>");
    CHECK(v.engine.wants_character());
    v.keys("\"<Esc>");
    CHECK(v.shown() == "ab|a");
    Vim replace("|abc");
    replace.keys("yllR<C-r>\"<Esc>");
    CHECK(replace.shown() == "a|ac");
}

TEST_CASE("Ctrl-O runs one command, then goes back to the mode it left", "[vim][engine]") {
    Vim v("ab|c");
    v.keys("i<C-o>");
    CHECK(v.engine.mode() == vim::Mode::Normal);
    v.keys("0");
    CHECK(v.engine.mode() == vim::Mode::Insert);
    v.keys("x<Esc>");
    CHECK(v.shown() == "|xabc");
    // A caret that was at the line's end goes back past it, as after $.
    Vim end("|ab");
    end.keys("A<C-o>~x<Esc>");
    CHECK(end.shown() == "aB|x");
    Vim dollar("|abc");
    dollar.keys("i<C-o>$x<Esc>");
    CHECK(dollar.shown() == "abc|x");
    // An operator waits for its motion; a command that inserts stays.
    Vim op("|ab cd");
    op.keys("i<C-o>d");
    CHECK(op.engine.mode() == vim::Mode::Normal);
    op.keys("wx<Esc>");
    CHECK(op.shown() == "|xcd");
    Vim inserts("|ab");
    inserts.keys("i<C-o>Ax<Esc>");
    CHECK(inserts.shown() == "ab|x");
    // From replace mode, back to replace mode.
    Vim replace("|abc");
    replace.keys("R<C-o>l");
    CHECK(replace.engine.mode() == vim::Mode::Replace);
    replace.keys("x<Esc>");
    CHECK(replace.shown() == "a|xc");
    // A binding run in its place is the command.
    Vim bound("|ab");
    bound.keys("A<C-o>");
    CommandContext acting = bound.context();
    bound.engine.cancel(acting);
    CHECK(bound.engine.mode() == vim::Mode::Insert);
    bound.keys("x<Esc>");
    CHECK(bound.shown() == "ab|x");
}

TEST_CASE("What was typed before Ctrl-O is undone apart from what was typed after", "[vim][engine]") {
    Vim v("|a");
    v.keys("ix<C-o>0y<Esc>u");
    CHECK(v.view.buffer().text() == "xa");
}

TEST_CASE("Ctrl-T and Ctrl-D indent the line to the next whole level in or out", "[vim][engine]") {
    Vim v("|ab");
    v.view.document.set_indent({.tabs = false, .width = 4});
    v.keys("i<C-t>x<Esc>");
    CHECK(v.shown() == "    |xab");
    v.keys("I<C-d><Esc>");
    CHECK(v.shown() == "|xab");
    Vim odd("|   ab");
    odd.view.document.set_indent({.tabs = false, .width = 4});
    odd.keys("A<C-t><Esc>");
    CHECK(odd.shown() == "    a|b");
    Vim out("|   ab");
    out.view.document.set_indent({.tabs = false, .width = 4});
    out.keys("A<C-d><Esc>");
    CHECK(out.shown() == "a|b");
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
    CommandContext acting = v.context();
    v.engine.cancel(acting);
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

TEST_CASE("Visual mode's operators: shift by a count and back, case, r, J", "[vim][visual]") {
    Vim shift("|a\nb");
    shift.keys("Vj2>");
    CHECK(shift.shown() == "        |a\n        b");
    shift.keys("Vj<");
    CHECK(shift.shown() == "    |a\n    b");
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

TEST_CASE("R types over the characters, on past a line's end, an underline its caret", "[vim][replace]") {
    Vim v("|abc");
    v.keys("Rx");
    CHECK(v.engine.mode() == vim::Mode::Replace);
    CHECK(v.engine.caret_shape() == vim::CaretShape::Underline);
    v.keys("y<Esc>");
    CHECK(v.engine.mode() == vim::Mode::Normal);
    CHECK(v.shown() == "x|yc");
    Vim past("a|b");
    past.keys("Rxyz<Esc>");
    CHECK(past.shown() == "axy|z");
}

TEST_CASE("Backspace in replace mode puts back what was typed over, and before that only moves", "[vim][replace]") {
    Vim v("|abc");
    v.keys("Rxy<BS><BS><Esc>");
    CHECK(v.shown() == "|abc");
    Vim before("a|bc");
    before.keys("Rx<BS><BS><Esc>");
    CHECK(before.shown() == "|abc");
    // What was put on past the end goes again.
    Vim added("a|b");
    added.keys("Rxy<BS><Esc>");
    CHECK(added.shown() == "a|x");
}

TEST_CASE("A replace is one step of the history, and acts at every caret", "[vim][replace]") {
    Vim v("|abc");
    v.keys("Rxy<Esc>u");
    CHECK(v.shown() == "|abc");
    Vim carets("|ab\n|cd");
    carets.keys("Rx<Esc>");
    CHECK(carets.shown() == "|xb\n|xd");
}

TEST_CASE("Settling puts a caret past a line's end back on its last character", "[vim][engine]") {
    Vim v("ab|");
    CommandContext context{.document = v.view.document, .selections = v.view.selections, .layout = v.view.layout,
                           .clipboard = &v.view.clipboard, .folds = &v.view.folds, .text = {}};
    v.engine.settle(context);
    CHECK(v.shown() == "a|b");
}

TEST_CASE("Each mode looks up its own keymap mode first", "[vim][engine]") {
    CHECK(vim::keymap_mode(vim::Mode::Normal) == Keymap::VIM_NORMAL);
    CHECK(vim::keymap_mode(vim::Mode::Insert) == Keymap::VIM_INSERT);
    CHECK(vim::keymap_mode(vim::Mode::Replace) == Keymap::VIM_INSERT);
    CHECK(vim::keymap_mode(vim::Mode::Visual) == Keymap::VIM_VISUAL);
    CHECK(vim::keymap_mode(vim::Mode::VisualBlock) == Keymap::VIM_VISUAL);
}

TEST_CASE("Vim's keys for a command are those of the mode it is in", "[vim][engine]") {
    CHECK(vim::keys_for("edit.undo", vim::Mode::Normal) == vim::parse_notation("u"));
    CHECK(vim::keys_for("edit.redo", vim::Mode::Normal) == vim::parse_notation("<C-r>"));
    CHECK(vim::keys_for("edit.copy", vim::Mode::Normal) == vim::parse_notation("\"+yy"));
    CHECK(vim::keys_for("edit.copy", vim::Mode::VisualLine) == vim::parse_notation("\"+y"));
    CHECK(vim::keys_for("edit.undo", vim::Mode::Insert) == vim::parse_notation("<C-o>u"));
    CHECK(vim::keys_for("edit.paste", vim::Mode::Replace) == vim::parse_notation("<C-r>+"));
    // Visual mode's u makes letters lower; insert mode has no cut.
    CHECK_FALSE(vim::keys_for("edit.undo", vim::Mode::Visual));
    CHECK_FALSE(vim::keys_for("edit.cut", vim::Mode::Insert));
    CHECK_FALSE(vim::keys_for("find.open", vim::Mode::Normal));
}

TEST_CASE("A dot makes the last change again, a count given it for its own", "[vim][repeat]") {
    Vim x("|abcdefgh");
    x.keys("x");
    CHECK(x.shown() == "|bcdefgh");
    x.keys("3.");
    CHECK(x.shown() == "|efgh");
    // The count given stays the change's.
    x.keys(".");
    CHECK(x.shown() == "|h");
    // The counts before and after an operator are one.
    Vim words("|a b c d e f g h i");
    words.keys("2d2w");
    CHECK(words.shown() == "|e f g h i");
    words.keys(".");
    CHECK(words.shown() == "|i");
    // A yank is no change, nor an operator whose motion can't go.
    Vim yank("|ab cd");
    yank.keys("xywd.");
    CHECK(yank.shown() == "|b cd");
    yank.keys(".");
    CHECK(yank.shown() == "| cd");
}

TEST_CASE("A dot makes an insert again with what was typed in it", "[vim][repeat]") {
    Vim i("|ab");
    i.keys("ix<Esc>.");
    CHECK(i.shown() == "|xxab");
    Vim append("|a\nb");
    append.keys("A;<Esc>j.");
    CHECK(append.shown() == "a;\nb|;");
    Vim change("|foo bar");
    change.keys("cwx<Esc>w.");
    CHECK(change.shown() == "x |x");
    // A count types it that many times.
    Vim counted("|ab");
    counted.keys("ix<Esc>3.");
    CHECK(counted.shown() == "xx|xxab");
    // One step of the history.
    Vim undone("|ab");
    undone.keys("ix<Esc>.u");
    CHECK(undone.view.buffer().text() == "xab");
}

TEST_CASE("A count before an insert types it that many times, after o on lines of its own", "[vim][repeat]") {
    Vim i("|ab");
    i.keys("3ix<Esc>");
    CHECK(i.shown() == "xx|xab");
    i.keys("u");
    CHECK(i.view.buffer().text() == "ab");
    Vim o("|a");
    o.keys("3ox<Esc>");
    CHECK(o.shown() == "a\nx\nx\n|x");
    Vim replace("|abcdefg");
    replace.keys("3Rxy<Esc>");
    CHECK(replace.shown() == "xyxyx|yg");
}

TEST_CASE("A dot types Ctrl-R's text and a completion's again", "[vim][repeat]") {
    // The text as it was put, whatever the register holds by then.
    Vim v("|ab\ncd");
    v.keys("ylA<C-r>\"<Esc>jyl.");
    CHECK(v.view.buffer().text() == "aba\ncda");
    // A completion taken in place of the word typed: the view's edit, then
    // what it tells the engine.
    Vim completed("|x\ny");
    completed.keys("Apr");
    CommandContext acting = completed.context();
    acting.text = "intf";
    (void)run_command("edit.type", acting);
    completed.engine.completed("pr", "printf");
    completed.keys("<Esc>j.");
    CHECK(completed.view.buffer().text() == "xprintf\nyprintf");
}

TEST_CASE("A move in insert mode ends the change there; what is typed after it is one of its own", "[vim][repeat]") {
    Vim v("|ab");
    v.keys("ix<Right>y<Esc>.");
    CHECK(v.shown() == "xa|yyb");
    // The mouse moving the caret ends it as well.
    Vim mouse("|ab");
    mouse.keys("ix");
    mouse.select(Selection::at({0, 2}), true);
    mouse.keys("y<Esc>0.");
    CHECK(mouse.shown() == "|yxayb");
}

TEST_CASE("A dot makes a visual change again over as much text from the caret", "[vim][repeat]") {
    Vim lines("|a\nb\nc\nd");
    lines.keys("Vj>jj.");
    CHECK(lines.view.buffer().text() == "    a\n    b\n    c\n    d");
    Vim chars("|abcdef");
    chars.keys("vld.");
    CHECK(chars.shown() == "|ef");
    // Over lines, on to the same place along the last.
    Vim across("|abc\ndef\nghi\njkl");
    across.keys("vjdj.");
    CHECK(across.view.buffer().text() == "ef\nkl");
    Vim block("|abc\ndef\nghi\njkl");
    block.keys("<C-v>jldjj.");
    CHECK(block.view.buffer().text() == "c\nf\ni\nl");
    // With what was typed after it.
    Vim typed("|ab ab");
    typed.keys("vlcx<Esc>w.");
    CHECK(typed.shown() == "x |x");
}

TEST_CASE("A dot after a put from a numbered register puts from the next", "[vim][repeat]") {
    Vim v("|a\nb\nc\nd");
    v.keys("dddddd\"1p..");
    CHECK(v.shown() == "d\nc\nb\n|a");
}

namespace {

// `count` lines of `line`, the caret at the first's start.
std::string lines_of(std::size_t count, std::string_view line = "text") {
    std::string out = "|";
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0) out += '\n';
        out += line;
    }
    return out;
}

} // namespace

TEST_CASE("H, M and L go to the rows in sight at the top, the middle and the bottom", "[vim][view]") {
    Vim v(lines_of(50));
    const auto line = [&] { return v.view.selections.primary().caret.line; };
    v.sight.top = 10;
    v.keys("H");
    CHECK(line() == 10);
    v.keys("M");
    CHECK(line() == 14);
    v.keys("L");
    CHECK(line() == 19);
    v.keys("3H");
    CHECK(line() == 12);
    v.keys("3L");
    CHECK(line() == 17);
    // Kept from an edge by the rows kept around the caret, but where the
    // text begins.
    v.sight.around = 2;
    v.keys("H");
    CHECK(line() == 12);
    v.keys("L");
    CHECK(line() == 17);
    v.sight.top = 0;
    v.keys("H");
    CHECK(line() == 0);
    // An operator takes the rows to the edge itself.
    v.sight.top = 10;
    v.keys("HdL");
    CHECK(v.view.buffer().line_count() == 42);
}

TEST_CASE("Ctrl-D and Ctrl-U scroll half the view, the caret with the text; a count is kept", "[vim][view]") {
    Vim v(lines_of(50));
    const auto line = [&] { return v.view.selections.primary().caret.line; };
    v.keys("<C-d>");
    CHECK(v.sight.top == 5);
    CHECK(line() == 5);
    v.keys("<C-d>");
    CHECK(v.sight.top == 10);
    CHECK(line() == 10);
    v.keys("<C-u>");
    CHECK(v.sight.top == 5);
    CHECK(line() == 5);
    v.keys("3<C-d>");
    CHECK(v.sight.top == 8);
    v.keys("<C-d>");
    CHECK(v.sight.top == 11);
    CHECK(line() == 11);
    // Near the end the view stops with the last line at its bottom, and the
    // caret goes on.
    Vim end(lines_of(50));
    end.sight.top = 38;
    end.keys("45G<C-d>");
    CHECK(end.sight.top == 40);
    CHECK(end.view.selections.primary().caret.line == 49);
    // The caret keeps its place along the line.
    Vim along(lines_of(20, "abcdef"));
    along.keys("3l<C-d>");
    CHECK(along.view.selections.primary().caret == Position{5, 3});
}

TEST_CASE("Ctrl-F and Ctrl-B scroll a view less two rows, the caret into sight", "[vim][view]") {
    Vim v(lines_of(50));
    const auto line = [&] { return v.view.selections.primary().caret.line; };
    v.keys("<C-f>");
    CHECK(v.sight.top == 8);
    CHECK(line() == 8);
    v.sight.around = 2;
    v.keys("<C-f>");
    CHECK(v.sight.top == 16);
    CHECK(line() == 18);
    v.keys("<C-b>");
    CHECK(v.sight.top == 8);
    CHECK(line() == 15);
}

TEST_CASE("Ctrl-E and Ctrl-Y scroll a row, the caret moving only to stay in sight", "[vim][view]") {
    Vim v(lines_of(50));
    const auto line = [&] { return v.view.selections.primary().caret.line; };
    v.keys("5G<C-e>");
    CHECK(v.sight.top == 1);
    CHECK(line() == 4);
    v.keys("<C-e><C-e><C-e><C-e>");
    CHECK(v.sight.top == 5);
    CHECK(line() == 5);
    v.keys("15G<C-y>");
    CHECK(v.sight.top == 4);
    CHECK(line() == 13);
}

TEST_CASE("zz, zt and zb put the caret's row in the middle, at the top or at the bottom", "[vim][view]") {
    Vim v(lines_of(50, "  text"));
    v.keys("21Gzz");
    CHECK(v.sight.top == 16);
    v.keys("zt");
    CHECK(v.sight.top == 20);
    v.keys("zb");
    CHECK(v.sight.top == 11);
    v.sight.around = 2;
    v.keys("zt");
    CHECK(v.sight.top == 18);
    v.keys("zb");
    CHECK(v.sight.top == 13);
    // A count goes to that line first; z<CR> to its first non-blank.
    v.keys("5zt");
    CHECK(v.view.selections.primary().caret == Position{4, 2});
    CHECK(v.sight.top == 2);
    v.keys("$z<CR>");
    CHECK(v.view.selections.primary().caret == Position{4, 2});
}

TEST_CASE("Scrolling in visual mode moves the selection's caret end", "[vim][view]") {
    Vim v(lines_of(50));
    v.keys("V<C-d>d");
    CHECK(v.view.buffer().line_count() == 44);
}

TEST_CASE("zc, zo, za, zM and zR close and open the folds at the caret", "[vim][view]") {
    Vim v("|void f() {\n    a;\n}\nvoid g() {\n    b;\n}");
    v.keys("zc");
    CHECK(v.view.folds.hidden(1));
    v.keys("zo");
    CHECK_FALSE(v.view.folds.hidden(1));
    v.keys("za");
    CHECK(v.view.folds.hidden(1));
    v.keys("zR");
    CHECK_FALSE(v.view.folds.hidden(1));
    v.keys("zM");
    CHECK(v.view.folds.hidden(1));
    CHECK(v.view.folds.hidden(4));
}

TEST_CASE("Insert mode begun from outside notes nothing for the dot", "[vim][engine]") {
    Vim v("|abc");
    v.keys("x");
    v.engine.begin_insert();
    CHECK(v.engine.mode() == vim::Mode::Insert);
    v.keys("zz");
    CommandContext acting = v.context();
    v.engine.end_insert(acting);
    CHECK(v.engine.mode() == vim::Mode::Normal);
    CHECK(v.shown() == "zz|bc");
    // The dot makes the x again, not what was typed.
    v.keys(".");
    CHECK(v.shown() == "zz|c");
}

TEST_CASE("/ searches on from the caret; n and N go on with it round the text's ends", "[vim][search]") {
    Vim v("|foo bar foo\nfoo");
    const auto caret = [&] { return v.view.selections.primary().caret; };
    v.keys("/foo<CR>");
    CHECK(v.engine.mode() == vim::Mode::Normal);
    CHECK(caret() == Position{0, 8});
    v.keys("n");
    CHECK(caret() == Position{1, 0});
    v.keys("n");
    CHECK(caret() == Position{0, 0});
    CHECK(v.engine.message().text == "search hit BOTTOM, continuing at TOP");
    CHECK_FALSE(v.engine.message().error);
    v.keys("N");
    CHECK(caret() == Position{1, 0});
    CHECK(v.engine.message().text == "search hit TOP, continuing at BOTTOM");
    // A count goes that many matches on.
    v.keys("gg2n");
    CHECK(caret() == Position{1, 0});
}

TEST_CASE("? searches back, and n goes its way", "[vim][search]") {
    Vim v("foo bar foo ba|z");
    const auto caret = [&] { return v.view.selections.primary().caret; };
    v.keys("?foo<CR>");
    CHECK(caret() == Position{0, 8});
    v.keys("n");
    CHECK(caret() == Position{0, 0});
    v.keys("N");
    CHECK(caret() == Position{0, 8});
}

TEST_CASE("A search is a motion, for an operator and in visual mode", "[vim][search]") {
    Vim d("|one two foo");
    d.keys("d/foo<CR>");
    CHECK(d.shown() == "|foo");
    Vim visual("|abc foo");
    visual.keys("v/foo<CR>d");
    CHECK(visual.shown() == "|oo");
}

TEST_CASE("While a pattern is typed its match is selected; Esc goes back where it began", "[vim][search]") {
    Vim v("|one foo");
    v.keys("/fo");
    CHECK(v.engine.mode() == vim::Mode::Command);
    REQUIRE(v.engine.command_line() != nullptr);
    CHECK(v.engine.command_line()->kind == '/');
    CHECK(v.engine.command_line()->text == "fo");
    CHECK(v.view.selections.primary().span() == Span{{0, 4}, {0, 6}});
    CHECK(v.engine.shown_search() != nullptr);
    v.keys("<Esc>");
    CHECK(v.engine.mode() == vim::Mode::Normal);
    CHECK(v.engine.command_line() == nullptr);
    CHECK(v.shown() == "|one foo");
    CHECK(v.engine.shown_search() == nullptr);
}

TEST_CASE("The command line's keys: Backspace, Ctrl-W, Ctrl-U, Ctrl-R and the searches before", "[vim][search]") {
    Vim v("|ab abc");
    v.keys("/abc<CR>");
    // Backspace on an empty line leaves it.
    v.keys("/x<BS>");
    CHECK(v.engine.mode() == vim::Mode::Command);
    v.keys("<BS>");
    CHECK(v.engine.mode() == vim::Mode::Normal);
    v.keys("/one two<C-w>");
    CHECK(v.engine.command_line()->text == "one ");
    v.keys("<C-u>");
    CHECK(v.engine.command_line()->text.empty());
    v.keys("<Up>");
    CHECK(v.engine.command_line()->text == "abc");
    v.keys("<Down>");
    CHECK(v.engine.command_line()->text.empty());
    v.keys("<Esc>yiw/<C-r>\"");
    CHECK(v.engine.command_line()->text == "abc");
}

TEST_CASE("An empty pattern looks for the last again; one that finds nothing, or doesn't read, says so",
          "[vim][search]") {
    Vim v("|foo x foo y foo");
    const auto caret = [&] { return v.view.selections.primary().caret; };
    v.keys("/foo<CR>/<CR>");
    CHECK(caret() == Position{0, 12});
    v.keys("/zzz<CR>");
    CHECK(v.engine.message().text == "E486: Pattern not found: zzz");
    CHECK(v.engine.message().error);
    CHECK(caret() == Position{0, 12});
    v.keys("/\\(<CR>");
    CHECK(v.engine.message().text.starts_with("E54"));
    // A / not escaped nor in a collection ends the pattern; an offset after
    // it is refused.
    v.keys("gg/foo/<CR>");
    CHECK(caret() == Position{0, 6});
    v.keys("/[/]x\\/y/e<CR>");
    CHECK(v.engine.message().text == "Search offsets aren't supported: e");
    // The next key clears what was said.
    v.keys("l");
    CHECK(v.engine.message().text.empty());
}

TEST_CASE("* and # look for the word at the caret, whole; g* and g# as it is", "[vim][search]") {
    Vim v("|spin spinner spin");
    const auto caret = [&] { return v.view.selections.primary().caret; };
    v.keys("*");
    CHECK(caret() == Position{0, 13});
    v.keys("#");
    CHECK(caret() == Position{0, 0});
    v.keys("g*");
    CHECK(caret() == Position{0, 5});
}

TEST_CASE("Ctrl-L hides the matches until the next search; a find's search is the one n goes on with",
          "[vim][search]") {
    Vim v("|foo bar foo");
    v.keys("/foo<CR>");
    CHECK(v.engine.shown_search() != nullptr);
    v.keys("<C-l>");
    CHECK(v.engine.shown_search() == nullptr);
    v.keys("n");
    CHECK(v.engine.shown_search() != nullptr);
    ftk::Result<vim::Pattern> bar = vim::read_pattern("bar");
    REQUIRE(bar);
    v.engine.search_for({.text = "bar", .pattern = *bar, .forward = true});
    v.keys("0n");
    CHECK(v.view.selections.primary().caret == Position{0, 4});
}

TEST_CASE("The : line goes to a line and deletes the lines a range names", "[vim][ex]") {
    Vim v("|a\nb\nc\nd\ne");
    v.keys(":3<CR>");
    CHECK(v.shown() == "a\nb\n|c\nd\ne");
    v.keys(":2,3d<CR>");
    CHECK(v.shown() == "a\n|d\ne");
    v.keys(":$d<CR>");
    CHECK(v.shown() == "a\n|d");
    v.keys(":%d<CR>");
    CHECK(v.view.buffer().text().empty());
}

TEST_CASE("The : line yanks into a register, and a count takes lines from a range's last", "[vim][ex]") {
    Vim v("|a\nb\nc");
    v.keys(":2y x<CR>");
    CHECK(v.shown() == "|a\nb\nc");
    v.keys("\"xp");
    CHECK(v.view.buffer().text() == "a\nb\nb\nc");
    v.keys(":1d 2<CR>");
    CHECK(v.view.buffer().text() == "b\nc");
    v.keys(":1d 0<CR>");
    CHECK(v.engine.message().text == "E939: Positive count required");
}

TEST_CASE("The : line moves and copies lines below the line an address names", "[vim][ex]") {
    Vim v("|a\nb\nc");
    v.keys(":1m$<CR>");
    CHECK(v.shown() == "b\nc\n|a");
    v.keys(":m0<CR>");
    CHECK(v.shown() == "|a\nb\nc");
    v.keys(":t.<CR>");
    CHECK(v.shown() == "a\n|a\nb\nc");
    v.keys(":1,2m3<CR>");
    CHECK(v.shown() == "b\na\n|a\nc");
    v.keys(":1,3m2<CR>");
    CHECK(v.engine.message().text == "E134: Cannot move a range of lines into itself");
    CHECK(v.view.buffer().text() == "b\na\na\nc");
}

TEST_CASE("The : line shifts lines a level each > or <, and says so for more than two", "[vim][ex]") {
    Vim v("|a\nb\nc\nd");
    v.view.document.set_indent({.tabs = false, .width = 4});
    v.keys(":2,3><CR>");
    CHECK(v.shown() == "a\n    b\n    |c\nd");
    v.keys(":%>><CR>");
    CHECK(v.view.buffer().text() == "        a\n            b\n            c\n        d");
    CHECK(v.engine.message().text == "4 lines >ed 2 times");
    v.keys(":%<lt><lt><lt><CR>");
    CHECK(v.view.buffer().text() == "a\nb\nc\nd");
}

TEST_CASE("A count before : and a visual selection give the line its range; a pattern finds a line", "[vim][ex]") {
    Vim count("|a\nb\nc\nd");
    count.keys("2:");
    REQUIRE(count.engine.command_line() != nullptr);
    CHECK(count.engine.command_line()->text == ".,.+1");
    count.keys("d<CR>");
    CHECK(count.view.buffer().text() == "c\nd");
    Vim visual("|a\nb\nc\nd");
    visual.keys("jVj:");
    REQUIRE(visual.engine.command_line() != nullptr);
    CHECK(visual.engine.command_line()->text == "'<,'>");
    visual.keys("d<CR>");
    CHECK(visual.view.buffer().text() == "a\nd");
    // The pattern is the last search then.
    Vim found("|a\nfoo\nb\nfoo");
    found.keys(":/foo/d<CR>");
    CHECK(found.view.buffer().text() == "a\nb\nfoo");
    found.keys("n");
    CHECK(found.view.selections.primary().caret == Position{2, 0});
}

TEST_CASE(":s replaces the first match on each line of its range, or every one with g", "[vim][ex]") {
    Vim v("|spin Spin spin\nspin");
    v.keys(":s/spin/X/<CR>");
    CHECK(v.view.buffer().text() == "X Spin spin\nspin");
    v.keys(":s/spin/X/g<CR>");
    CHECK(v.view.buffer().text() == "X Spin X\nspin");
    v.keys(":%s/spin/Y/gi<CR>");
    CHECK(v.view.buffer().text() == "X Y X\nY");
    // One step of the history, the caret on the last line changed.
    CHECK(v.view.selections.primary().caret == Position{1, 0});
    v.keys("u");
    CHECK(v.view.buffer().text() == "X Spin X\nspin");
}

TEST_CASE(":s puts in groups, the match and the last replacement; an empty pattern is the last search's",
          "[vim][ex]") {
    Vim groups("|ab ab");
    groups.keys(":s/\\(a\\)\\(b\\)/\\2\\1/g<CR>");
    CHECK(groups.view.buffer().text() == "ba ba");
    Vim last("|x x");
    last.keys("/x<CR>:s//[&]/g<CR>");
    CHECK(last.view.buffer().text() == "[x] [x]");
    Vim previous("|a b");
    previous.keys(":s/a/1/<CR>:s/b/~2/<CR>");
    CHECK(previous.view.buffer().text() == "1 12");
    // ^ and $ match where they stand, taking nothing.
    Vim ends("|a\nb");
    ends.keys(":%s/^/# /<CR>:%s/$/;/<CR>");
    CHECK(ends.view.buffer().text() == "# a;\n# b;");
}

TEST_CASE("& and g& make the last :s again; :s says what it did and what it didn't find", "[vim][ex]") {
    Vim v("|aaa\naaa");
    v.keys(":s/a/b/<CR>&");
    CHECK(v.view.buffer().text() == "bba\naaa");
    v.keys("j&");
    CHECK(v.view.buffer().text() == "bba\nbaa");
    v.keys("g&");
    CHECK(v.view.buffer().text() == "bbb\nbba");
    v.keys(":s/z/y/<CR>");
    CHECK(v.engine.message().text == "E486: Pattern not found: z");
    v.keys(":s/z/y/e<CR>");
    CHECK(v.engine.message().text.empty());
    Vim many("|a a a a");
    many.keys(":s/a/b/g<CR>");
    CHECK(many.engine.message().text == "4 substitutions on 1 line");
}

TEST_CASE(":g runs a command at each line its pattern matches, :v at each it doesn't, as one step", "[vim][ex]") {
    Vim g("|a1\nb\na2\nc");
    g.keys(":g/a/d<CR>");
    CHECK(g.view.buffer().text() == "b\nc");
    g.keys("u");
    CHECK(g.view.buffer().text() == "a1\nb\na2\nc");
    Vim v("|a1\nb\na2\nc");
    v.keys(":v/a/d<CR>");
    CHECK(v.view.buffer().text() == "a1\na2");
    Vim empty("|a\n\nb\n");
    empty.keys(":g/^$/d<CR>");
    CHECK(empty.view.buffer().text() == "a\nb");
    Vim none("|a");
    none.keys(":g/zz/d<CR>");
    CHECK(none.engine.message().text == "E486: Pattern not found: zz");
}

TEST_CASE(":g follows its lines as they move: reversed with m0, gathered at the end with m$", "[vim][ex]") {
    Vim reverse("|1\n2\n3");
    reverse.keys(":g/^/m0<CR>");
    CHECK(reverse.view.buffer().text() == "3\n2\n1");
    Vim gather("|x1\ny\nx2\nz");
    gather.keys(":g/x/m$<CR>");
    CHECK(gather.view.buffer().text() == "y\nz\nx1\nx2");
    Vim substitute("|a1\nb1\na1");
    substitute.keys(":g/a/s/1/X/<CR>");
    CHECK(substitute.view.buffer().text() == "aX\nb1\naX");
    Vim normal("|a\nb\na");
    normal.keys(":g/a/norm A;<CR>");
    CHECK(normal.view.buffer().text() == "a;\nb\na;");
}

TEST_CASE(":norm types its keys at each line of its range, or at the caret, as one step", "[vim][ex]") {
    Vim v("|a\nb\nc");
    v.keys(":%norm A!<CR>");
    CHECK(v.view.buffer().text() == "a!\nb!\nc!");
    v.keys("u");
    CHECK(v.view.buffer().text() == "a\nb\nc");
    Vim caret("x|y");
    caret.keys(":norm iz<CR>");
    CHECK(caret.view.buffer().text() == "xzy");
    // A command left unfinished does nothing.
    caret.keys(":norm d<CR>");
    CHECK(caret.view.buffer().text() == "xzy");
    CHECK(caret.engine.mode() == vim::Mode::Normal);
}

TEST_CASE(":sort orders lines, the case let go with i, by number with n, once each with u, backwards with !",
          "[vim][ex]") {
    Vim v("|c\na\nb\na");
    v.keys(":sort<CR>");
    CHECK(v.view.buffer().text() == "a\na\nb\nc");
    Vim unique("|c\na\nb\na");
    unique.keys(":sort u<CR>");
    CHECK(unique.view.buffer().text() == "a\nb\nc");
    Vim back("|c\na\nb\na");
    back.keys(":sort!<CR>");
    CHECK(back.view.buffer().text() == "c\nb\na\na");
    Vim numbers("|x10\nx9\ny");
    numbers.keys(":sort n<CR>");
    CHECK(numbers.view.buffer().text() == "y\nx9\nx10");
    Vim folded("|b\nA\na\nB");
    folded.keys(":sort i<CR>");
    CHECK(folded.view.buffer().text() == "A\na\nb\nB");
    Vim part("|d\nc\nb\na");
    part.keys(":2,3sort<CR>");
    CHECK(part.view.buffer().text() == "d\nb\nc\na");
}

TEST_CASE("The : line says what is wrong as Vim does, keeps what was typed, and :noh hides the matches",
          "[vim][ex]") {
    Vim v("|a\nb");
    v.keys(":frob<CR>");
    CHECK(v.engine.message().text == "E492: Not an editor command: frob");
    CHECK(v.engine.message().error);
    v.keys(":9d<CR>");
    CHECK(v.engine.message().text == "E16: Invalid range");
    v.keys(":'ad<CR>");
    CHECK(v.engine.message().text == "E20: Mark not set");
    v.keys(":2<CR>:<Up>");
    CHECK(v.engine.command_line()->text == "2");
    v.keys("<Esc>/b<CR>");
    CHECK(v.engine.shown_search() != nullptr);
    v.keys(":");
    CHECK(v.engine.shown_search() != nullptr);
    v.keys("noh<CR>");
    CHECK(v.engine.shown_search() == nullptr);
}
