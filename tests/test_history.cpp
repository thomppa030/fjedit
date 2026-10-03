#include "fjedit/core/history.hpp"

#include "print.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace fjedit;

namespace {

// A buffer with one caret, edited the way a typing user edits it.
struct Doc {
    Buffer buffer;
    Selections selections;
    History history;

    explicit Doc(std::string_view text) : buffer(text) {}

    void edit(Change change, EditKind kind) {
        const Selections before = selections;
        Edit made{{std::move(change)}};
        Edit undo = buffer.apply(made);
        selections = Selections(Selection::at(spans_after(made).back().to));
        history.record({.made = std::move(made), .undo = std::move(undo), .before = before,
                        .after = selections, .kind = kind});
    }

    void type(std::string_view text) {
        for (const char c : text) {
            const Position at = selections.primary().caret;
            edit({.span = {at, at}, .text = std::string(1, c)}, EditKind::Typing);
        }
    }

    void backspace() {
        const Position at = selections.primary().caret;
        edit({.span = {{at.line, at.byte - 1}, at}, .text = ""}, EditKind::Deleting);
    }

    void undo() {
        if (auto undone = history.undo()) {
            for (const Edit& edit : undone->edits) (void)buffer.apply(edit);
            selections = undone->selections;
        }
    }

    void redo() {
        if (auto redone = history.redo()) {
            for (const Edit& edit : redone->edits) (void)buffer.apply(edit);
            selections = redone->selections;
        }
    }
};

} // namespace

TEST_CASE("A word typed letter by letter is one step", "[history]") {
    Doc doc("x");
    doc.selections = Selections(Selection::at({0, 1}));
    doc.type(" = 42");
    REQUIRE(doc.buffer.text() == "x = 42");

    doc.undo();
    REQUIRE(doc.buffer.text() == "x");
    REQUIRE(doc.selections.primary() == Selection::at({0, 1}));
    REQUIRE_FALSE(doc.history.can_undo());

    doc.redo();
    REQUIRE(doc.buffer.text() == "x = 42");
    REQUIRE(doc.selections.primary() == Selection::at({0, 6}));
}

TEST_CASE("Closing the step splits typing in two", "[history]") {
    Doc doc("");
    doc.type("one");
    doc.history.close();
    doc.type(" two");

    doc.undo();
    REQUIRE(doc.buffer.text() == "one");
    doc.undo();
    REQUIRE(doc.buffer.text().empty());
}

TEST_CASE("Typing then deleting are two steps", "[history]") {
    Doc doc("");
    doc.type("abc");
    doc.backspace();
    doc.backspace();
    REQUIRE(doc.buffer.text() == "a");

    doc.undo();
    REQUIRE(doc.buffer.text() == "abc");
    doc.undo();
    REQUIRE(doc.buffer.text().empty());
}

TEST_CASE("Other edits are steps of their own", "[history]") {
    Doc doc("");
    doc.edit({.span = {{0, 0}, {0, 0}}, .text = "pasted"}, EditKind::Other);
    doc.edit({.span = {{0, 6}, {0, 6}}, .text = " again"}, EditKind::Other);
    doc.type("!");

    doc.undo();
    REQUIRE(doc.buffer.text() == "pasted again");
    doc.undo();
    REQUIRE(doc.buffer.text() == "pasted");
}

TEST_CASE("An edit after an undo drops what could be redone", "[history]") {
    Doc doc("");
    doc.type("first");
    doc.history.close();
    doc.type(" second");
    doc.undo();
    doc.type(" other");
    REQUIRE_FALSE(doc.history.can_redo());

    doc.undo();
    REQUIRE(doc.buffer.text() == "first");
}

TEST_CASE("Undo and redo with nothing to do change nothing", "[history]") {
    Doc doc("text");
    REQUIRE_FALSE(doc.history.undo().has_value());
    REQUIRE_FALSE(doc.history.redo().has_value());
    REQUIRE(doc.buffer.text() == "text");
}

TEST_CASE("Undoing back to the saved text makes it unchanged again", "[history]") {
    Doc doc("saved");
    REQUIRE(doc.history.at_saved());

    doc.selections = Selections(Selection::at({0, 5}));
    doc.type("!");
    REQUIRE_FALSE(doc.history.at_saved());
    doc.undo();
    REQUIRE(doc.history.at_saved());
    doc.redo();
    REQUIRE_FALSE(doc.history.at_saved());

    doc.history.mark_saved();
    REQUIRE(doc.history.at_saved());
    doc.type("?");
    REQUIRE_FALSE(doc.history.at_saved());
    doc.undo();
    REQUIRE(doc.history.at_saved());
}

TEST_CASE("Saving ends the open step", "[history]") {
    Doc doc("");
    doc.type("ab");
    doc.history.mark_saved();
    doc.type("cd");
    doc.undo();
    REQUIRE(doc.buffer.text() == "ab");
    REQUIRE(doc.history.at_saved());
}

TEST_CASE("A saved text undone past and edited apart can't be reached again", "[history]") {
    Doc doc("");
    doc.type("kept");
    doc.history.mark_saved();
    doc.undo();
    doc.type("other");
    REQUIRE_FALSE(doc.history.at_saved());
    doc.undo();
    REQUIRE_FALSE(doc.history.at_saved());
}
