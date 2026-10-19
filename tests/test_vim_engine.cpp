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

    void keys(std::string_view typed) {
        const std::optional<std::vector<Chord>> chords = vim::parse_notation(typed);
        REQUIRE(chords);
        for (const Chord& key : *chords) {
            view.folds.catch_up(view.document);
            CommandContext context{.document = view.document, .selections = view.selections, .layout = view.layout,
                                   .clipboard = &view.clipboard, .folds = &view.folds, .text = {}};
            (void)engine.take(key, context);
        }
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

TEST_CASE("Settling puts a caret past a line's end back on its last character", "[vim][engine]") {
    Vim v("ab|");
    CommandContext context{.document = v.view.document, .selections = v.view.selections, .layout = v.view.layout,
                           .clipboard = &v.view.clipboard, .folds = &v.view.folds, .text = {}};
    v.engine.settle(context);
    CHECK(v.shown() == "a|b");
}
