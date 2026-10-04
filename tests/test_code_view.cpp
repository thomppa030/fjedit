#include "fjedit/widget/code_view.hpp"

#include "print.hpp"

#include "ftk/test/kit_harness.hpp"
#include "ftk/ui/kit/icons.hpp"
#include "ftk/ui/theme.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

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

// The shapes the last frame drew in `colour`, where it drew them.
std::vector<ftk::ui::core::Rect> drawn(const ftk::test::UiHarness& h, ftk::ui::Color colour) {
    const uint32_t packed = ftk::ui::core::pack_colour(colour);
    std::vector<ftk::ui::core::Rect> out;
    for (const ftk::ui::core::RootOutput& root : h.output().roots) {
        for (const ftk::ui::core::Primitive& p : root.draw->primitives) {
            if (p.colour == packed) out.push_back({.min = p.min, .max = p.max});
        }
    }
    return out;
}

constexpr Modifiers CTRL{.ctrl = true};
constexpr Modifiers SHIFT{.shift = true};

// Across the fold chevrons, between the line numbers and the text.
constexpr float FOLD_X = 30.0f;

// Halfway down row `row`.
float row_y(std::size_t row) {
    return (static_cast<float>(row) + 0.5f) * CODE_LINE;
}

// The closed folds' pills: shapes in their colour wider than an indent
// guide, which shares it.
std::vector<ftk::ui::core::Rect> pills(const ftk::test::UiHarness& h) {
    std::vector<ftk::ui::core::Rect> out = drawn(h, ftk::theme::surface_raised());
    std::erase_if(out, [](const ftk::ui::core::Rect& r) { return r.max.x - r.min.x <= 1.0f; });
    return out;
}

// A function folding on lines 0..2, then a line after it.
constexpr std::string_view FUNCTION = "void f() {\n    x();\n}\nint a;";

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

TEST_CASE("A fold's chevron closes it, and the rows after it move up", "[code_view][folds]") {
    Editor e(FUNCTION);
    e.h.click_at({FOLD_X, row_y(0)});
    REQUIRE(e.view.folds.closed_at(0));
    e.h.click_at({780.0f, row_y(1)});
    REQUIRE(e.caret() == Position{3, 6});

    e.h.click_at({FOLD_X, row_y(0)});
    REQUIRE_FALSE(e.view.folds.closed_at(0));
}

TEST_CASE("Without a fold there, a press on the chevrons' column selects the line", "[code_view][folds]") {
    Editor e(FUNCTION);
    e.h.click_at({FOLD_X, row_y(3)});
    REQUIRE(e.view.selections.primary().span() == Span{{3, 0}, {3, 6}});
}

TEST_CASE("A closed fold draws a pill, the closer after it, and opens at a click on it", "[code_view][folds]") {
    Editor e(FUNCTION);
    e.h.click_at({FOLD_X, row_y(0)});
    e.h.step();
    const std::vector<ftk::ui::core::Rect> shown = pills(e.h);
    REQUIRE(shown.size() == 1);
    const auto closer = std::ranges::count_if(drawn(e.h, ftk::theme::code_punctuation()), [&](const auto& glyph) {
        return glyph.min.x >= shown[0].max.x && glyph.max.y <= CODE_LINE;
    });
    REQUIRE(closer == 1);

    const Position caret = e.caret();
    e.h.click_at((shown[0].min + shown[0].max) * 0.5f);
    REQUIRE_FALSE(e.view.folds.closed_at(0));
    REQUIRE(e.caret() == caret);
    e.h.step();
    REQUIRE(pills(e.h).empty());
}

TEST_CASE("Up and down step over a closed fold, and a caret that comes into one opens it", "[code_view][folds]") {
    Editor e(FUNCTION);
    e.h.click_at({FOLD_X, row_y(0)});
    e.h.click_at({780.0f, row_y(0)});
    REQUIRE(e.caret() == Position{0, 10});
    e.h.press(Key::Down);
    REQUIRE(e.caret() == Position{3, 6});
    e.h.press(Key::Up);
    REQUIRE(e.caret() == Position{0, 10});
    REQUIRE(e.view.folds.closed_at(0));

    e.h.press(Key::Right);
    REQUIRE(e.caret() == Position{1, 0});
    REQUIRE_FALSE(e.view.folds.closed_at(0));
}

TEST_CASE("A fold closed over the caret takes it to the end of the fold's first line", "[code_view][folds]") {
    Editor e(FUNCTION);
    e.h.click_at({780.0f, row_y(1)});
    REQUIRE(e.caret() == Position{1, 8});
    e.h.click_at({FOLD_X, row_y(0)});
    REQUIRE(e.view.folds.closed_at(0));
    REQUIRE(e.caret() == Position{0, 10});
}

TEST_CASE("A press on a folded row's number selects every line it holds", "[code_view][folds]") {
    Editor e(FUNCTION);
    e.h.click_at({FOLD_X, row_y(0)});
    e.h.click_at({12.0f, row_y(0)});
    REQUIRE(e.view.selections.primary().span() == Span{{0, 0}, {3, 0}});
}

TEST_CASE("Folds close and open from the keys", "[code_view][folds]") {
    Editor e(FUNCTION);
    e.h.click_at({780.0f, row_y(1)});
    e.h.press(Key::LeftBracket, {.ctrl = true, .shift = true});
    REQUIRE(e.view.folds.closed_at(0));
    e.h.press(Key::RightBracket, {.ctrl = true, .shift = true});
    REQUIRE_FALSE(e.view.folds.closed_at(0));
    e.h.press(Key::K, CTRL);
    e.h.press(Key::Digit0, CTRL);
    REQUIRE(e.view.folds.closed_at(0));
    e.h.press(Key::K, CTRL);
    e.h.press(Key::J, CTRL);
    REQUIRE_FALSE(e.view.folds.closed_at(0));
}

TEST_CASE("Each view keeps its own folds, and they follow edits made in another", "[code_view][folds]") {
    ftk::test::KitHarness h;
    Document document(FUNCTION, language_for("x.cpp"));
    EditorView left(document);
    EditorView right(document);
    const Keymap keys = Keymap::standard();
    h.set_ui([&] {
        (void)code_view("Left", document, left, keys, {.min = {0.0f, 0.0f}, .max = {400.0f, 600.0f}});
        (void)code_view("Right", document, right, keys, {.min = {400.0f, 0.0f}, .max = {800.0f, 600.0f}});
    });
    h.step(2);
    h.click_at({400.0f + FOLD_X, row_y(0)});
    REQUIRE(right.folds.closed_at(0));
    REQUIRE_FALSE(left.folds.closed_at(0));

    h.click_at({200.0f, row_y(0)});
    h.press(Key::Home);
    h.press(Key::Enter);
    h.step();
    REQUIRE(right.folds.closed_at(1));
}

namespace {

// Three uses of "band" over two lines, and one of "bandit".
constexpr std::string_view BANDS = "band = 1;\nwide(band, band);\nbandit();";

// A press on the find plate's item called `label`.
void press_on_plate(Editor& e, std::string_view label, ftk::ui::core::Role role = ftk::ui::core::Role::Button) {
    e.h.step();
    const ftk::ui::core::SemanticNode* item = e.h.find(label, role);
    REQUIRE(item != nullptr);
    e.h.click_at(item->rect.centre());
}

} // namespace

TEST_CASE("Ctrl F opens the find with the word at the caret, its matches washed", "[code_view][find]") {
    Editor e(BANDS);
    e.h.click_at({50.0f, row_y(1)});
    e.h.press(Key::F, CTRL);
    REQUIRE(e.view.find.open);
    REQUIRE(e.view.find.query == "wide");

    // The field has the keyboard, the query selected: typing replaces it.
    e.h.type("band");
    REQUIRE(e.view.find.query == "band");
    e.h.step();
    REQUIRE(e.view.find.matches.size() == 4);
    // The match nearest on from the caret is selected, the rest washed.
    REQUIRE(e.view.selections.primary().span() == Span{{1, 5}, {1, 9}});
    REQUIRE(drawn_in(e.h, ftk::theme::selection_secondary()) == 3);
}

TEST_CASE("Enter goes on to the next match and Shift Enter back, going round", "[code_view][find]") {
    Editor e(BANDS);
    e.h.click_at({780.0f, row_y(0)});
    e.h.press(Key::F, CTRL);
    e.h.type("band");
    e.h.press(Key::Enter);
    REQUIRE(e.view.selections.primary().span() == Span{{1, 11}, {1, 15}});
    e.h.press(Key::Enter);
    e.h.press(Key::Enter);
    REQUIRE(e.view.selections.primary().span() == Span{{0, 0}, {0, 4}});
    e.h.press(Key::Enter, SHIFT);
    REQUIRE(e.view.selections.primary().span() == Span{{2, 0}, {2, 4}});
    // Whole words: bandit is no match.
    press_on_plate(e, "Whole word", ftk::ui::core::Role::Toggle);
    e.h.step();
    REQUIRE(e.view.find.matches.size() == 3);
}

TEST_CASE("Escape in the find closes it and gives the code the keyboard back", "[code_view][find]") {
    Editor e(BANDS);
    e.h.click_at({780.0f, row_y(0)});
    e.h.press(Key::F, CTRL);
    e.h.type("wide");
    e.h.press(Key::Escape);
    REQUIRE_FALSE(e.view.find.open);
    // The match stays selected, and typing replaces it in the code.
    e.h.step();
    e.h.type("narrow");
    REQUIRE(e.text() == "band = 1;\nnarrow(band, band);\nbandit();");
}

TEST_CASE("Escape in the code closes its find before anything else", "[code_view][find]") {
    Editor e(BANDS);
    e.h.click_at({780.0f, row_y(0)});
    e.h.press(Key::F, CTRL);
    e.h.type("band");
    // Clear of the plate over the top right.
    e.h.click_at({200.0f, row_y(2)});
    e.h.press(Key::Left, SHIFT);
    e.h.press(Key::Escape);
    REQUIRE_FALSE(e.view.find.open);
    // The selection stays: Escape went to the find.
    REQUIRE_FALSE(e.view.selections.primary().empty());
}

TEST_CASE("Replace replaces the match selected and goes on, Replace all in one step", "[code_view][find]") {
    Editor e(BANDS);
    e.h.click_at({780.0f, row_y(0)});
    e.h.press(Key::F, CTRL);
    e.h.type("band");
    e.view.find.toggles.whole_word = true;
    e.view.find.stale = true;
    e.view.find.replacement = "strip";
    REQUIRE(e.view.selections.primary().span() == Span{{1, 5}, {1, 9}});

    press_on_plate(e, std::string(ftk::ui::icon::replace) + "  Replace");
    REQUIRE(e.text() == "band = 1;\nwide(strip, band);\nbandit();");
    REQUIRE(e.view.selections.primary().span() == Span{{1, 12}, {1, 16}});

    press_on_plate(e, "Replace all");
    REQUIRE(e.text() == "strip = 1;\nwide(strip, strip);\nbandit();");
    e.h.press(Key::Escape);
    e.h.press(Key::Z, CTRL);
    REQUIRE(e.text() == "band = 1;\nwide(strip, band);\nbandit();");
}

TEST_CASE("F3 goes on to the next match with the find closed", "[code_view][find]") {
    Editor e(BANDS);
    e.h.click_at({780.0f, row_y(0)});
    e.h.press(Key::F, CTRL);
    e.h.type("band");
    e.h.press(Key::Escape);
    e.h.press(Key::F3);
    REQUIRE(e.view.selections.primary().span() == Span{{1, 11}, {1, 15}});
    e.h.press(Key::F3, SHIFT);
    REQUIRE(e.view.selections.primary().span() == Span{{1, 5}, {1, 9}});
}

TEST_CASE("A press on the find's plate never reaches the code under it", "[code_view][find]") {
    Editor e(BANDS);
    e.h.click_at({780.0f, row_y(0)});
    e.h.press(Key::F, CTRL);
    e.h.step();
    const Selections before = e.view.selections;
    // Between the plate's rows, at its right end: the plate itself.
    const ftk::ui::core::SemanticNode* all = e.h.find("Replace all", ftk::ui::core::Role::Button);
    REQUIRE(all != nullptr);
    e.h.click_at({all->rect.max.x + 2.0f, all->rect.min.y - 3.0f});
    REQUIRE(e.view.selections == before);
    REQUIRE(e.view.find.open);
}

TEST_CASE("Go to line reads a line, or a line and a column, counted from 1", "[code_view][go_to_line]") {
    const Buffer buffer("one\ntwo\nth\xC3\xA4ree");
    REQUIRE(go_to_line_target(buffer, "2") == Position{1, 0});
    REQUIRE(go_to_line_target(buffer, " 3 ") == Position{2, 0});
    // Columns are characters, and stop at the line's end.
    REQUIRE(go_to_line_target(buffer, "3:4") == Position{2, 4});
    REQUIRE(go_to_line_target(buffer, "1:99") == Position{0, 3});
    for (const char* none : {"", "0", "4", "x", "2:", "2:0", "-1", "1:x"}) {
        REQUIRE_FALSE(go_to_line_target(buffer, none).has_value());
    }
}

TEST_CASE("Ctrl G goes to the line typed, Enter taking the code back the keyboard", "[code_view][go_to_line]") {
    Editor e(FUNCTION);
    e.h.click_at({780.0f, row_y(0)});
    e.h.press(Key::G, CTRL);
    REQUIRE(e.view.go_to_line.open);
    e.h.type("2:5");
    e.h.press(Key::Enter);
    e.h.step();
    REQUIRE_FALSE(e.view.go_to_line.open);
    REQUIRE(e.caret() == Position{1, 4});
    e.h.type("y");
    REQUIRE(e.text() == "void f() {\n    yx();\n}\nint a;");
}

TEST_CASE("Go to line names the function the line is in", "[code_view][go_to_line]") {
    Editor e(FUNCTION);
    e.h.click_at({780.0f, row_y(3)});
    e.h.press(Key::G, CTRL);
    e.h.step();
    REQUIRE_FALSE(e.view.go_to_line.function.has_value());
    e.h.type("2");
    e.h.step();
    REQUIRE(e.view.go_to_line.function == "f()");
}

TEST_CASE("Escape leaves go to line where the caret was", "[code_view][go_to_line]") {
    Editor e(FUNCTION);
    e.h.click_at({780.0f, row_y(3)});
    e.h.press(Key::G, CTRL);
    e.h.type("1");
    e.h.press(Key::Escape);
    e.h.step();
    REQUIRE_FALSE(e.view.go_to_line.open);
    REQUIRE(e.caret() == Position{3, 6});
    e.h.type("!");
    REQUIRE(e.text() == "void f() {\n    x();\n}\nint a;!");
}

TEST_CASE("Enter on a line there isn't closes go to line and goes nowhere", "[code_view][go_to_line]") {
    Editor e(FUNCTION);
    e.h.click_at({780.0f, row_y(3)});
    e.h.press(Key::G, CTRL);
    e.h.type("99");
    e.h.press(Key::Enter);
    e.h.step();
    REQUIRE_FALSE(e.view.go_to_line.open);
    REQUIRE(e.caret() == Position{3, 6});
}

TEST_CASE("Two views' finds are their own, open at once", "[code_view][find]") {
    ftk::test::KitHarness h;
    Document document(BANDS, language_for("x.cpp"));
    EditorView left(document);
    EditorView right(document);
    const Keymap keys = Keymap::standard();
    h.set_ui([&] {
        (void)code_view("Left", document, left, keys, {.min = {0.0f, 0.0f}, .max = {500.0f, 600.0f}});
        (void)code_view("Right", document, right, keys, {.min = {500.0f, 0.0f}, .max = {1000.0f, 600.0f}});
    });
    h.step(2);
    right.find.open = true;
    h.click_at({100.0f, row_y(4)});
    h.press(Key::F, CTRL);
    h.type("wide");
    REQUIRE(left.find.query == "wide");
    REQUIRE(right.find.query.empty());
}
