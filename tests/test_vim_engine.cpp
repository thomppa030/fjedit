#include "fjedit/core/vim/engine.hpp"
#include "fjedit/core/vim/notation.hpp"

#include "view.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
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
    // The `:` commands handed on for the program, in order; the files'
    // marks set and gone to; and the jumps made and the jump list stepped.
    std::vector<vim::ExCommand> handed;
    std::vector<vim::Asked> marks;
    std::vector<vim::Asked> jumps;

    explicit Vim(std::string_view marked) : view(marked) {}

    // What the engine acts through.
    CommandContext context() {
        view.folds.catch_up(view.document);
        return {.document = view.document, .selections = view.selections, .layout = view.layout,
                .clipboard = &view.clipboard, .folds = &view.folds, .text = {}, .close_pairs = view.close_pairs,
                .in_view = &sight};
    }

    // Keys typed, as the view gives them to Vim: each kept for a macro being
    // recorded, a macro played going in ahead of the rest, and what a macro
    // had yet to play let go when a command fails.
    void keys(std::string_view typed) {
        const std::optional<std::vector<Chord>> chords = vim::parse_notation(typed);
        REQUIRE(chords);
        // Each key to come, and whether it was typed.
        std::deque<std::pair<Chord, bool>> ahead;
        for (const Chord& key : *chords) ahead.emplace_back(key, true);
        while (!ahead.empty()) {
            const auto [key, was_typed] = ahead.front();
            ahead.pop_front();
            if (was_typed) engine.record(std::span(&key, 1));
            CommandContext acting = context();
            for (vim::Asked& asked : engine.take(key, acting)) {
                if (const auto* command = std::get_if<vim::ExCommand>(&asked)) {
                    handed.push_back(*command);
                } else if (std::holds_alternative<vim::MarkSet>(asked) || std::holds_alternative<vim::MarkWanted>(asked)) {
                    marks.push_back(std::move(asked));
                } else {
                    jumps.push_back(std::move(asked));
                }
            }
            const std::vector<Chord> played = engine.take_played();
            if (engine.failed()) {
                while (!ahead.empty() && !ahead.front().second) ahead.pop_front();
            }
            for (auto at = played.rbegin(); at != played.rend(); ++at) ahead.emplace_front(*at, false);
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

TEST_CASE("Insert mode closes pairs, steps over them and takes an empty one whole", "[vim][engine]") {
    Vim closing("f|");
    closing.view.close_pairs = true;
    closing.keys("a(x<Esc>");
    CHECK(closing.shown() == "f(|x)");
    Vim over("f|");
    over.view.close_pairs = true;
    over.keys("a(x)y<Esc>");
    CHECK(over.shown() == "f(x)|y");
    Vim back("f|");
    back.view.close_pairs = true;
    back.keys("a(<BS>x<Esc>");
    CHECK(back.shown() == "f|x");
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
    // A pair typed closed is typed closed again.
    Vim paired("|a\nb");
    paired.view.close_pairs = true;
    paired.keys("A(x<Esc>j.");
    CHECK(paired.shown() == "a(x)\nb(|x)");
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

TEST_CASE("A / search goes on from the caret; n and N go on with it round the text's ends", "[vim][search]") {
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

TEST_CASE(":set turns Vim's settings on, off and round, says them when asked, and gives the view's for the session",
          "[vim][ex]") {
    Vim v("|a Spin");
    v.keys("/spin<CR>");
    CHECK(v.engine.message().text == "E486: Pattern not found: spin");
    v.keys(":set ic<CR>/spin<CR>");
    CHECK(v.view.selections.primary().caret == Position{0, 2});
    v.keys(":set ic? scs?<CR>");
    CHECK(v.engine.message().text == "  ignorecase  nosmartcase");
    v.keys(":set noic<CR>:set invic<CR>:set ic?<CR>");
    CHECK(v.engine.message().text == "  ignorecase");
    v.keys(":set hls!<CR>");
    CHECK_FALSE(v.engine.settings().hlsearch);
    CHECK(v.engine.shown_search() == nullptr);
    v.keys(":set rnu so=20<CR>");
    CHECK(v.engine.settings().relative_numbers == std::optional<bool>{true});
    CHECK(v.engine.settings().around_caret == std::optional<std::size_t>{20});
    v.keys(":set so?<CR>");
    CHECK(v.engine.message().text == "  scrolloff=20");
}

TEST_CASE(":set shiftwidth, tabstop and expandtab indent as they say; what it can't set it says", "[vim][ex]") {
    Vim v("|a");
    v.keys(":set sw=2 et<CR>>>");
    CHECK(v.view.buffer().text() == "  a");
    v.keys(":set noet ts=2<CR>>>");
    CHECK(v.view.buffer().text() == "\t\ta");
    v.keys(":set sw?<CR>");
    CHECK(v.engine.message().text == "  shiftwidth=2");
    v.keys(":set foo<CR>");
    CHECK(v.engine.message().text == "E518: Unknown option: foo");
    v.keys(":set so=x<CR>");
    CHECK(v.engine.message().text == "E521: Number required after =: so=x");
    v.keys(":set sw=0<CR>");
    CHECK(v.engine.message().text == "E487: Argument must be positive: sw=0");
    v.keys(":set nonu<CR>");
    CHECK(v.engine.message().text == "Line numbers always show here");
}

TEST_CASE(":set and :noh leave every caret where it is; a command on lines acts at the primary's", "[vim][ex]") {
    Vim v("|ab\n|cd\nef");
    v.keys(":set ic<CR>");
    CHECK(v.shown() == "|ab\n|cd\nef");
    v.keys(":noh<CR>");
    CHECK(v.shown() == "|ab\n|cd\nef");
    Vim lines("|ab\n|cd\nef");
    lines.keys(":d<CR>");
    CHECK(lines.shown() == "ab\n|ef");
}

namespace {

// The names `commands` have, each with a ! after it when it has one and its
// argument after a blank when it has one.
std::vector<std::string> written(const std::vector<vim::ExCommand>& commands) {
    std::vector<std::string> out;
    for (const vim::ExCommand& command : commands) {
        out.push_back(command.name + (command.bang ? "!" : "") + (command.argument.empty() ? "" : " " + command.argument));
    }
    return out;
}

} // namespace

TEST_CASE("The program's : commands are handed on as they read, the carets left; what doesn't read is said",
          "[vim][ex]") {
    Vim v("|a\n|b");
    v.keys(":w<CR>");
    CHECK(written(v.handed) == std::vector<std::string>{"write"});
    CHECK(v.shown() == "|a\n|b");
    v.handed.clear();
    v.keys(":w!<CR>:wa<CR>:q<CR>:q!<CR>:clo<CR>:wq<CR>:x<CR>:qa!<CR>:e src/coin.cpp<CR>:e!<CR>");
    v.keys(":bn<CR>:bp<CR>:sp<CR>:vs<CR>");
    CHECK(written(v.handed) == std::vector<std::string>{"write!", "wall", "quit", "quit!", "close", "wq", "xit",
                                                        "qall!", "edit src/coin.cpp", "edit!", "bnext",
                                                        "bprevious", "split", "vsplit"});
    v.handed.clear();
    v.keys(":2w<CR>");
    CHECK(v.engine.message().text == "Not supported yet: :write with a range");
    v.keys(":w other.txt<CR>");
    CHECK(v.engine.message().text == "Not supported yet: :write with a file name");
    v.keys(":q now<CR>");
    CHECK(v.engine.message().text == "E488: Trailing characters: now");
    v.keys(":bn 2<CR>");
    CHECK(v.engine.message().text == "Not supported yet: :bnext with a count");
    v.keys(":wincmd<CR>");
    CHECK(v.engine.message().text == "E471: Argument required");
    // No group stands below another.
    v.keys(":wincmd j<CR>");
    CHECK(v.engine.message().text.empty());
    CHECK(v.handed.empty());
    // What the program has to say of one is said as the key's.
    v.engine.say({.text = "E37: No write since last change (add ! to override)", .error = true});
    CHECK(v.engine.message().error);
    v.keys("l");
    CHECK(v.engine.message().text.empty());
}

TEST_CASE("Ctrl-W's keys are the commands Vim runs for them, from visual and insert mode too", "[vim][ex]") {
    Vim v("|ab\ncd");
    v.keys("<C-w>w<C-w><C-w><C-w>h<C-w><C-h><C-w>l<C-w>q<C-w>c<C-w>s<C-w>v:wincmd l<CR>");
    CHECK(written(v.handed) == std::vector<std::string>{"wincmd w", "wincmd w", "wincmd h", "wincmd h", "wincmd l",
                                                        "quit", "close", "split", "vsplit", "wincmd l"});
    // A window fjedit has none of, or Ctrl-W after an operator, does
    // nothing: j moves no caret, d deletes nothing.
    v.handed.clear();
    v.keys("<C-w>j");
    v.keys("d<C-w>w");
    CHECK(v.handed.empty());
    CHECK(v.shown() == "|ab\ncd");
    // Out of visual mode, as `:` goes.
    v.keys("vl<C-w>v");
    CHECK(written(v.handed) == std::vector<std::string>{"vsplit"});
    CHECK(v.engine.mode() == vim::Mode::Normal);
    // Ctrl-O's one command, back in insert mode after it.
    v.handed.clear();
    v.keys("i<C-o><C-w>q");
    CHECK(written(v.handed) == std::vector<std::string>{"quit"});
    CHECK(v.engine.mode() == vim::Mode::Insert);
}

namespace {

// What the `:` line will do with `line` typed on it, as the words say it,
// what is typed in them between backticks; a refusal or what won't run
// says so first. The line is left with Esc.
std::string reading(Vim& v, std::string_view line, const vim::FileFacts& files = {}) {
    v.keys(":");
    if (!line.empty()) v.keys(line);
    CommandContext acting = v.context();
    const std::optional<vim::Outcome> outcome = v.engine.outcome(acting, files);
    v.keys("<Esc>");
    if (!outcome) return "(nothing)";
    std::string text = outcome->text;
    std::ranges::replace(text, '\x01', '`');
    std::ranges::replace(text, '\x02', '`');
    if (outcome->kind == vim::Outcome::Kind::Refused) return "refused: " + text;
    if (outcome->kind == vim::Outcome::Kind::Fails) return "fails: " + text;
    return text;
}

} // namespace

TEST_CASE("The : line says in plain words what Enter will do, its range as the lines it names", "[vim][ex]") {
    Vim v("a\n|gust\ngust\ngust\nb\nwarn");
    const std::string text = v.view.buffer().text();
    CHECK(reading(v, "") == "(nothing)");
    CHECK(reading(v, "5") == "Go to line 5");
    CHECK(reading(v, "12") == "fails: No such line in this file");
    CHECK(reading(v, "d") == "Delete line 2");
    CHECK(reading(v, "2,4d a") == "Delete lines 2 to 4 into register `a`");
    CHECK(reading(v, "d 3") == "Delete lines 2 to 4");
    CHECK(reading(v, "%y") == "Copy every line");
    CHECK(reading(v, "y +") == "Copy line 2 into the clipboard");
    CHECK(reading(v, ">>") == "Indent line 2 by 2 levels");
    CHECK(reading(v, "m0") == "Move line 2 above the first line");
    CHECK(reading(v, "t$") == "Put a copy of line 2 below line 6");
    CHECK(reading(v, "/warn/d") == "Delete line 6");
    CHECK(reading(v, "2,4m3") == "fails: E134: Cannot move a range of lines into itself");
    CHECK(reading(v, "s/gust/wind/") == "Replace the first `gust` with `wind` on line 2");
    CHECK(reading(v, "2,4s/gust/wind/g") == "Replace every `gust` with `wind` on lines 2 to 4");
    CHECK(reading(v, "%s/gust/wind/gi") == "Replace every `gust` with `wind` on every line, any case");
    CHECK(reading(v, "%s/gust/wind/") == "Replace the first `gust` with `wind` on each line");
    CHECK(reading(v, "2,4s/gust/wind/") == "Replace the first `gust` with `wind` on each of lines 2 to 4");
    CHECK(reading(v, "g/warn/d") == "Delete every line with `warn`");
    CHECK(reading(v, "v/warn/d") == "Delete every line without `warn`");
    CHECK(reading(v, "g/gust/s/u/o/") == "On every line with `gust`, replace the first `u` with `o` on it");
    CHECK(reading(v, "g/gust/.,+1d") == "On every line with `gust`, run `.,+1d`");
    CHECK(reading(v, "g/gust/") == "Search for `gust`");
    CHECK(reading(v, "g/gust/g/a/d") == "fails: E147: Cannot do :global recursive");
    CHECK(reading(v, "norm A;") == "Type `A;` in normal mode at the caret");
    CHECK(reading(v, "%norm A;") == "Type `A;` in normal mode on each line");
    CHECK(reading(v, "sort u") == "Sort every line, leaving out repeats");
    CHECK(reading(v, "sort! n") == "Sort every line in reverse by their first number");
    CHECK(reading(v, "set rnu ic?") == "Turn relative line numbers on, show whether ignoring case is on");
    CHECK(reading(v, "set sw=2") == "Set the indent width to 2");
    CHECK(reading(v, "set hls!") == "Turn showing the search's matches off");
    CHECK(reading(v, "set nonu") == "fails: Line numbers always show here");
    CHECK(reading(v, "set foo") == "fails: E518: Unknown option: foo");
    CHECK(reading(v, "noh") == "Hide the search's matches");
    CHECK(reading(v, "frob") == "fails: Not a command: `frob`");
    // Half typed: what Enter would do now.
    CHECK(reading(v, "s/gu") == "Delete the first `gu` on line 2");
    CHECK(reading(v, "s") == "fails: No search before this to go on with");
    CHECK(reading(v, "s/a\\(").starts_with("fails: "));
    // The selection's lines, as '<,'> names them.
    v.keys("Vjj:s/gust/wind/g");
    CommandContext acting = v.context();
    const std::optional<vim::Outcome> selected = v.engine.outcome(acting, {});
    REQUIRE(selected);
    CHECK(selected->text == "Replace every \x01gust\x02 with \x01wind\x02 on lines 2 to 4");
    v.keys("<Esc>");
    // Reading does nothing: no text changed, no search made.
    CHECK(v.view.buffer().text() == text);
    CHECK(v.engine.shown_search() == nullptr);
}

TEST_CASE("Tab goes round the commands a name typed can become, and back to it; typing ends the round",
          "[vim][ex]") {
    Vim v("|a");
    const auto line = [&] { return v.engine.command_line()->text; };
    v.keys(":w");
    const std::optional<vim::NameChoices> choices = v.engine.name_choices();
    REQUIRE(choices);
    CHECK(choices->typed == "w");
    REQUIRE(choices->names.size() == 4);
    CHECK(choices->names[3].name == "write");
    CHECK(choices->names[3].words == "Save");
    // :w is :write, which Enter runs.
    CHECK(choices->lit == std::optional<std::size_t>{3});
    v.keys("<Tab>");
    CHECK(line() == "wall");
    CHECK(v.engine.name_choices()->lit == std::optional<std::size_t>{0});
    CHECK(v.engine.name_choices()->names.size() == 4);
    v.keys("<Tab><Tab><Tab>");
    CHECK(line() == "write");
    v.keys("<Tab>");
    CHECK(line() == "w");
    v.keys("<Tab>");
    CHECK(line() == "wall");
    v.keys("<S-Tab>");
    CHECK(line() == "w");
    v.keys("<S-Tab>");
    CHECK(line() == "write");
    // A blank ends the round, and the names give way.
    v.keys(" ");
    CHECK(line() == "write ");
    CHECK_FALSE(v.engine.name_choices());
    v.keys("<Esc>");
    // A range before the name stays where it is.
    v.keys(":%su<Tab>");
    CHECK(line() == "%substitute");
    v.keys("<Esc>");
    // One picked with the mouse goes in as Tab's would.
    v.keys(":q");
    v.engine.choose_name(0);
    CHECK(line() == "qall");
    v.keys("<Tab>");
    CHECK(line() == "quit");
    v.keys("<Esc>");
    // A name no command begins as, and a search, have none.
    v.keys(":zz");
    CHECK_FALSE(v.engine.name_choices());
    v.keys("<Tab>");
    CHECK(line() == "zz");
    v.keys("<Esc>/w");
    CHECK_FALSE(v.engine.name_choices());
    v.keys("<Esc>");
}

TEST_CASE("The engine says the mode a map's keys are typed in, and runs a map's <Cmd> lines", "[vim][maps]") {
    Vim v("|a\nb\nc");
    CHECK(v.engine.map_mode() == vim::MapModes::Normal);
    v.keys("d");
    CHECK(v.engine.map_mode() == vim::MapModes::OperatorPending);
    v.keys("<Esc>v");
    CHECK(v.engine.map_mode() == vim::MapModes::Visual);
    v.keys("<Esc>i");
    CHECK(v.engine.map_mode() == vim::MapModes::Insert);
    v.keys("<Esc>:");
    CHECK(v.engine.map_mode() == vim::MapModes::Command);
    v.keys("<Esc>");
    // A <Cmd> line runs in the mode the engine is in, the program's handed
    // up: a line gone to is a jump.
    CommandContext acting = v.context();
    const std::vector<vim::Asked> went = v.engine.run_line("3", acting);
    REQUIRE(went.size() == 1);
    CHECK(std::holds_alternative<vim::JumpFrom>(went.front()));
    CHECK(v.shown() == "a\nb\n|c");
    CHECK(v.engine.mode() == vim::Mode::Normal);
    const std::vector<vim::Asked> write = v.engine.run_line("w", acting);
    REQUIRE(write.size() == 1);
    REQUIRE(std::holds_alternative<vim::ExCommand>(write.front()));
    CHECK(std::get<vim::ExCommand>(write.front()).name == "write");
    // timeoutlen, in milliseconds.
    v.keys(":set tm=300<CR>");
    CHECK(v.engine.settings().timeout_ms == 300);
    CHECK(reading(v, "set tm=500") == "Set the wait for a longer map to 500 ms");
}

TEST_CASE(":set incsearch and clipboard, and keys.vim's set lines applied by their owners", "[vim][ex][keys]") {
    Vim v("|spin\nroll");
    // Without incsearch, a pattern typed goes nowhere until Enter.
    v.keys(":set nois<CR>/roll");
    CHECK(v.shown() == "|spin\nroll");
    CHECK(v.engine.shown_search() == nullptr);
    v.keys("<CR>");
    CHECK(v.shown() == "spin\n|roll");
    v.keys(":set cb=unnamedplus<CR>");
    CHECK(v.engine.registers().uses_clipboard());
    v.keys(":set cb?<CR>");
    CHECK(v.engine.message().text == "  clipboard=unnamedplus");
    CHECK(reading(v, "set cb=") == "Keep the unnamed register off the system clipboard");

    // keys.vim's: Vim's own apply; a view's and a file's are said and skipped.
    vim::Settings settings;
    vim::Registers registers;
    const vim::KeysFile file = vim::read_keys_file(
        "set ignorecase smartcase nohlsearch tm=400\nset clipboard=unnamedplus\nset rnu ic\nset sw=2\nset foo\n");
    const std::vector<vim::KeysError> said = vim::apply_settings(file.settings, settings, registers);
    CHECK(settings.pattern.ignore_case);
    CHECK(settings.pattern.smart_case);
    CHECK_FALSE(settings.hlsearch);
    CHECK(settings.timeout_ms == 400);
    CHECK(registers.uses_clipboard());
    CHECK(said == std::vector<vim::KeysError>{{.line = 3, .message = "relativenumber: set in Preferences"},
                                              {.line = 4, .message = "shiftwidth: each file keeps its own; :set it there"},
                                              {.line = 5, .message = "E518: Unknown option: foo"}});
}

TEST_CASE("Ctrl-J and Ctrl-K step the : line's rows as Tab does; with none, Ctrl-J is Enter", "[vim][ex]") {
    Vim v("|a\nb");
    const auto line = [&] { return v.engine.command_line()->text; };
    v.keys(":w<C-j>");
    CHECK(line() == "wall");
    v.keys("<C-j>");
    CHECK(line() == "wincmd");
    v.keys("<C-k>");
    CHECK(line() == "wall");
    v.keys("<Esc>");
    // No rows: Ctrl-K does nothing, and Ctrl-J runs the line.
    v.keys(":2<C-k>");
    CHECK(line() == "2");
    v.keys("<C-j>");
    CHECK(v.engine.command_line() == nullptr);
    CHECK(v.shown() == "a\n|b");
}

TEST_CASE("Tab goes round the paths after :e; one alone goes in, a folder's entries listing next", "[vim][ex]") {
    Vim v("|a");
    // A folder of the program's, as it would list it.
    std::vector<std::string> asked;
    v.engine.list_paths_with([&](std::string_view folder, bool hidden) {
        asked.emplace_back(std::string(folder) + (hidden ? " hidden" : ""));
        if (folder == "src/") return std::vector<vim::PathEntry>{{.name = "main.cpp", .folder = false}};
        std::vector<vim::PathEntry> root{{.name = "scripts", .folder = true},
                                         {.name = "src", .folder = true},
                                         {.name = "spin.txt", .folder = false}};
        if (hidden) root.insert(root.begin(), vim::PathEntry{.name = ".git", .folder = true});
        return root;
    });
    const auto line = [&] { return v.engine.command_line()->text; };
    v.keys(":e s");
    const std::optional<vim::PathChoices> choices = v.engine.path_choices();
    REQUIRE(choices);
    CHECK(choices->typed == "s");
    CHECK(choices->entries.size() == 3);
    CHECK_FALSE(choices->lit);
    CHECK(asked.back() == "");
    v.keys("<Tab>");
    CHECK(line() == "e scripts/");
    CHECK(v.engine.path_choices()->lit == std::optional<std::size_t>{0});
    v.keys("<Tab><Tab>");
    CHECK(line() == "e spin.txt");
    v.keys("<Tab>");
    CHECK(line() == "e s");
    v.keys("<S-Tab>");
    CHECK(line() == "e spin.txt");
    v.keys("<Esc>");
    // One alone goes in and ends the round; the next Tab lists inside.
    v.keys(":e sr<Tab>");
    CHECK(line() == "e src/");
    v.keys("<Tab>");
    CHECK(line() == "e src/main.cpp");
    CHECK(asked.back() == "src/");
    v.keys("<Esc>");
    // Hidden ones once a dot begins the name.
    v.keys(":e .<Tab>");
    CHECK(line() == "e .git/");
    CHECK(asked.back() == " hidden");
    v.keys("<Esc>");
    // A click goes in, a folder's entries next.
    v.keys(":e s");
    v.engine.choose_path(1);
    CHECK(line() == "e src/");
    CHECK(v.engine.path_choices()->entries == std::vector<vim::PathEntry>{{.name = "main.cpp", .folder = false}});
    v.keys("<Esc>");
    // Nothing matching: nothing listed, Tab leaves the line.
    v.keys(":e zz<Tab>");
    CHECK_FALSE(v.engine.path_choices());
    CHECK(line() == "e zz");
    v.keys("<Esc>");
}

TEST_CASE("The : line says what the program's commands will do, and what they will refuse over changes",
          "[vim][ex]") {
    Vim v("|a");
    const vim::FileFacts saved{.file = "windmill.cpp", .closing_loses = false, .unsaved = {}};
    CHECK(reading(v, "w", saved) == "Save windmill.cpp");
    CHECK(reading(v, "w") == "Save this file");
    CHECK(reading(v, "wa", saved) == "Save all");
    CHECK(reading(v, "wq", saved) == "Save and close windmill.cpp");
    CHECK(reading(v, "x", saved) == "Close windmill.cpp");
    CHECK(reading(v, "q", saved) == "Close windmill.cpp");
    CHECK(reading(v, "qa", saved) == "Quit");
    CHECK(reading(v, "e scripts/wind.cpp", saved) == "Open `scripts/wind.cpp`");
    CHECK(reading(v, "e", saved) == "Read windmill.cpp again");
    CHECK(reading(v, "bn") == "Next tab");
    CHECK(reading(v, "bp") == "Previous tab");
    CHECK(reading(v, "vs") == "Split");
    CHECK(reading(v, "wincmd l") == "Go to the group on the right");
    CHECK(reading(v, "wincmd w") == "Go to the next group");
    CHECK(reading(v, "wincmd j") == "Nothing: no group stands there");
    CHECK(reading(v, "2w") == "fails: Not supported yet: :write with a range");
    // Changed: closing it or reading it again would lose the change.
    v.keys("x");
    const vim::FileFacts changed{.file = "windmill.cpp", .closing_loses = true, .unsaved = "windmill.cpp"};
    CHECK(reading(v, "q", changed) == "refused: Unsaved changes. `:q!` drops them, `:wq` saves first");
    CHECK(reading(v, "q!", changed) == "Close windmill.cpp, dropping its changes");
    CHECK(reading(v, "x", changed) == "Save and close windmill.cpp");
    CHECK(reading(v, "qa", changed) == "refused: Unsaved changes in windmill.cpp. `:qa!` drops them, `:wa` saves them");
    CHECK(reading(v, "qa!", changed) == "Quit, dropping every change");
    CHECK(reading(v, "e", changed) == "refused: Unsaved changes. `:e!` drops them");
    CHECK(reading(v, "e!", changed) == "Read windmill.cpp again, dropping its changes");
    // Another tab shows it: closing this one loses nothing.
    const vim::FileFacts shown{.file = "windmill.cpp", .closing_loses = false, .unsaved = "windmill.cpp"};
    CHECK(reading(v, "q", shown) == "Close windmill.cpp");
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

TEST_CASE("m sets a mark: ' goes to its line's first text, ` to the very place, and edits carry it", "[vim][marks]") {
    Vim v("one\n  t|wo\nthree");
    v.keys("maG`a");
    CHECK(v.shown() == "one\n  t|wo\nthree");
    v.keys("G'a");
    CHECK(v.shown() == "one\n  |two\nthree");
    // A line put in above, and text before it on its line, carry it.
    v.keys("ggOzero<Esc>G`a");
    CHECK(v.shown() == "zero\none\n  t|wo\nthree");
    v.keys("Ixy<Esc>G`a");
    CHECK(v.shown() == "zero\none\n  xyt|wo\nthree");
    CHECK(v.view.document.mark('a') == Position{2, 5});
    v.keys("'b");
    CHECK(v.engine.message().text == "E20: Mark not set");
    v.keys("'!");
    CHECK(v.engine.message().text == "E78: Unknown mark");
    v.keys("m!");
    CHECK(v.engine.message().text == "E191: Argument must be a letter or forward/backward quote");
    // m waits for the mark's name, which no binding may take.
    v.keys("m");
    CHECK(v.engine.wants_character());
    v.keys("<Esc>");
    CHECK_FALSE(v.engine.wants_character());
}

TEST_CASE("An operator takes what a mark's motion passes over: whole lines after ', up to the place after `",
          "[vim][marks]") {
    Vim lines("|a\nb\nc\nd");
    lines.keys("jjmaggd'a");
    CHECK(lines.shown() == "|d");
    Vim place("ab|cdef");
    place.keys("ma0d`a");
    CHECK(place.shown() == "|cdef");
}

TEST_CASE("'' and `` go back to where the latest jump went from; m' sets it, and edits carry it", "[vim][marks]") {
    Vim v("a\nb|b\nc\nd");
    v.keys("G``");
    CHECK(v.shown() == "a\nb|b\nc\nd");
    v.keys("``");
    CHECK(v.shown() == "a\nbb\nc\n|d");
    v.keys("''");
    CHECK(v.shown() == "a\n|bb\nc\nd");
    // A move that isn't a jump leaves it.
    v.keys("j''");
    CHECK(v.shown() == "a\nbb\nc\n|d");
    // A line put in above carries it.
    v.keys("ggOz<Esc>''");
    CHECK(v.shown() == "z\na\nbb\nc\n|d");

    // A search, and a line gone to on the : line, are jumps.
    Vim s("|a\nb\nc");
    s.keys("/c<CR>''");
    CHECK(s.shown() == "|a\nb\nc");
    s.keys(":3<CR>''");
    CHECK(s.shown() == "|a\nb\nc");
    s.keys("jm'j''");
    CHECK(s.shown() == "a\n|b\nc");
    Vim none("|a");
    none.keys("''");
    CHECK(none.engine.message().text == "E20: Mark not set");
}

TEST_CASE("Marks on the last change or yank and on the last visual selection, carried as the text changes",
          "[vim][marks]") {
    Vim v("|one\ntwo\nthree");
    v.keys("yjG`[");
    CHECK(v.shown() == "|one\ntwo\nthree");
    v.keys("`]");
    CHECK(v.shown() == "one\ntw|o\nthree");
    // Lines put below begin on the line after.
    v.keys("ggyyjp");
    REQUIRE(v.view.buffer().text() == "one\ntwo\none\nthree");
    v.keys("gg`[");
    CHECK(v.view.selections.primary().caret == Position{2, 0});
    v.keys("gg`]");
    CHECK(v.view.selections.primary().caret == Position{2, 2});
    // An insert, all that was typed in it; a delete, where the text was.
    Vim typed("ab|c");
    typed.keys("iXY<Esc>0`[");
    CHECK(typed.shown() == "ab|XYc");
    typed.keys("`]");
    CHECK(typed.shown() == "abX|Yc");
    Vim gone("a|bcd");
    gone.keys("x$`[");
    CHECK(gone.shown() == "a|cd");

    Vim s("ab|cd\nefgh\nij");
    s.keys("vjl<Esc>gg`<");
    CHECK(s.shown() == "ab|cd\nefgh\nij");
    s.keys("`>");
    CHECK(s.shown() == "abcd\nefg|h\nij");
    // Whole lines in visual line mode.
    s.keys("ggVj<Esc>G`<");
    CHECK(s.shown() == "|abcd\nefgh\nij");
    s.keys("`>");
    CHECK(s.shown() == "abcd\nefg|h\nij");
    // '<,'> names the lines selected, wherever an edit has moved them.
    Vim m("a\n|b\nc\nd");
    m.keys("Vj<Esc>ggOz<Esc>:'<,'>d<CR>");
    CHECK(m.view.buffer().text() == "z\na\nd");
    Vim r("|1\n2\n3\n4\n5");
    r.keys("jmajjmbgg:'a,'bd<CR>");
    CHECK(r.view.buffer().text() == "1\n5");
}

TEST_CASE("A file's mark is handed up when set, gone to when this file has it, and asked for when not",
          "[vim][marks]") {
    Vim v("|a\nb");
    v.keys("jmA");
    REQUIRE(v.marks.size() == 1);
    const auto* set = std::get_if<vim::MarkSet>(&v.marks[0]);
    REQUIRE(set != nullptr);
    CHECK(set->name == 'A');
    v.keys("gg'A");
    CHECK(v.shown() == "a\n|b");
    CHECK(v.marks.size() == 1);
    v.keys("`B");
    REQUIRE(v.marks.size() == 2);
    const auto* wanted = std::get_if<vim::MarkWanted>(&v.marks[1]);
    REQUIRE(wanted != nullptr);
    CHECK(wanted->name == 'B');
    CHECK(wanted->exact);
    // After an operator it goes nowhere: it is in another file.
    v.keys("d'B");
    CHECK(v.marks.size() == 2);
    CHECK(v.view.buffer().text() == "a\nb");
}

TEST_CASE("Jumps are handed up from where they went, a :g once; Ctrl-O, Ctrl-I and Tab step the program's list",
          "[vim][jumps]") {
    Vim v("|a\nb\nc\nd");
    v.keys("Gjk");
    REQUIRE(v.jumps.size() == 1);
    const auto* from = std::get_if<vim::JumpFrom>(&v.jumps[0]);
    REQUIRE(from != nullptr);
    CHECK(from->at == Position{0, 0});
    CHECK(from->version == v.view.document.version());
    v.keys(":2<CR>m'");
    REQUIRE(v.jumps.size() == 3);
    v.keys("<C-o>3<C-o><C-i><Tab>");
    REQUIRE(v.jumps.size() == 7);
    CHECK(std::get<vim::JumpStep>(v.jumps[3]).count == -1);
    CHECK(std::get<vim::JumpStep>(v.jumps[4]).count == -3);
    CHECK(std::get<vim::JumpStep>(v.jumps[5]).count == 1);
    CHECK(std::get<vim::JumpStep>(v.jumps[6]).count == 1);
    // The caret stays: the program moves it.
    CHECK(v.shown() == "a\n|b\nc\nd");
    // A :g is one jump, the lines it goes to none.
    v.jumps.clear();
    v.keys(":g/./1<CR>");
    CHECK(v.jumps.size() == 1);
}

TEST_CASE("q records the keys typed into a register, @ plays them, @@ the last again, a count that many times",
          "[vim][macros]") {
    Vim v("|one\ntwo\nthree\nfour\nfive");
    v.keys("qa");
    CHECK(v.engine.recording() == 'a');
    v.keys("0i- <Esc>jq");
    CHECK_FALSE(v.engine.recording());
    // In Vim's notation, the q that stopped it left out.
    CHECK(v.engine.registers().get('a', nullptr)->text() == "0i-<Space><Esc>j");
    CHECK(v.shown() == "- one\nt|wo\nthree\nfour\nfive");
    v.keys("@a");
    CHECK(v.shown() == "- one\n- two\nt|hree\nfour\nfive");
    v.keys("@@");
    CHECK(v.view.buffer().text() == "- one\n- two\n- three\nfour\nfive");
    // A count plays it so many times, until a command fails: j at the last
    // line.
    v.keys("5@a");
    CHECK(v.view.buffer().text() == "- one\n- two\n- three\n- four\n- five");
    // A capital adds to the register.
    v.keys("qAkq");
    CHECK(v.engine.registers().get('a', nullptr)->text() == "0i-<Space><Esc>jk");
    // The unnamed register keeps what it held.
    CHECK_FALSE(v.engine.registers().get('"', nullptr));

    Vim none("|a");
    none.keys("@@");
    CHECK(none.engine.message().text == "E748: No previously used register");
    none.keys("@z");
    CHECK(none.engine.failed());
    none.keys("q!");
    CHECK_FALSE(none.engine.recording());
    CHECK(none.engine.failed());
}

TEST_CASE("A macro that plays itself goes on until a command in it fails", "[vim][macros]") {
    Vim v("|a\nb\nc");
    // qbq empties b first, so the @b recorded plays nothing yet.
    v.keys("qbqqbA;<Esc>j@bq");
    CHECK(v.engine.registers().get('b', nullptr)->text() == "A;<Esc>j@b");
    v.keys("@b");
    CHECK(v.view.buffer().text() == "a;\nb;\nc;");
}

TEST_CASE("Motions that can't go and keys that make no command fail, as Vim beeps", "[vim][macros]") {
    Vim v("ab|c\nd");
    v.keys("l");
    CHECK(v.engine.failed());
    v.keys("h");
    CHECK_FALSE(v.engine.failed());
    v.keys("jj");
    CHECK(v.engine.failed());
    v.keys("fz");
    CHECK(v.engine.failed());
    v.keys("dfz");
    CHECK(v.engine.failed());
    v.keys("zq");
    CHECK(v.engine.failed());
    v.keys("\"zp");
    CHECK(v.engine.failed());
    CHECK(v.engine.message().text == "E353: Nothing in register z");
    // x on an empty line does nothing, and fails nothing.
    Vim empty("|");
    empty.keys("x");
    CHECK_FALSE(empty.engine.failed());
}

TEST_CASE("Ctrl-O q stops a recording from insert mode, the Ctrl-O left out; :normal plays macros",
          "[vim][macros]") {
    Vim v("|ab");
    v.keys("qcix<C-o>q");
    CHECK_FALSE(v.engine.recording());
    CHECK(v.engine.mode() == vim::Mode::Insert);
    v.keys("<Esc>");
    CHECK(v.engine.registers().get('c', nullptr)->text() == "ix");

    Vim g("|a1\nb\na2\na3");
    g.keys("qdA!<Esc>q");
    g.keys(":g/a/normal @d<CR>");
    CHECK(g.view.buffer().text() == "a1!!\nb\na2!\na3!");
    // A command that fails lets go of the rest of :normal's keys there.
    Vim n("|a\nb");
    n.keys(":%normal jix<CR>");
    CHECK(n.view.buffer().text() == "a\nxb");
}
