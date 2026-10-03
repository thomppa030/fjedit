#include "fjedit/widget/code_view.hpp"

#include "print.hpp"

#include "ftk/test/kit_harness.hpp"

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
