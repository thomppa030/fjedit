#include "fjedit/widget/code_view.hpp"

#include "print.hpp"

#include "ftk/test/kit_harness.hpp"
#include "ftk/ui/theme.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using namespace fjedit;
using ftk::ui::Key;
using ftk::ui::Modifiers;

namespace {

// A code view of one document filling an 800 by 600 window.
struct Editor {
    ftk::test::KitHarness h;
    Document document;
    EditorView view;
    Keymap keys = Keymap::standard();

    explicit Editor(std::string_view text, std::string_view file = "x.cpp")
        : document(text, language_for(file)), view(document) {
        h.set_ui([this] { (void)code_view("Code", document, view, keys, {.min = {0.0f, 0.0f}, .max = {800.0f, 600.0f}}); });
        h.step(2);
    }

    [[nodiscard]] std::string text() const { return document.buffer().text(); }
    [[nodiscard]] Position caret() const { return view.selections.primary().caret; }
};

// How many shapes the last frame drew in `colour`.
std::size_t drawn_in(const ftk::test::UiHarness& h, ftk::ui::Color colour) {
    const uint32_t packed = ftk::ui::core::pack_colour(colour);
    std::size_t count = 0;
    for (const ftk::ui::core::RootOutput& root : h.output().roots) {
        for (const ftk::ui::core::Primitive& p : root.draw->primitives) count += p.colour == packed ? 1 : 0;
    }
    return count;
}

constexpr Modifiers CTRL{.ctrl = true};
constexpr Modifiers SHIFT{.shift = true};

} // namespace

TEST_CASE("The view records itself, by its label", "[code_view]") {
    Editor e("int a;");
    REQUIRE(e.h.find("Code", ftk::ui::core::Role::TextField) != nullptr);
}

TEST_CASE("A click gives the view the keyboard and places the caret", "[code_view]") {
    Editor e("abc");
    // Far right of the only line: its end.
    e.h.click_at({780.0f, 10.0f});
    REQUIRE(e.caret() == Position{0, 3});
    e.h.type("de");
    REQUIRE(e.text() == "abcde");
    REQUIRE(e.caret() == Position{0, 5});
}

TEST_CASE("A click below the last line places the caret on it", "[code_view]") {
    Editor e("one\ntwo");
    e.h.click_at({780.0f, 500.0f});
    REQUIRE(e.caret() == Position{1, 3});
}

TEST_CASE("Keys run their commands through the keymap", "[code_view]") {
    Editor e("abc");
    e.h.click_at({780.0f, 10.0f});
    e.h.press(Key::Left);
    e.h.press(Key::Left, SHIFT);
    REQUIRE(e.view.selections.primary().span() == Span{{0, 1}, {0, 2}});
    e.h.type("X");
    REQUIRE(e.text() == "aXc");
    e.h.press(Key::Z, CTRL);
    REQUIRE(e.text() == "abc");
}

TEST_CASE("Enter indents a new line through the view", "[code_view]") {
    Editor e("f() {");
    e.h.click_at({780.0f, 10.0f});
    e.h.press(Key::Enter);
    REQUIRE(e.text() == "f() {\n    ");
}

TEST_CASE("Tab indents, and the keyboard stays with the view", "[code_view]") {
    Editor e("x");
    e.h.click_at({780.0f, 10.0f});
    e.h.press(Key::Tab);
    e.h.type("y");
    REQUIRE(e.text() == "x   y");
}

TEST_CASE("Text typed with Ctrl is a shortcut's, with Ctrl and Alt AltGr's", "[code_view]") {
    Editor e("");
    e.h.click_at({780.0f, 10.0f});
    e.h.hold(CTRL);
    e.h.type("q");
    e.h.hold({.ctrl = true, .alt = true});
    e.h.type("{");
    e.h.hold({});
    REQUIRE(e.text() == "{");
}

TEST_CASE("Copy and paste go through the core's clipboard", "[code_view]") {
    Editor e("ab");
    e.h.click_at({780.0f, 10.0f});
    e.h.press(Key::A, CTRL);
    e.h.press(Key::C, CTRL);
    REQUIRE(e.h.ui().clipboard() == "ab");
    e.h.press(Key::End, CTRL);
    e.h.press(Key::V, CTRL);
    REQUIRE(e.text() == "abab");
}

TEST_CASE("A shift-click selects from the caret to where it lands", "[code_view]") {
    Editor e("one\ntwo");
    e.h.click_at({780.0f, 10.0f});
    e.h.hold(SHIFT);
    e.h.click_at({780.0f, 30.0f});
    e.h.hold({});
    REQUIRE(e.view.selections.primary().span() == Span{{0, 3}, {1, 3}});
}

TEST_CASE("The wheel scrolls, never past the start", "[code_view]") {
    std::string text;
    for (int i = 0; i < 100; ++i) text += "line\n";
    Editor e(text);
    e.h.wheel({400.0f, 300.0f}, -2.0f);
    REQUIRE(e.view.scroll.y > 0.0f);
    e.h.wheel({400.0f, 300.0f}, 10.0f);
    REQUIRE(e.view.scroll.y == 0.0f);
}

TEST_CASE("A caret moved out of sight brings the view after it", "[code_view]") {
    std::string text;
    for (int i = 0; i < 100; ++i) text += "line\n";
    Editor e(text);
    e.h.click_at({780.0f, 10.0f});
    e.h.press(Key::End, CTRL);
    REQUIRE(e.caret().line == 100);
    const float top = 100.0f * CODE_LINE - e.view.scroll.y;
    REQUIRE(top >= 0.0f);
    REQUIRE(top + CODE_LINE <= 600.0f);
}

TEST_CASE("A second view of the document follows its edits", "[code_view]") {
    ftk::test::KitHarness h;
    Document document("one\ntwo", language_for("x.txt"));
    EditorView left(document);
    EditorView right(document);
    right.selections = Selections(Selection::at({1, 1}));
    const Keymap keys = Keymap::standard();
    h.set_ui([&] {
        (void)code_view("Left", document, left, keys, {.min = {0.0f, 0.0f}, .max = {400.0f, 600.0f}});
        (void)code_view("Right", document, right, keys, {.min = {400.0f, 0.0f}, .max = {800.0f, 600.0f}});
    });
    h.step(2);
    h.click_at({200.0f, 10.0f});
    h.press(Key::Home);
    h.press(Key::Home);
    h.press(Key::Enter);
    h.step();
    REQUIRE(document.buffer().text() == "\none\ntwo");
    REQUIRE(right.selections.primary() == Selection::at({2, 1}));
}

TEST_CASE("A double click selects the word, a triple click the line", "[code_view][mouse]") {
    Editor e("alpha beta\ngamma");
    e.h.double_click_at({780.0f, 10.0f});
    REQUIRE(e.view.selections.primary().span() == Span{{0, 6}, {0, 10}});
    e.h.click_at({780.0f, 10.0f});
    REQUIRE(e.view.selections.primary().span() == Span{{0, 0}, {1, 0}});
}

TEST_CASE("A drag selects from where it was pressed", "[code_view][mouse]") {
    Editor e("one\ntwo\nthree");
    e.h.drag({780.0f, 10.0f}, {780.0f, 50.0f});
    REQUIRE(e.view.selections.primary() == Selection{.anchor = {0, 3}, .caret = {2, 5}});
}

TEST_CASE("A press on the line numbers selects the line, a drag there more lines", "[code_view][mouse]") {
    Editor e("one\ntwo\nthree");
    e.h.click_at({12.0f, 30.0f});
    REQUIRE(e.view.selections.primary().span() == Span{{1, 0}, {2, 0}});
    e.h.drag({12.0f, 50.0f}, {12.0f, 10.0f});
    REQUIRE(e.view.selections.primary() == Selection{.anchor = {2, 5}, .caret = {0, 0}});
}

TEST_CASE("Alt and a click adds a caret", "[code_view][mouse]") {
    Editor e("one\ntwo");
    e.h.click_at({780.0f, 10.0f});
    e.h.hold({.alt = true});
    e.h.click_at({780.0f, 30.0f});
    e.h.hold({});
    REQUIRE(e.view.selections.size() == 2);
    e.h.type("!");
    REQUIRE(e.text() == "one!\ntwo!");
}

TEST_CASE("Alt and a drag makes a column", "[code_view][mouse]") {
    // Lines long enough to reach across the view, so the column is inside each.
    const std::string line(100, 'a');
    Editor e(line + "\n" + line + "\n" + line);
    e.h.hold({.alt = true});
    e.h.drag({400.0f, 10.0f}, {440.0f, 50.0f});
    e.h.hold({});
    REQUIRE(e.view.selections.size() == 3);
    e.h.type("d");
    const Buffer& buffer = e.document.buffer();
    REQUIRE(buffer.line(0).find('d') != std::string_view::npos);
    REQUIRE(buffer.line(0) == buffer.line(1));
    REQUIRE(buffer.line(1) == buffer.line(2));
    REQUIRE(buffer.line(0).size() < 100);
}

TEST_CASE("A drag past the bottom scrolls the view on", "[code_view][mouse]") {
    std::string text;
    for (int i = 0; i < 100; ++i) text += "line\n";
    Editor e(text);
    e.h.drag({780.0f, 10.0f}, {780.0f, 700.0f});
    REQUIRE(e.view.scroll.y > 0.0f);
    REQUIRE(e.caret().line > 29);
}

TEST_CASE("The bracket paired with the one at the caret is outlined, both", "[code_view][marks]") {
    Editor e("f(a)");
    e.h.click_at({780.0f, 10.0f});
    REQUIRE(drawn_in(e.h, ftk::theme::code_bracket()) == 2);
    e.h.press(Key::Home);
    e.h.step();
    REQUIRE(drawn_in(e.h, ftk::theme::code_bracket()) == 0);
}

TEST_CASE("The other uses of the word at the caret are boxed", "[code_view][marks]") {
    Editor e("spin + spinning + spin\nspin");
    e.h.click_at({780.0f, 30.0f});
    REQUIRE(drawn_in(e.h, ftk::theme::code_occurrence()) == 2);
}

TEST_CASE("Indent guides, the caret's block brighter", "[code_view][marks]") {
    Editor e("a {\n    b {\n        c;\n    }\n}");
    e.h.click_at({780.0f, 10.0f});
    // Line 1 and 3 one level in, line 2 two: four guides, none active.
    REQUIRE(drawn_in(e.h, ftk::theme::code_indent_guide()) == 4);
    REQUIRE(drawn_in(e.h, ftk::theme::code_indent_guide_active()) == 0);
    e.h.press(Key::Down);
    e.h.press(Key::Down);
    e.h.step();
    // On c: the second guide of line 2 is the caret's block's.
    REQUIRE(drawn_in(e.h, ftk::theme::code_indent_guide_active()) == 1);
}
