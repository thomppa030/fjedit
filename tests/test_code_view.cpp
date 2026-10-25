#include "fjedit/core/language.hpp"
#include "fjedit/core/vim/notation.hpp"
#include "fjedit/widget/code_view.hpp"
#include "fjedit/widget/completion.hpp"
#include "fjedit/widget/minimap.hpp"

#include "fake_language.hpp"
#include "print.hpp"

#include "ftk/test/kit_harness.hpp"
#include "ftk/ui/kit/icons.hpp"
#include "ftk/ui/theme.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
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
    // What the view handed out, in whichever frame it did.
    std::optional<Location> went{};
    std::optional<Rename> renamed{};
    std::vector<vim::ExCommand> handed{};
    std::vector<vim::JumpFrom> jumped{};
    // Steps of text size asked for, all frames together.
    int zoomed{0};

    explicit Editor(std::string_view text, std::string_view file = "x.cpp")
        : document(text, language_for(file)), view(document) {
        h.set_ui([this] {
            const CodeViewResult result = code_view("Code", document, view, keys, {.min = {0.0f, 0.0f}, .max = {800.0f, 600.0f}});
            if (result.go) went = result.go;
            if (result.rename) renamed = result.rename;
            for (const vim::Asked& asked : result.asked) {
                if (const auto* command = std::get_if<vim::ExCommand>(&asked)) handed.push_back(*command);
                if (const auto* jump = std::get_if<vim::JumpFrom>(&asked)) jumped.push_back(*jump);
            }
            zoomed += result.zoom;
        });
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

// Right of every test line's end, left of the minimap and scrollbar the
// code stops short of.
constexpr float PAST_END = 640.0f;
constexpr float CODE_RIGHT = 800.0f - MINIMAP_WIDTH - 10.0f;

// What the last frame drew in `colour` in the code, left of its side.
std::vector<ftk::ui::core::Rect> drawn_in_code(const ftk::test::UiHarness& h, ftk::ui::Color colour) {
    std::vector<ftk::ui::core::Rect> out = drawn(h, colour);
    std::erase_if(out, [](const ftk::ui::core::Rect& r) { return r.max.x > CODE_RIGHT; });
    return out;
}

// Halfway down row `row`.
float row_y(std::size_t row) {
    return (static_cast<float>(row) + 0.5f) * CODE_LINE;
}

// The closed folds' pills: shapes in their colour in the code wider than an
// indent guide, which shares it.
std::vector<ftk::ui::core::Rect> pills(const ftk::test::UiHarness& h) {
    std::vector<ftk::ui::core::Rect> out = drawn_in_code(h, ftk::theme::surface_raised());
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
    e.h.click_at({PAST_END, 10.0f});
    REQUIRE(e.caret() == Position{0, 3});
    e.h.type("de");
    REQUIRE(e.text() == "abcde");
    REQUIRE(e.caret() == Position{0, 5});
}

TEST_CASE("A click below the last line places the caret on it", "[code_view]") {
    Editor e("one\ntwo");
    e.h.click_at({PAST_END, 500.0f});
    REQUIRE(e.caret() == Position{1, 3});
}

TEST_CASE("Keys run their commands through the keymap", "[code_view]") {
    Editor e("abc");
    e.h.click_at({PAST_END, 10.0f});
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
    e.h.click_at({PAST_END, 10.0f});
    e.h.press(Key::Enter);
    REQUIRE(e.text() == "f() {\n    ");
}

TEST_CASE("Tab indents, and the keyboard stays with the view", "[code_view]") {
    Editor e("x");
    e.h.click_at({PAST_END, 10.0f});
    e.h.press(Key::Tab);
    e.h.type("y");
    REQUIRE(e.text() == "x   y");
}

TEST_CASE("Text typed with Ctrl is a shortcut's, with Ctrl and Alt AltGr's", "[code_view]") {
    Editor e("");
    e.h.click_at({PAST_END, 10.0f});
    e.h.hold(CTRL);
    e.h.type("q");
    e.h.hold({.ctrl = true, .alt = true});
    e.h.type("{");
    e.h.hold({});
    REQUIRE(e.text() == "{");
}

TEST_CASE("Keys and text that share a frame run in the order they came, each one", "[code_view]") {
    using ftk::ui::core::KeyEvent;
    Editor e("abc");
    e.h.click_at({PAST_END, 10.0f});
    // Left twice in one frame moves the caret twice.
    for (int i = 0; i < 2; ++i) {
        e.h.send(KeyEvent{.kind = KeyEvent::Kind::Down, .key = Key::Left});
        e.h.send(KeyEvent{.kind = KeyEvent::Kind::Up, .key = Key::Left});
    }
    e.h.step();
    REQUIRE(e.caret() == Position{0, 1});
    // Text, Enter, then text: the line breaks between the two.
    e.h.ui().add_text("x");
    e.h.send(KeyEvent{.kind = KeyEvent::Kind::Down, .key = Key::Enter});
    e.h.send(KeyEvent{.kind = KeyEvent::Kind::Up, .key = Key::Enter});
    e.h.ui().add_text("y");
    e.h.step();
    REQUIRE(e.text() == "ax\nybc");
}

TEST_CASE("Copy and paste go through the core's clipboard", "[code_view]") {
    Editor e("ab");
    e.h.click_at({PAST_END, 10.0f});
    e.h.press(Key::A, CTRL);
    e.h.press(Key::C, CTRL);
    REQUIRE(e.h.ui().clipboard() == "ab");
    e.h.press(Key::End, CTRL);
    e.h.press(Key::V, CTRL);
    REQUIRE(e.text() == "abab");
}

TEST_CASE("A shift-click selects from the caret to where it lands", "[code_view]") {
    Editor e("one\ntwo");
    e.h.click_at({PAST_END, 10.0f});
    e.h.hold(SHIFT);
    e.h.click_at({PAST_END, 30.0f});
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

TEST_CASE("Ctrl and the wheel ask for the text to grow or shrink, and scroll nothing", "[code_view]") {
    std::string text;
    for (int i = 0; i < 100; ++i) text += "line\n";
    Editor e(text);
    e.h.hold(CTRL);
    e.h.wheel({400.0f, 300.0f}, 2.0f);
    REQUIRE(e.zoomed == 2);
    e.h.wheel({400.0f, 300.0f}, -3.0f);
    REQUIRE(e.zoomed == -1);
    REQUIRE(e.view.scroll.y == 0.0f);
    // A touchpad's small turns add up to a step.
    e.h.wheel({400.0f, 300.0f}, 0.5f);
    REQUIRE(e.zoomed == -1);
    e.h.wheel({400.0f, 300.0f}, 0.5f);
    REQUIRE(e.zoomed == 0);
    // Over the minimap too.
    e.h.wheel({CODE_RIGHT + 40.0f, 300.0f}, 1.0f);
    REQUIRE(e.zoomed == 1);
    e.h.hold({});
}

TEST_CASE("A new text size keeps the line at the top where it was", "[code_view]") {
    std::string text;
    for (int i = 0; i < 100; ++i) text += "line\n";
    Editor e(text);
    e.view.scroll.y = 10.0f * code_line(CODE_TEXT);
    e.h.step();
    e.view.text_size = 2.0f * CODE_TEXT;
    e.h.step();
    REQUIRE(e.view.scroll.y == 10.0f * code_line(2.0f * CODE_TEXT));
}

TEST_CASE("A caret moved out of sight brings the view after it", "[code_view]") {
    std::string text;
    for (int i = 0; i < 100; ++i) text += "line\n";
    Editor e(text);
    e.h.click_at({PAST_END, 10.0f});
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
    e.h.double_click_at({PAST_END, 10.0f});
    REQUIRE(e.view.selections.primary().span() == Span{{0, 6}, {0, 10}});
    e.h.click_at({PAST_END, 10.0f});
    REQUIRE(e.view.selections.primary().span() == Span{{0, 0}, {1, 0}});
}

TEST_CASE("A drag selects from where it was pressed", "[code_view][mouse]") {
    Editor e("one\ntwo\nthree");
    e.h.drag({PAST_END, 10.0f}, {PAST_END, 50.0f});
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
    e.h.click_at({PAST_END, 10.0f});
    e.h.hold({.alt = true});
    e.h.click_at({PAST_END, 30.0f});
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
    e.h.drag({PAST_END, 10.0f}, {PAST_END, 700.0f});
    REQUIRE(e.view.scroll.y > 0.0f);
    REQUIRE(e.caret().line > 29);
}

TEST_CASE("The bracket paired with the one at the caret is outlined, both", "[code_view][marks]") {
    Editor e("f(a)");
    e.h.click_at({PAST_END, 10.0f});
    REQUIRE(drawn_in(e.h, ftk::theme::code_bracket()) == 2);
    e.h.press(Key::Home);
    e.h.step();
    REQUIRE(drawn_in(e.h, ftk::theme::code_bracket()) == 0);
}

TEST_CASE("The other uses of the word at the caret are boxed", "[code_view][marks]") {
    Editor e("spin + spinning + spin\nspin");
    e.h.click_at({PAST_END, 30.0f});
    REQUIRE(drawn_in(e.h, ftk::theme::code_occurrence()) == 2);
}

TEST_CASE("Indent guides, the caret's block brighter", "[code_view][marks]") {
    Editor e("a {\n    b {\n        c;\n    }\n}");
    e.h.click_at({PAST_END, 10.0f});
    // Line 1 and 3 one level in, line 2 two: four guides, none active.
    REQUIRE(drawn_in_code(e.h, ftk::theme::code_indent_guide()).size() == 4);
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
    e.h.click_at({PAST_END, row_y(1)});
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
    e.h.click_at({PAST_END, row_y(0)});
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
    e.h.click_at({PAST_END, row_y(1)});
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
    e.h.click_at({PAST_END, row_y(1)});
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
    e.h.click_at({PAST_END, row_y(0)});
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
    e.h.click_at({PAST_END, row_y(0)});
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
    e.h.click_at({PAST_END, row_y(0)});
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
    e.h.click_at({PAST_END, row_y(0)});
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
    e.h.click_at({PAST_END, row_y(0)});
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
    e.h.click_at({PAST_END, row_y(0)});
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
    e.h.click_at({PAST_END, row_y(0)});
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
    e.h.click_at({PAST_END, row_y(3)});
    e.h.press(Key::G, CTRL);
    e.h.step();
    REQUIRE_FALSE(e.view.go_to_line.function.has_value());
    e.h.type("2");
    e.h.step();
    REQUIRE(e.view.go_to_line.function == "f()");
}

TEST_CASE("Escape leaves go to line where the caret was", "[code_view][go_to_line]") {
    Editor e(FUNCTION);
    e.h.click_at({PAST_END, row_y(3)});
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
    e.h.click_at({PAST_END, row_y(3)});
    e.h.press(Key::G, CTRL);
    e.h.type("99");
    e.h.press(Key::Enter);
    e.h.step();
    REQUIRE_FALSE(e.view.go_to_line.open);
    REQUIRE(e.caret() == Position{3, 6});
}

namespace {

// `colour` at `share` of its strength, as the minimap draws code in it.
ftk::ui::Color faint(ftk::ui::Color colour, float share) {
    colour.a *= share;
    return colour;
}

// Three hundred numbered lines, for scrolling.
std::string many_lines() {
    std::string text;
    for (int i = 0; i < 300; ++i) text += "int line_" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    return text;
}

} // namespace

TEST_CASE("The minimap draws a bar for each word in its token's colour, comments fainter", "[code_view][minimap]") {
    Editor e("int a; // two words");
    e.h.step();
    REQUIRE(drawn(e.h, faint(ftk::theme::code_keyword(), 0.8f)).size() == 1);
    REQUIRE(drawn(e.h, faint(ftk::theme::code_comment(), 0.55f)).size() == 3);
}

TEST_CASE("A press on the minimap scrolls the view there", "[code_view][minimap]") {
    Editor e(many_lines());
    e.h.click_at({CODE_RIGHT + 40.0f, 590.0f});
    e.h.step();
    REQUIRE(e.view.scroll.y > 5000.0f);
    e.h.click_at({CODE_RIGHT + 40.0f, 5.0f});
    e.h.step();
    REQUIRE(e.view.scroll.y < 100.0f);
}

TEST_CASE("The scrollbar and the wheel over the minimap scroll the view", "[code_view][minimap]") {
    Editor e(many_lines());
    e.h.drag({795.0f, 5.0f}, {795.0f, 300.0f});
    e.h.step();
    REQUIRE(e.view.scroll.y > 2000.0f);
    const float scrolled = e.view.scroll.y;
    e.h.wheel({CODE_RIGHT + 40.0f, 300.0f}, -3.0f);
    e.h.step();
    REQUIRE(e.view.scroll.y > scrolled);
}

TEST_CASE("The minimap ticks the find's matches and the caret", "[code_view][minimap]") {
    Editor e(BANDS);
    e.h.click_at({PAST_END, row_y(0)});
    e.h.press(Key::F, CTRL);
    e.h.type("band");
    e.h.step();
    const auto ticks = [&](ftk::ui::Color colour) {
        return std::ranges::count_if(drawn(e.h, colour), [](const ftk::ui::core::Rect& r) {
            return r.max.x == 790.0f && r.max.x - r.min.x == 6.0f;
        });
    };
    // Matches on all three lines; the caret's own tick on the first match's.
    REQUIRE(ticks(ftk::theme::accent()) == 3);
    REQUIRE(ticks(ftk::theme::text()) == 1);
}

TEST_CASE("Without the minimap the code reaches the scrollbar", "[code_view][minimap]") {
    Editor e(std::string(100, 'x'));
    e.view.minimap = false;
    e.h.step();
    e.h.click_at({760.0f, 10.0f});
    REQUIRE(e.caret().byte > 80);
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

TEST_CASE("What an input method composes shows at the caret, and only its result is typed", "[code_view][ime]") {
    Editor e("ab");
    e.h.click_at({PAST_END, 10.0f});
    e.h.press(Key::Left);
    e.h.compose("ka");
    e.h.step();
    REQUIRE(e.text() == "ab");
    // Underlined: a level line in the text colour.
    const auto underlines = std::ranges::count_if(drawn_in_code(e.h, ftk::theme::text()), [](const auto& r) {
        return r.max.y - r.min.y < 1.5f && r.max.x - r.min.x > 4.0f;
    });
    REQUIRE(underlines == 1);

    e.h.compose("");
    e.h.type("\xE3\x81\x8B");
    REQUIRE(e.text() == "a\xE3\x81\x8B" "b");
}

TEST_CASE("Commands queued on a view run at its next draw, without the keyboard", "[code_view]") {
    Editor e("alpha beta");
    e.view.queued = {"select.all", "find.open"};
    e.h.step();
    REQUIRE(e.view.selections.primary().span() == Span{{0, 0}, {0, 10}});
    REQUIRE(e.view.find.open);
    REQUIRE(e.view.queued.empty());
}

TEST_CASE("A view asked to take the keyboard takes it at its next draw", "[code_view]") {
    Editor e("abc");
    e.view.focus = true;
    e.h.step();
    e.h.type("x");
    REQUIRE(e.text() == "xabc");
}

TEST_CASE("A larger text size spreads the lines further apart", "[code_view]") {
    Editor e("a\nb\nc\nd");
    e.view.text_size = 2.0f * CODE_TEXT;
    e.h.step();
    // Lines 40 apart: 50 down is the second.
    e.h.click_at({PAST_END, 50.0f});
    REQUIRE(e.caret().line == 1);
}

TEST_CASE("Relative line numbers count rows from the caret's line, which keeps its own", "[code_view][numbers]") {
    const Document document(FUNCTION, language_for("x.cpp"));
    Folds folds;
    CHECK(line_number_shown(LineNumbers::Absolute, folds, 3, 0) == 4);
    CHECK(line_number_shown(LineNumbers::Relative, folds, 3, 0) == 3);
    CHECK(line_number_shown(LineNumbers::Relative, folds, 0, 3) == 3);
    CHECK(line_number_shown(LineNumbers::Relative, folds, 1, 1) == 2);
    // The function closed is one row, so the line after it is one away.
    REQUIRE(folds.close(document, 0));
    CHECK(line_number_shown(LineNumbers::Relative, folds, 3, 0) == 1);
    CHECK(line_number_shown(LineNumbers::Relative, folds, 0, 3) == 1);
}

TEST_CASE("Relative, the caret's own line number goes against the gutter's left", "[code_view][numbers]") {
    // Twelve lines, so the gutter is two digits wide, and the caret on the
    // second: its "2" is the only number in the brighter colour.
    Editor e("a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl");
    e.h.click_at({PAST_END, row_y(1)});
    const auto caret_number_left = [&] {
        float left = 1e9f;
        for (const ftk::ui::core::Rect& r : drawn(e.h, ftk::theme::text_secondary())) {
            if (r.max.x < FOLD_X) left = std::min(left, r.min.x);
        }
        return left;
    };
    const float right_aligned = caret_number_left();
    e.view.line_numbers = LineNumbers::Relative;
    e.h.step();
    const float left_aligned = caret_number_left();
    CHECK(left_aligned < 12.0f);
    CHECK(left_aligned < right_aligned - 4.0f);
}

namespace {

// A hundred short lines: the 600 high view shows thirty of them.
std::string hundred_lines() {
    std::string text;
    for (int i = 0; i < 100; ++i) text += "line\n";
    text.pop_back();
    return text;
}

} // namespace

TEST_CASE("Rows are kept in sight around the caret as the keys move it", "[code_view][scroll]") {
    Editor e(hundred_lines());
    e.view.around_caret = 5;
    e.h.click_at({PAST_END, row_y(0)});
    for (int i = 0; i < 25; ++i) e.h.press(Key::Down);
    // The caret on row 25 with five rows under it: rows 1 to 30 in sight.
    REQUIRE(e.caret().line == 25);
    CHECK(e.view.scroll.y == 20.0f);
    // Up near the top, the rows above it come back into sight.
    for (int i = 0; i < 20; ++i) e.h.press(Key::Up);
    CHECK(e.view.scroll.y == 0.0f);
}

TEST_CASE("Rows kept around the caret never reach past the text's end", "[code_view][scroll]") {
    Editor e(hundred_lines());
    e.view.around_caret = 5;
    e.h.click_at({PAST_END, row_y(0)});
    e.h.press(Key::End, CTRL);
    // The last line at the view's foot, nothing past it.
    CHECK(e.view.scroll.y == 100.0f * CODE_LINE - 600.0f);
}

TEST_CASE("More rows kept around the caret than fit keep it in the middle", "[code_view][scroll]") {
    Editor e(hundred_lines());
    e.view.around_caret = 100;
    // A command moving the caret, as go to line does.
    e.view.selections = Selections(Selection::at({50, 0}));
    e.view.reveal = true;
    e.h.step();
    // Fourteen rows each side of row 50 fit in thirty, so row 64 is the last
    // in sight.
    CHECK(e.view.scroll.y == 65.0f * CODE_LINE - 600.0f);
}

TEST_CASE("A click near the edge never scrolls for the rows kept around the caret", "[code_view][scroll]") {
    Editor e(hundred_lines());
    e.view.around_caret = 5;
    e.h.click_at({PAST_END, row_y(28)});
    REQUIRE(e.caret().line == 28);
    CHECK(e.view.scroll.y == 0.0f);
    // The next key brings them in.
    e.h.press(Key::Down);
    CHECK(e.view.scroll.y == 35.0f * CODE_LINE - 600.0f);
}

namespace {

// Whether every shape in `shapes` lies on row `row`.
bool on_row(const std::vector<ftk::ui::core::Rect>& shapes, std::size_t row) {
    const float top = static_cast<float>(row) * CODE_LINE;
    return std::ranges::all_of(shapes, [&](const ftk::ui::core::Rect& r) {
        return r.min.y >= top - 1.0f && r.max.y <= top + CODE_LINE + 1.0f;
    });
}

} // namespace

TEST_CASE("A problem tints its line, underlines its code and says what it is after the line", "[code_view][problems]") {
    FakeLanguage language;
    language.problems = {{.span = {{1, 4}, {1, 5}},
                          .severity = Severity::Error,
                          .message = "use of undeclared identifier 'x'\nnote: declared nowhere"}};
    Editor e(FUNCTION);
    e.view.service = &language;
    e.h.step(2);

    const std::vector<ftk::ui::core::Rect> tint = drawn_in_code(e.h, ftk::theme::code_error_line());
    REQUIRE(tint.size() == 1);
    REQUIRE(on_row(tint, 1));
    // The wave, under x alone, in the lower part of its row.
    const std::vector<ftk::ui::core::Rect> wave = drawn_in_code(e.h, ftk::theme::error());
    REQUIRE_FALSE(wave.empty());
    REQUIRE(on_row(wave, 1));
    REQUIRE(std::ranges::all_of(wave, [](const auto& r) { return r.min.y > row_y(1); }));
    // The message's first line, with its icon, past the end of the code.
    const std::vector<ftk::ui::core::Rect> message = drawn_in_code(e.h, ftk::theme::code_error_text());
    REQUIRE(message.size() > 20);
    REQUIRE(message.size() < 40);
    REQUIRE(on_row(message, 1));
}

TEST_CASE("The caret's line takes a problem's tint over its own", "[code_view][problems]") {
    FakeLanguage language;
    language.problems = {{.span = {{1, 4}, {1, 5}}, .severity = Severity::Warning, .message = "unused"}};
    Editor e(FUNCTION);
    e.view.service = &language;
    e.view.selections = Selections(Selection::at({1, 0}));
    e.h.step(2);
    REQUIRE(drawn_in_code(e.h, ftk::theme::code_current_line()).empty());
    REQUIRE(on_row(drawn_in_code(e.h, ftk::theme::code_warning_line()), 1));
    REQUIRE_FALSE(drawn_in_code(e.h, ftk::theme::code_warning_text()).empty());
}

TEST_CASE("A problem's row is marked beside the code, and what is less than a warning shows only there", "[code_view][problems]") {
    FakeLanguage language;
    language.problems = {{.span = {{2, 0}, {2, 1}}, .severity = Severity::Error, .message = "error"},
                         {.span = {{3, 0}, {3, 3}}, .severity = Severity::Information, .message = "a note"}};
    Editor e(FUNCTION);
    e.view.service = &language;
    e.h.step(2);
    std::vector<ftk::ui::core::Rect> beside = drawn(e.h, ftk::theme::error());
    std::erase_if(beside, [](const ftk::ui::core::Rect& r) { return r.min.x < CODE_RIGHT; });
    REQUIRE_FALSE(beside.empty());
    // The information leaves no tint and no message.
    REQUIRE(drawn_in_code(e.h, ftk::theme::code_error_line()).size() == 1);
    REQUIRE(on_row(drawn_in_code(e.h, ftk::theme::code_error_line()), 2));
}


TEST_CASE("A line is drawn in its service's colours over its lexer's", "[code_view][colours]") {
    FakeLanguage language;
    Editor e(FUNCTION);
    e.view.service = &language;
    e.h.step(2);
    // The lexer takes the a of "int a;" for a plain name.
    REQUIRE(drawn_in_code(e.h, ftk::theme::code_member()).empty());

    fjedit::SemanticTokens colours;
    colours.lines.resize(4);
    colours.lines[3] = {{.begin = 4, .end = 5, .kind = TokenKind::Member}};
    language.colours = colours;
    e.h.step(2);
    const std::vector<ftk::ui::core::Rect> member = drawn_in_code(e.h, ftk::theme::code_member());
    REQUIRE(member.size() == 1);
    REQUIRE(on_row(member, 3));
}

namespace {

// Steps `e` through `seconds` of frames.
void wait(Editor& e, double seconds) {
    e.h.step(static_cast<int>(seconds * 60.0) + 1);
}

// The card's surface: shapes in the sunken colour in the code.
std::vector<ftk::ui::core::Rect> cards(const ftk::test::UiHarness& h) {
    return drawn_in_code(h, ftk::theme::surface_sunken());
}

// The middle of the x of FUNCTION's "    x();", where it was drawn in the
// colour of a function called.
glm::vec2 x_called(const ftk::test::UiHarness& h) {
    std::vector<ftk::ui::core::Rect> glyphs = drawn_in_code(h, ftk::theme::code_function());
    std::erase_if(glyphs, [](const auto& r) { return r.max.y < static_cast<float>(1) * CODE_LINE || r.min.y > 2.0f * CODE_LINE; });
    REQUIRE(glyphs.size() == 1);
    return glyphs.front().centre();
}

} // namespace

TEST_CASE("Resting on a word asks what it is, and the answer hangs under it", "[code_view][hover]") {
    FakeLanguage language;
    Editor e(FUNCTION);
    e.view.service = &language;
    e.view.path = "x.cpp";
    const glm::vec2 x_at = x_called(e.h);
    e.h.move_to(x_at);
    e.h.step(2);
    REQUIRE(language.hovers.empty());
    wait(e, 0.6);
    REQUIRE(language.hovers.size() == 1);
    REQUIRE(language.hovers.front().path == "x.cpp");
    REQUIRE(language.hovers.front().at.line == 1);
    REQUIRE(cards(e.h).empty());

    FakeLanguage::answer(language.hovers.front(),
                         std::optional<Hover>(Hover{.markdown = "### function `x`\n\n---\n→ `void`", .span = {}}));
    e.h.step(2);
    const std::vector<ftk::ui::core::Rect> card = cards(e.h);
    REQUIRE(card.size() == 1);
    REQUIRE(card.front().min.y >= static_cast<float>(2) * CODE_LINE);
    // The heading's name in the text colour, on the card.
    REQUIRE(std::ranges::any_of(drawn_in_code(e.h, ftk::theme::text()),
                                [&](const auto& r) { return card.front().contains(r.centre()); }));

    // Onto the card it stays; away from the word and the card it goes.
    e.h.move_to(card.front().centre());
    e.h.step(2);
    REQUIRE(cards(e.h).size() == 1);
    e.h.move_to({PAST_END, row_y(3)});
    e.h.step(2);
    REQUIRE(cards(e.h).empty());
}

TEST_CASE("A hover card taller than the view stays inside it, the wheel over it scrolling it and not the code",
          "[code_view][hover]") {
    FakeLanguage language;
    Editor e(FUNCTION);
    e.view.service = &language;
    e.view.path = "x.cpp";
    e.h.move_to(x_called(e.h));
    wait(e, 0.6);
    REQUIRE(language.hovers.size() == 1);
    std::string markdown;
    for (int i = 0; i < 80; ++i) markdown += "Line " + std::to_string(i) + " of a long answer.\n\n";
    FakeLanguage::answer(language.hovers.front(), std::optional<Hover>(Hover{.markdown = markdown, .span = {}}));
    e.h.step(3);
    std::vector<ftk::ui::core::Rect> card = cards(e.h);
    REQUIRE(card.size() == 1);
    CHECK(card.front().max.y <= 600.0f);

    // Its scrollbar's thumb down its right edge, in the raised colour.
    const auto thumb = [&](const ftk::ui::core::Rect& on) {
        std::vector<ftk::ui::core::Rect> found = drawn_in_code(e.h, ftk::theme::surface_raised());
        std::erase_if(found, [&](const ftk::ui::core::Rect& r) {
            return r.max.x - r.min.x > 16.0f || r.centre().x < on.max.x - 16.0f || !on.contains(r.centre());
        });
        REQUIRE(found.size() == 1);
        return found.front();
    };
    const float thumb_top = thumb(card.front()).min.y;

    // The wheel over it moves it on, and leaves the code where it was.
    const glm::vec2 scrolled = e.view.scroll;
    e.h.wheel(card.front().centre(), -3.0f);
    e.h.step(30);
    card = cards(e.h);
    REQUIRE(card.size() == 1);
    CHECK(e.view.scroll == scrolled);
    CHECK(thumb(card.front()).min.y > thumb_top);
    // Onto its foot it stays.
    e.h.move_to({card.front().centre().x, card.front().max.y - 4.0f});
    e.h.step(2);
    CHECK(cards(e.h).size() == 1);
}

TEST_CASE("With Vim on, K shows what is at the caret until the caret moves", "[code_view][vim][hover]") {
    FakeLanguage language;
    Editor e(FUNCTION);
    e.view.service = &language;
    e.view.path = "x.cpp";
    e.view.vim.emplace();
    e.h.click_at({PAST_END, row_y(1)});
    e.h.step();
    e.h.type("K");
    e.h.step();
    REQUIRE(language.hovers.size() == 1);
    CHECK(language.hovers.front().at.line == 1);
    FakeLanguage::answer(language.hovers.front(), std::optional<Hover>(Hover{.markdown = "`x`", .span = {}}));
    e.h.step(2);
    CHECK(cards(e.h).size() == 1);
    // The mouse going elsewhere leaves it; the caret moving takes it away.
    e.h.move_to({PAST_END, row_y(3)});
    e.h.step(2);
    CHECK(cards(e.h).size() == 1);
    e.h.type("j");
    e.h.step(2);
    CHECK(cards(e.h).empty());
}

TEST_CASE("Typing takes the hover card away, and resting on space asks nothing", "[code_view][hover]") {
    FakeLanguage language;
    Editor e(FUNCTION);
    e.view.service = &language;
    // The keyboard to the view, the caret on the last line.
    e.h.click_at({PAST_END, row_y(3)});
    e.h.step();
    const glm::vec2 x_at = x_called(e.h);
    e.h.move_to(x_at);
    wait(e, 0.6);
    REQUIRE(language.hovers.size() == 1);
    FakeLanguage::answer(language.hovers.front(), std::optional<Hover>(Hover{.markdown = "x", .span = {}}));
    e.h.step(2);
    REQUIRE(cards(e.h).size() == 1);
    e.h.type("a");
    e.h.step(2);
    REQUIRE(cards(e.h).empty());

    // Past the end of a line, and over the indent, there is nothing to ask.
    e.h.move_to({PAST_END, row_y(2)});
    wait(e, 0.6);
    e.h.move_to({x_at.x - 20.0f, x_at.y});
    wait(e, 0.6);
    REQUIRE(language.hovers.size() == 1);
}

namespace {

// The names the completion list shows, as the last frame drew them.
std::vector<std::string> listed(Editor& e) {
    std::vector<std::string> out;
    for (const auto& node : e.h.ui().semantics()) {
        if (node.role == ftk::ui::core::Role::TreeItem) out.emplace_back(node.label);
    }
    return out;
}

// A function entry named `name` that replaces `word` with itself.
Completion entry(std::string name, Span word, std::string sort) {
    Completion made;
    made.label = name;
    made.kind = CompletionKind::Function;
    made.change = {.span = word, .text = name};
    made.filter = name;
    made.sort = std::move(sort);
    return made;
}

// An editor on an empty function, the caret in its body, with `language`.
struct Completing {
    FakeLanguage language;
    Editor e{"void f() {\n    \n}", "coin.cpp"};

    Completing() {
        e.view.service = &language;
        e.view.path = "coin.cpp";
        e.h.click_at({PAST_END, row_y(1)});
        e.h.step();
    }

    // Answers the last question with `items`, marked `incomplete`.
    void answer(std::vector<Completion> items, bool incomplete = false) {
        REQUIRE_FALSE(language.completions.empty());
        const Question<Completions>& asked = language.completions.back();
        FakeLanguage::answer(asked, Completions{.items = std::move(items), .incomplete = incomplete, .at = asked.at,
                                                .version = e.document.version()});
        e.h.step(2);
    }
};

const Span TYPED_R{{1, 4}, {1, 5}};

} // namespace

TEST_CASE("Typing a word asks what completes it, and the list narrows as more is typed", "[code_view][completion]") {
    Completing c;
    c.e.h.type("r");
    c.e.h.step();
    REQUIRE(c.language.completions.size() == 1);
    REQUIRE(c.language.completions.front().at == Position{1, 5});
    c.answer({entry("rotate", TYPED_R, "1"), entry("roll", TYPED_R, "2"), entry("round", TYPED_R, "3")});
    REQUIRE(listed(c.e) == std::vector<std::string>{"rotate", "roll", "round"});

    c.e.h.type("ot");
    c.e.h.step(2);
    REQUIRE(listed(c.e) == std::vector<std::string>{"rotate"});
    // What was answered narrows on this side: nothing more is asked.
    REQUIRE(c.language.completions.size() == 1);
}

TEST_CASE("Down then Enter takes the entry below, and the caret goes after it", "[code_view][completion]") {
    Completing c;
    c.e.h.type("ro");
    c.e.h.step();
    c.answer({entry("rotate", TYPED_R, "1"), entry("roll", TYPED_R, "2")});
    c.e.h.press(Key::Down);
    c.e.h.press(Key::Enter);
    c.e.h.step();
    REQUIRE(c.e.document.buffer().line(1) == "    roll");
    REQUIRE(c.e.caret() == Position{1, 8});
    REQUIRE(listed(c.e).empty());
    // One step of the history takes it back.
    c.e.h.press(Key::Z, CTRL);
    c.e.h.step();
    REQUIRE(c.e.document.buffer().line(1) == "    ro");
}

TEST_CASE("An entry brings what it needs along, such as its #include", "[code_view][completion]") {
    Completing c;
    c.e.h.type("ro");
    c.e.h.step();
    Completion rotate = entry("rotate", TYPED_R, "1");
    rotate.more = {{.span = {{0, 0}, {0, 0}}, .text = "#include <glm/gtc/quaternion.hpp>\n"}};
    c.answer({rotate});
    c.e.h.press(Key::Tab);
    c.e.h.step();
    REQUIRE(c.e.document.buffer().line(0) == "#include <glm/gtc/quaternion.hpp>");
    REQUIRE(c.e.document.buffer().line(2) == "    rotate");
    REQUIRE(c.e.caret() == Position{2, 10});
}

TEST_CASE("Escape closes the list, and a click takes the entry clicked", "[code_view][completion]") {
    Completing c;
    c.e.h.type("ro");
    c.e.h.step();
    c.answer({entry("rotate", TYPED_R, "1"), entry("roll", TYPED_R, "2")});
    c.e.h.press(Key::Escape);
    c.e.h.step();
    REQUIRE(listed(c.e).empty());
    REQUIRE(c.e.document.buffer().line(1) == "    ro");

    // Ctrl Space asks again; the second entry clicked.
    c.e.h.press(Key::Space, CTRL);
    c.e.h.step();
    c.answer({entry("rotate", TYPED_R, "1"), entry("roll", TYPED_R, "2")});
    const auto* roll = c.e.h.find("roll", ftk::ui::core::Role::TreeItem);
    REQUIRE(roll != nullptr);
    c.e.h.click_at(roll->rect.centre());
    c.e.h.step(2);
    REQUIRE(c.e.document.buffer().line(1) == "    roll");
}

TEST_CASE("A list said to be incomplete asks again as more is typed, and a space closes it", "[code_view][completion]") {
    Completing c;
    c.e.h.type("r");
    c.e.h.step();
    c.answer({entry("rotate", TYPED_R, "1")}, true);
    c.e.h.type("o");
    c.e.h.step();
    REQUIRE(c.language.completions.size() == 2);
    REQUIRE(c.language.completions.back().at == Position{1, 6});
    c.answer({entry("rotate", {{1, 4}, {1, 6}}, "1")});
    REQUIRE(listed(c.e) == std::vector<std::string>{"rotate"});
    c.e.h.type(" ");
    c.e.h.step();
    REQUIRE(listed(c.e).empty());
}

TEST_CASE("A word begun asks, as the language has its words, and so does what the service names", "[completion]") {
    const Language& code = language_for("x.cpp");
    const Language& sheet = language_for("hud.fjss");
    const std::vector<std::string> clangd{".", "->"};
    const auto asks = [](const Language& language, std::span<const std::string> triggers, std::string_view line) {
        return asks_completion(language, triggers, line, line.size());
    };
    const auto character = [](std::string typed) {
        return CompletionTrigger{.kind = CompletionTrigger::Kind::Character, .character = std::move(typed)};
    };
    CHECK(asks(code, clangd, "    ro") == CompletionTrigger{});
    CHECK(asks(code, clangd, "    12") == std::nullopt);
    CHECK(asks(code, clangd, "    coin.") == character("."));
    CHECK(asks(code, clangd, "    coin->") == character("->"));
    // A number's point; a hyphen in C++; what no service names.
    CHECK(asks(code, clangd, "    1.") == std::nullopt);
    CHECK(asks(code, clangd, "    a-") == std::nullopt);
    CHECK(asks(code, {}, "    coin.") == std::nullopt);
    // A sheet's words run through hyphens; a negative number is still one.
    CHECK(asks(sheet, {}, "    justify-c") == CompletionTrigger{});
    CHECK(asks(sheet, {}, "    color: var(--") == CompletionTrigger{});
    CHECK(asks(sheet, {}, "    margin: -1") == std::nullopt);
    CHECK(asks(sheet, std::vector<std::string>{"@"}, "@") == character("@"));
    // A widget's run through hyphens and colons too; its quote and brace are
    // what fjls names.
    const Language& widget = language_for("hud.fjwidget");
    const std::vector<std::string> fjls{"<", "{", "\""};
    CHECK(asks(widget, fjls, "    <health-b") == CompletionTrigger{});
    CHECK(asks(widget, fjls, "    <div on:") == CompletionTrigger{});
    CHECK(asks(widget, fjls, "    <div class=\"") == character("\""));
    CHECK(asks(widget, fjls, "    <text>{") == character("{"));
}

TEST_CASE("What the service names asks anew, even with a list open", "[code_view][completion]") {
    Completing c;
    c.language.triggers = {".", "->"};
    c.e.h.type("ro");
    c.e.h.step();
    c.answer({entry("rotation", TYPED_R, "1")});
    REQUIRE(listed(c.e) == std::vector<std::string>{"rotation"});

    c.e.h.type(".");
    c.e.h.step();
    REQUIRE(c.language.completions.size() == 2);
    REQUIRE(c.language.completions.back().at == Position{1, 7});
    REQUIRE(c.language.completions_why.back() ==
            CompletionTrigger{.kind = CompletionTrigger::Kind::Character, .character = "."});
    const Span after_dot{{1, 7}, {1, 7}};
    c.answer({entry("x", after_dot, "1"), entry("y", after_dot, "2")});
    REQUIRE(listed(c.e) == std::vector<std::string>{"x", "y"});

    // A word narrows the open list; a hyphen closes it; -> asks again.
    c.e.h.type("x");
    c.e.h.step(2);
    REQUIRE(listed(c.e) == std::vector<std::string>{"x"});
    c.e.h.type("-");
    c.e.h.step();
    REQUIRE(listed(c.e).empty());
    c.e.h.type(">");
    c.e.h.step();
    REQUIRE(c.language.completions.size() == 3);
    REQUIRE(c.language.completions_why.back().character == "->");

    // A number's point asks nothing.
    c.e.h.type(" 1.");
    c.e.h.step();
    REQUIRE(c.language.completions.size() == 3);
}

TEST_CASE("In a sheet a word runs through its hyphens, the list open across them", "[code_view][completion]") {
    FakeLanguage language;
    Editor e{"#hud {\n    \n}", "hud.fjss"};
    e.view.service = &language;
    e.view.path = "hud.fjss";
    e.h.click_at({PAST_END, row_y(1)});
    e.h.step();
    e.h.type("f");
    e.h.step();
    REQUIRE(language.completions.size() == 1);
    const Span f{{1, 4}, {1, 5}};
    FakeLanguage::answer(language.completions.back(),
                         Completions{.items = {entry("flex-direction", f, "1"), entry("flex-grow", f, "2"),
                                               entry("flex-wrap", f, "3"), entry("font-size", f, "4")},
                                     .incomplete = false,
                                     .at = language.completions.back().at,
                                     .version = e.document.version()});
    e.h.step(2);

    e.h.type("lex-w");
    e.h.step(2);
    // What begins with "flex-w" first; nothing more asked.
    REQUIRE(listed(e) == std::vector<std::string>{"flex-wrap", "flex-grow"});
    REQUIRE(language.completions.size() == 1);
    e.h.press(Key::Enter);
    e.h.step();
    REQUIRE(e.document.buffer().line(1) == "    flex-wrap");
    REQUIRE(e.caret() == Position{1, 13});
}

namespace {

// An editor on FUNCTION with `language`, the keyboard in it, the caret at
// the end of "    x();".
struct Asking {
    FakeLanguage language;
    Editor e{FUNCTION};

    Asking() {
        e.view.service = &language;
        e.view.path = "x.cpp";
        e.h.click_at({PAST_END, row_y(1)});
        e.h.step();
    }
};

} // namespace

TEST_CASE("F12 asks where the name at the caret is defined, and selects it there in the same file", "[code_view][definition]") {
    Asking a;
    a.e.h.press(Key::F12);
    a.e.h.step();
    REQUIRE(a.language.definitions.size() == 1);
    REQUIRE(a.language.definitions.front().at == Position{1, 8});
    FakeLanguage::answer(a.language.definitions.front(), std::vector<Location>{{"x.cpp", {{3, 4}, {3, 5}}}});
    a.e.h.step(2);
    REQUIRE(a.e.view.selections.primary().span() == Span{{3, 4}, {3, 5}});
    REQUIRE_FALSE(a.e.went.has_value());
}

TEST_CASE("With Vim on, a definition gone to in the same file is a jump from where the caret was",
          "[code_view][definition][vim]") {
    Asking a;
    a.e.view.vim.emplace();
    a.e.h.step();
    const Position from = a.e.caret();
    a.e.h.type("gd");
    a.e.h.step();
    REQUIRE(a.language.definitions.size() == 1);
    FakeLanguage::answer(a.language.definitions.front(), std::vector<Location>{{"x.cpp", {{3, 4}, {3, 5}}}});
    a.e.h.step(2);
    CHECK(a.e.caret() == Position{3, 4});
    REQUIRE(a.e.jumped.size() == 1);
    CHECK(a.e.jumped.front().at == from);
    // `` goes back to it.
    a.e.h.type("``");
    a.e.h.step();
    CHECK(a.e.caret() == from);
}

TEST_CASE("A definition in another file is handed out for the program to open", "[code_view][definition]") {
    Asking a;
    a.e.h.press(Key::F12);
    a.e.h.step();
    const Location elsewhere{"coin.hpp", {{10, 2}, {10, 6}}};
    FakeLanguage::answer(a.language.definitions.front(), std::vector<Location>{elsewhere});
    a.e.h.step();
    REQUIRE(a.e.went == elsewhere);
    REQUIRE(a.e.caret() == Position{1, 8});
}

TEST_CASE("Ctrl and a click asks where the word clicked is defined, the caret left where it is", "[code_view][definition]") {
    Asking a;
    const glm::vec2 x_at = x_called(a.e.h);
    a.e.h.hold(CTRL);
    a.e.h.click_at(x_at);
    a.e.h.hold({});
    a.e.h.step();
    REQUIRE(a.language.definitions.size() == 1);
    REQUIRE(a.language.definitions.front().at.line == 1);
    REQUIRE(a.e.caret() == Position{1, 8});
}

TEST_CASE("Format makes the service's edit as one step, while the text is as it was asked about", "[code_view][format]") {
    Asking a;
    a.e.h.press(Key::I, {.ctrl = true, .shift = true});
    a.e.h.step();
    REQUIRE(a.language.formats.size() == 1);
    const Edit indent{.changes = {{.span = {{1, 0}, {1, 4}}, .text = "  "}}};
    FakeLanguage::answer(a.language.formats.front(),
                         std::optional<VersionedEdit>(VersionedEdit{.version = a.e.document.version(), .edit = indent}));
    a.e.h.step(2);
    REQUIRE(a.e.document.buffer().line(1) == "  x();");
    a.e.h.press(Key::Z, CTRL);
    a.e.h.step();
    REQUIRE(a.e.document.buffer().line(1) == "    x();");

    // An answer for a text since changed is let go.
    a.e.h.press(Key::I, {.ctrl = true, .shift = true});
    a.e.h.step();
    const uint64_t asked = a.e.document.version();
    a.e.h.type("y");
    a.e.h.step();
    FakeLanguage::answer(a.language.formats.back(), std::optional<VersionedEdit>(VersionedEdit{.version = asked, .edit = indent}));
    a.e.h.step(2);
    REQUIRE(a.e.document.buffer().line(1) == "    x();y");
}

namespace {

// A name used twice, the caret in the first, a service to ask.
struct Renaming {
    FakeLanguage language;
    Editor e{"int speed;\nint f() { return speed; }"};
    const std::vector<Span> uses{{{0, 4}, {0, 9}}, {{1, 17}, {1, 22}}};

    Renaming() {
        e.view.service = &language;
        e.view.path = "x.cpp";
        e.h.click_at({PAST_END, row_y(0)});
        e.h.step();
    }

    // F2 with the caret in the name, the service saying where it stands.
    void begin() {
        e.h.press(Key::Left);
        e.h.press(Key::Left);
        e.h.press(Key::F2);
        e.h.step();
        REQUIRE_FALSE(language.highlights.empty());
        FakeLanguage::answer(language.highlights.back(), uses);
        e.h.step();
    }
};

} // namespace

TEST_CASE("F2 renames in place: every use the service names is typed over at once", "[code_view][rename]") {
    Renaming r;
    r.begin();
    CHECK(r.e.view.selections.size() == 2);
    r.e.h.type("pace");
    r.e.h.step();
    CHECK(r.e.text() == "int pace;\nint f() { return pace; }");
    // Enter takes the typing back and hands the rename out.
    r.e.h.press(Key::Enter);
    r.e.h.step();
    CHECK(r.e.text() == "int speed;\nint f() { return speed; }");
    REQUIRE(r.e.renamed.has_value());
    CHECK(*r.e.renamed == Rename{.at = {0, 4}, .from = "speed", .to = "pace"});
    // Nothing of the typing is left to undo or redo.
    r.e.h.press(Key::Y, CTRL);
    r.e.h.step();
    CHECK(r.e.text() == "int speed;\nint f() { return speed; }");
}

TEST_CASE("Esc gives a rename in place up, and so does a caret going off the name", "[code_view][rename]") {
    Renaming r;
    r.begin();
    r.e.h.type("pace");
    r.e.h.press(Key::Escape);
    r.e.h.step();
    CHECK(r.e.text() == "int speed;\nint f() { return speed; }");
    CHECK_FALSE(r.e.renamed.has_value());
    CHECK(r.e.view.selections.size() == 1);
    // A click on the second line: given up, the caret where it was put.
    r.begin();
    r.e.h.type("x");
    r.e.h.click_at({PAST_END, row_y(1)});
    r.e.h.step();
    CHECK(r.e.text() == "int speed;\nint f() { return speed; }");
    CHECK_FALSE(r.e.renamed.has_value());
    CHECK(r.e.caret().line == 1);
}

TEST_CASE("With Vim on, grn renames in place in insert mode, leaving nothing for the dot", "[code_view][vim][rename]") {
    Renaming r;
    r.e.view.vim.emplace();
    r.e.h.step();
    r.e.h.type("h");
    r.e.h.type("grn");
    r.e.h.step();
    REQUIRE(r.language.highlights.size() == 1);
    FakeLanguage::answer(r.language.highlights.front(), r.uses);
    r.e.h.step();
    CHECK(r.e.view.vim->mode() == vim::Mode::Insert);
    r.e.h.type("pace");
    r.e.h.press(Key::Enter);
    r.e.h.step();
    CHECK(r.e.view.vim->mode() == vim::Mode::Normal);
    REQUIRE(r.e.renamed.has_value());
    CHECK(r.e.renamed->to == "pace");
    r.e.h.type(".");
    r.e.h.step();
    CHECK(r.e.text() == "int speed;\nint f() { return speed; }");
}

namespace {

// An editor with Vim on, the caret on the first line.
struct WithVim {
    Editor e;

    explicit WithVim(std::string_view text) : e(text) {
        e.view.vim.emplace();
        e.h.click_at({PAST_END, row_y(0)});
        e.h.step();
    }
};

// The carets the last frame drew: shapes in the text colour nearly a row
// tall, which glyphs never are.
std::vector<ftk::ui::core::Rect> carets_drawn(const ftk::test::UiHarness& h) {
    std::vector<ftk::ui::core::Rect> out = drawn_in_code(h, ftk::theme::text());
    std::erase_if(out, [](const ftk::ui::core::Rect& r) { return r.max.y - r.min.y < CODE_LINE - 3.0f; });
    return out;
}

} // namespace

TEST_CASE("With Vim on, the keys are Vim's, and a caret past a line's end goes onto its last character", "[code_view][vim]") {
    WithVim v("foo bar");
    REQUIRE(v.e.caret() == Position{0, 6});
    v.e.h.type("0w");
    REQUIRE(v.e.caret() == Position{0, 4});
    v.e.h.type("iZ");
    v.e.h.press(Key::Escape);
    REQUIRE(v.e.text() == "foo Zbar");
    REQUIRE(v.e.caret() == Position{0, 4});
}

TEST_CASE("With Vim on, a key is read through the text it types, a chord as itself", "[code_view][vim]") {
    using ftk::ui::core::KeyEvent;
    WithVim v("a\n\nb");
    v.e.h.type("0");
    // } as a German keyboard types it, AltGr 0 (Ctrl and Alt, as Windows
    // reports AltGr): the text stands for the key.
    v.e.h.send(KeyEvent{.kind = KeyEvent::Kind::Down, .key = Key::Digit0, .modifiers = {.ctrl = true, .alt = true}});
    v.e.h.ui().add_text("}");
    v.e.h.send(KeyEvent{.kind = KeyEvent::Kind::Up, .key = Key::Digit0, .modifiers = {.ctrl = true, .alt = true}});
    v.e.h.step();
    REQUIRE(v.e.caret() == Position{1, 0});
    // Ctrl R types nothing: it reaches Vim as itself, and redoes.
    v.e.h.type("ix");
    v.e.h.press(Key::Escape);
    v.e.h.type("u");
    REQUIRE(v.e.text() == "a\n\nb");
    v.e.h.press(Key::R, CTRL);
    REQUIRE(v.e.text() == "a\nx\nb");
}

TEST_CASE("With Vim on, the keymap's bindings for its mode come first", "[code_view][vim]") {
    WithVim v("a\nb\nc");
    v.e.keys.bind(Keymap::VIM_NORMAL, {Chord::typed("g"), Chord::typed("x")}, "edit.delete_lines");
    v.e.h.type("j");
    v.e.h.type("gx");
    REQUIRE(v.e.text() == "a\nc");
    // A binding begun and gone no further gives its keys to Vim.
    v.e.h.type("gg");
    REQUIRE(v.e.caret() == Position{0, 0});
}

TEST_CASE("With Vim on, the character an f waits for is never a binding's", "[code_view][vim]") {
    WithVim v("axb");
    v.e.keys.bind(Keymap::VIM_NORMAL, {Chord::typed("x")}, "edit.delete_lines");
    v.e.h.type("0fx");
    REQUIRE(v.e.text() == "axb");
    REQUIRE(v.e.caret() == Position{0, 1});
    v.e.h.type("x");
    REQUIRE(v.e.text().empty());
}

namespace {

// keys.vim's maps for `v`'s view.
void use_maps(WithVim& v, std::string_view keys_vim) {
    const vim::KeysFile file = vim::read_keys_file(keys_vim);
    REQUIRE(file.errors.empty());
    v.e.view.maps.use(std::make_shared<const std::vector<vim::Mapping>>(file.maps));
}

} // namespace

TEST_CASE("With Vim on, a macro keeps the keys typed, before the maps, and plays them through the maps",
          "[code_view][vim][macros]") {
    WithVim v("one\ntwo\nthree\nfour");
    use_maps(v, "nnoremap Q A;<Esc>j\n");
    v.e.h.type("0qaQq");
    v.e.h.step();
    CHECK(v.e.view.vim->registers().get('a', nullptr)->text() == "Q");
    CHECK(v.e.text() == "one;\ntwo\nthree\nfour");
    // Q is the map's again each time, until j can't go at the last line.
    v.e.h.type("9@a");
    v.e.h.step();
    CHECK(v.e.text() == "one;\ntwo;\nthree;\nfour;");
    CHECK_FALSE(v.e.view.maps.more());
}

TEST_CASE("With Vim on, keys go through keys.vim's maps before Vim, <Cmd> lines handed up", "[code_view][vim][maps]") {
    WithVim v("one two");
    use_maps(v, "inoremap jk <Esc>\nnnoremap <leader>w <Cmd>w<CR>\nnnoremap Q 0dw\n");
    v.e.h.type("ijk");
    v.e.h.step();
    CHECK(v.e.view.vim->mode() == vim::Mode::Normal);
    CHECK(v.e.text() == "one two");
    v.e.h.type("Q");
    v.e.h.step();
    CHECK(v.e.text() == "two");
    v.e.h.type(" w");
    v.e.h.step();
    REQUIRE(v.e.handed.size() == 1);
    CHECK(v.e.handed.front().name == "write");
}

TEST_CASE("With Vim on, keys that begin a longer map or binding wait timeoutlen, then go as they stand",
          "[code_view][vim][maps]") {
    WithVim v("abc");
    use_maps(v, "nnoremap <leader>fg <Cmd>w<CR>\ninoremap jk <Esc>\n");
    // A wait shorter than the half second before the keys that can come
    // next show: Space waits for the map, then is Vim's, a step right.
    v.e.view.vim->settings().timeout_ms = 200;
    v.e.h.type("0 ");
    v.e.h.step();
    CHECK(v.e.caret() == Position{0, 0});
    CHECK(v.e.view.maps.waiting() == *vim::parse_notation("<Space>"));
    v.e.h.step(20);
    CHECK(v.e.caret() == Position{0, 1});
    // A binding of the keymap's waits the same way.
    v.e.keys.bind(Keymap::VIM_NORMAL, *vim::parse_notation("<Space>x"), "edit.select_all");
    v.e.h.type(" ");
    v.e.h.step(3);
    CHECK(v.e.caret() == Position{0, 1});
    v.e.h.step(40);
    CHECK(v.e.caret() == Position{0, 2});
    // In insert mode it is timeoutlen however long: a lone j of jk is typed.
    v.e.view.vim->settings().timeout_ms = 1000;
    v.e.h.type("ij");
    v.e.h.step(3);
    CHECK(v.e.text() == "abc");
    CHECK_FALSE(v.e.view.showing_next_keys);
    v.e.h.step(70);
    CHECK(v.e.text() == "abjc");
}

TEST_CASE("With Vim on, keys that waited half a second show what can come next and wait for it; Esc lets them go",
          "[code_view][vim][maps]") {
    WithVim v("abc");
    use_maps(v, "nnoremap <leader>fg <Cmd>w<CR>\nnnoremap <leader>w <Cmd>w<CR>\n");
    v.e.h.type("0 ");
    v.e.h.step(20);
    CHECK_FALSE(v.e.view.showing_next_keys);
    v.e.h.step(20);
    CHECK(v.e.view.showing_next_keys);
    // Drawn at the code's bottom right, its keys on raised caps.
    const auto caps = drawn(v.e.h, ftk::theme::surface_raised());
    CHECK(std::ranges::any_of(caps, [](const ftk::ui::core::Rect& cap) { return cap.min.x > 400.0f && cap.min.y > 400.0f; }));
    // Held past timeoutlen.
    v.e.h.step(70);
    CHECK(v.e.caret() == Position{0, 0});
    CHECK(v.e.view.maps.waiting() == *vim::parse_notation("<Space>"));
    // The next key goes on as it would have: w runs its map.
    v.e.h.type("w");
    v.e.h.step();
    CHECK(v.e.handed.size() == 1);
    CHECK_FALSE(v.e.view.showing_next_keys);
    // Esc lets the keys waiting go, and goes no further itself.
    v.e.h.type(" ");
    v.e.h.step(40);
    REQUIRE(v.e.view.showing_next_keys);
    v.e.h.press(Key::Escape);
    v.e.h.step();
    CHECK(v.e.view.maps.waiting().empty());
    CHECK_FALSE(v.e.view.showing_next_keys);
    CHECK(v.e.caret() == Position{0, 0});
    // A binding of the keymap's shows the same.
    v.e.keys.bind(Keymap::VIM_NORMAL, *vim::parse_notation("gx"), "edit.delete_lines");
    v.e.h.type("g");
    v.e.h.step(40);
    CHECK(v.e.view.showing_next_keys);
    v.e.h.type("x");
    v.e.h.step();
    CHECK_FALSE(v.e.view.showing_next_keys);
    CHECK(v.e.text().empty());
}

TEST_CASE("With Vim on, the caret is a block in normal mode, a bar in insert mode", "[code_view][vim]") {
    WithVim v("abc");
    const std::vector<ftk::ui::core::Rect> block = carets_drawn(v.e.h);
    REQUIRE(block.size() == 1);
    CHECK(block.front().max.x - block.front().min.x > 4.0f);
    // The block covers the last character, the caret's.
    v.e.h.type("ix");
    const std::vector<ftk::ui::core::Rect> bar = carets_drawn(v.e.h);
    REQUIRE(bar.size() == 1);
    CHECK(bar.front().max.x - bar.front().min.x < 3.0f);
}

TEST_CASE("With Vim on, a drag selects in visual mode", "[code_view][vim]") {
    WithVim v("one\ntwo\nthree");
    // From a row below the setup's click, which a press there would make a
    // double click of.
    v.e.h.drag({PAST_END, row_y(1)}, {PAST_END, row_y(2)});
    REQUIRE(v.e.view.vim->mode() == vim::Mode::Visual);
    REQUIRE(v.e.view.selections.primary() == Selection{.anchor = {1, 3}, .caret = {2, 5}});
    v.e.h.type("d");
    REQUIRE(v.e.text() == "one\ntwo");
    REQUIRE(v.e.view.vim->mode() == vim::Mode::Normal);
}

TEST_CASE("With Vim on, Ctrl Shift C and V copy and paste as Vim's keys through the + register", "[code_view][vim]") {
    constexpr Modifiers CTRL_SHIFT{.ctrl = true, .shift = true};
    WithVim v("one\ntwo");
    // Visual mode's selection, as "+y copies it, visual mode ending.
    v.e.h.type("0vl");
    v.e.h.press(Key::C, CTRL_SHIFT);
    CHECK(v.e.h.ui().clipboard() == "on");
    CHECK(v.e.view.vim->mode() == vim::Mode::Normal);
    // In normal mode the line, put back above as a line, as "+yy and "+P.
    v.e.h.type("j");
    v.e.h.press(Key::C, CTRL_SHIFT);
    CHECK(v.e.h.ui().clipboard() == "two\n");
    v.e.h.type("l");
    v.e.h.press(Key::V, CTRL_SHIFT);
    CHECK(v.e.text() == "one\ntwo\ntwo");
    // In insert mode what the clipboard holds is typed, as Ctrl-R + types it.
    v.e.h.ui().set_clipboard("x");
    v.e.h.type("i");
    v.e.h.press(Key::V, CTRL_SHIFT);
    v.e.h.press(Key::Escape);
    CHECK(v.e.text() == "one\nxtwo\ntwo");
}

TEST_CASE("With Vim on, the Edit menu's Paste puts as \"+P does, dropping keys Vim waited on", "[code_view][vim]") {
    WithVim v("one\ntwo");
    v.e.h.ui().set_clipboard("new\n");
    // The caret on the e of one.
    v.e.h.type("d");
    v.e.view.queued.emplace_back("edit.paste");
    v.e.h.step();
    // A line above, not the text at the caret.
    CHECK(v.e.text() == "new\none\ntwo");
    CHECK(v.e.view.vim->mode() == vim::Mode::Normal);
    // The d no longer waits for a motion.
    v.e.h.type("w");
    CHECK(v.e.text() == "new\none\ntwo");
    v.e.view.queued.emplace_back("edit.undo");
    v.e.h.step();
    CHECK(v.e.text() == "one\ntwo");
}

TEST_CASE("With Vim on, Ctrl-J and Ctrl-K step the completion list, as Ctrl-N and Ctrl-P do",
          "[code_view][vim][completion]") {
    Completing c;
    c.e.view.vim.emplace();
    c.e.h.type("ar");
    c.e.h.step();
    c.answer({entry("rotate", TYPED_R, "1"), entry("roll", TYPED_R, "2")});
    REQUIRE(listed(c.e) == std::vector<std::string>{"rotate", "roll"});
    const std::string text = c.e.text();
    REQUIRE(c.e.view.completing.selected == std::size_t{0});
    c.e.h.press(Key::J, CTRL);
    c.e.h.step();
    CHECK(c.e.view.completing.selected == std::size_t{1});
    c.e.h.press(Key::K, CTRL);
    c.e.h.step();
    CHECK(c.e.view.completing.selected == std::size_t{0});
    CHECK(c.e.text() == text);
}

TEST_CASE("With Vim on, leaving insert mode takes the completion list with it", "[code_view][vim][completion]") {
    Completing c;
    c.e.view.vim.emplace();
    c.e.h.type("ar");
    c.e.h.step();
    c.answer({entry("rotate", TYPED_R, "1"), entry("roll", TYPED_R, "2")});
    REQUIRE(listed(c.e) == std::vector<std::string>{"rotate", "roll"});
    c.e.h.press(Key::Escape);
    c.e.h.step();
    REQUIRE(listed(c.e).empty());
    REQUIRE(c.e.view.vim->mode() == vim::Mode::Normal);
}

TEST_CASE("With Vim on, / shows its line on a plate and its matches through the find", "[code_view][vim]") {
    WithVim v("one foo two foo");
    v.e.h.type("0/fo");
    v.e.h.step();
    const auto* field = v.e.h.find("Command line", ftk::ui::core::Role::TextField);
    REQUIRE(field != nullptr);
    CHECK(field->value == "fo");
    CHECK(v.e.view.find.shown);
    CHECK(v.e.view.find.matches.size() == 2);
    // The line reads at the code's size.
    const float small = field->rect.height();
    v.e.view.text_size = 21.0f;
    v.e.h.step();
    CHECK(v.e.h.find("Command line", ftk::ui::core::Role::TextField)->rect.height() >= small + 8.0f);
    v.e.view.text_size = CODE_TEXT;
    v.e.h.step();
    v.e.h.press(Key::Enter);
    v.e.h.step();
    CHECK(v.e.h.find("Command line", ftk::ui::core::Role::TextField) == nullptr);
    CHECK(v.e.caret() == Position{0, 4});
    CHECK(v.e.view.find.shown);
    v.e.h.press(Key::L, CTRL);
    v.e.h.step();
    CHECK_FALSE(v.e.view.find.shown);
}

TEST_CASE("With Vim on, the : line lists the commands a name typed can become; Tab or a click puts one in",
          "[code_view][vim]") {
    WithVim v("spin");
    const auto field = [&] { return v.e.h.find("Command line", ftk::ui::core::Role::TextField); };
    v.e.h.type(":w");
    v.e.h.step();
    REQUIRE(v.e.h.find("write", ftk::ui::core::Role::TreeItem) != nullptr);
    REQUIRE(v.e.h.find("wq", ftk::ui::core::Role::TreeItem) != nullptr);
    // Under the field.
    CHECK(v.e.h.find("wall", ftk::ui::core::Role::TreeItem)->rect.min.y >= field()->rect.max.y);
    v.e.h.press(Key::Tab);
    v.e.h.step();
    CHECK(field()->value == "wall");
    // A click puts its name in, and the keyboard stays on the line.
    v.e.h.click_at(v.e.h.find("wq", ftk::ui::core::Role::TreeItem)->rect.centre());
    v.e.h.step();
    CHECK(field()->value == "wq");
    v.e.h.type("!");
    v.e.h.step();
    CHECK(field()->value == "wq!");
    CHECK(v.e.h.find("wq", ftk::ui::core::Role::TreeItem) == nullptr);
}

TEST_CASE("With Vim on, the : line lists the paths after :e; a click on a folder goes into it", "[code_view][vim]") {
    WithVim v("spin");
    v.e.view.vim->list_paths_with([](std::string_view folder, bool) {
        if (folder == "src/") return std::vector<vim::PathEntry>{{.name = "main.cpp", .folder = false}};
        return std::vector<vim::PathEntry>{{.name = "src", .folder = true}, {.name = "spin.cpp", .folder = false}};
    });
    const auto field = [&] { return v.e.h.find("Command line", ftk::ui::core::Role::TextField); };
    v.e.h.type(":e s");
    v.e.h.step();
    REQUIRE(v.e.h.find("spin.cpp", ftk::ui::core::Role::TreeItem) != nullptr);
    const auto* folder = v.e.h.find("src/", ftk::ui::core::Role::TreeItem);
    REQUIRE(folder != nullptr);
    v.e.h.click_at(folder->rect.centre());
    v.e.h.step();
    CHECK(field()->value == "e src/");
    v.e.h.step();
    CHECK(v.e.h.find("main.cpp", ftk::ui::core::Role::TreeItem) != nullptr);
    CHECK(v.e.h.find("spin.cpp", ftk::ui::core::Role::TreeItem) == nullptr);
}

TEST_CASE("With Vim on, the : line's lit row keeps inside the plate", "[code_view][vim]") {
    WithVim v("spin");
    v.e.h.type(":w");
    v.e.h.step();
    const auto* field = v.e.h.find("Command line", ftk::ui::core::Role::TextField);
    REQUIRE(field != nullptr);
    // The selection's wash under the field, as wide as the field at most.
    const uint32_t wash = ftk::ui::core::pack_colour(ftk::theme::selection());
    bool washed = false;
    for (const auto& root : v.e.h.output().roots) {
        for (const auto& p : root.draw->primitives) {
            if (p.colour != wash || p.min.y < field->rect.max.y) continue;
            washed = true;
            CHECK(p.max.x <= field->rect.max.x + 1.0f);
        }
    }
    CHECK(washed);
}

TEST_CASE("With Vim on, the : line's rows grow with the code's text, as its field does", "[code_view][vim]") {
    WithVim v("spin");
    v.e.h.type(":w");
    v.e.h.step();
    const float small = v.e.h.find("write", ftk::ui::core::Role::TreeItem)->rect.height();
    v.e.view.text_size = 21.0f;
    v.e.h.step();
    CHECK(v.e.h.find("write", ftk::ui::core::Role::TreeItem)->rect.height() >= small + 8.0f);
}

TEST_CASE("With Vim on, the : line's plate says under its field what Enter will do", "[code_view][vim]") {
    WithVim v("spin");
    v.e.view.file_facts = {.file = "windmill.cpp", .closing_loses = true, .unsaved = "windmill.cpp"};
    // Whether the last frame drew in `colour` under the field, in the
    // plate's width.
    const auto under_field = [&](ftk::ui::Color colour) {
        const auto* field = v.e.h.find("Command line", ftk::ui::core::Role::TextField);
        REQUIRE(field != nullptr);
        const uint32_t packed = ftk::ui::core::pack_colour(colour);
        for (const auto& root : v.e.h.output().roots) {
            for (const auto& p : root.draw->primitives) {
                if (p.colour == packed && p.min.y >= field->rect.max.y && p.max.y <= field->rect.max.y + 8.0f * CODE_LINE &&
                    p.min.x >= field->rect.min.x - 1.0f && p.max.x <= field->rect.max.x + 1.0f) {
                    return true;
                }
            }
        }
        return false;
    };
    v.e.h.type(":w");
    v.e.h.step();
    CHECK(under_field(ftk::theme::text_secondary()));
    const float wide = v.e.h.find("Command line", ftk::ui::core::Role::TextField)->rect.width();
    // A refusal to come, in the warning colour.
    v.e.h.press(Key::Backspace);
    v.e.h.type("q");
    v.e.h.step();
    CHECK(under_field(ftk::theme::warning()));
    // A line that won't run, in the error colour.
    v.e.h.press(Key::Backspace);
    v.e.h.type("frob");
    v.e.h.step();
    CHECK(under_field(ftk::theme::error()));
    // A search says nothing under its field, on go to line's narrower plate.
    v.e.h.press(Key::Escape);
    v.e.h.type("/s");
    v.e.h.step();
    CHECK_FALSE(under_field(ftk::theme::text_secondary()));
    CHECK(v.e.h.find("Command line", ftk::ui::core::Role::TextField)->rect.width() < wide);
}

TEST_CASE("With Vim on, Ctrl-D scrolls the view with the caret, and zt puts the caret's line at the top", "[code_view][vim]") {
    std::string text;
    for (int i = 0; i < 100; ++i) text += "line\n";
    WithVim v(text);
    v.e.h.type("gg");
    v.e.h.press(Key::D, CTRL);
    v.e.h.step();
    const float rows = v.e.view.scroll.y / CODE_LINE;
    REQUIRE(rows > 0.0f);
    REQUIRE(static_cast<float>(v.e.caret().line) == rows);
    v.e.h.type("50G");
    v.e.h.step();
    v.e.h.type("zt");
    v.e.h.step();
    REQUIRE(v.e.view.scroll.y == 49.0f * CODE_LINE);
    REQUIRE(v.e.caret().line == 49);
}

TEST_CASE("With Vim on, a dot makes an insert again with the completion taken in it", "[code_view][vim][completion]") {
    Completing c;
    c.e.view.vim.emplace();
    c.e.h.type("ar");
    c.e.h.step();
    c.answer({entry("rotate", TYPED_R, "1"), entry("roll", TYPED_R, "2")});
    c.e.h.press(Key::Enter);
    c.e.h.step();
    c.e.h.press(Key::Escape);
    c.e.h.step();
    REQUIRE(c.e.document.buffer().line(1) == "    rotate");
    c.e.h.type(".");
    c.e.h.step();
    REQUIRE(c.e.document.buffer().line(1) == "    rotaterotate");
}

