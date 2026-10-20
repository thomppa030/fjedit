#include "fjedit/core/vim/registers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

using namespace fjedit;
using vim::Registers;
using vim::Yanked;

namespace {

// A clipboard of the test's own.
struct TestClipboard {
    std::string text;
    Clipboard clipboard{.get = [this] { return text; }, .set = [this](std::string set) { text = std::move(set); }};

    TestClipboard() = default;
    TestClipboard(const TestClipboard&) = delete;
    TestClipboard& operator=(const TestClipboard&) = delete;
};

const Yanked SPIN{.pieces = {"spin"}, .linewise = false};
const Yanked LINES{.pieces = {"a\nb"}, .linewise = true};

} // namespace

// Each expectation is where Vim keeps the same yank or delete.

TEST_CASE("A yank goes to the unnamed register and to 0", "[vim][registers]") {
    Registers registers;
    registers.yank('"', SPIN, nullptr);
    CHECK(registers.get('"', nullptr) == SPIN);
    CHECK(registers.get('0', nullptr) == SPIN);
    CHECK_FALSE(registers.get('1', nullptr));
}

TEST_CASE("A delete of lines goes to 1, the older ones moving up; a little goes to -", "[vim][registers]") {
    Registers registers;
    registers.deleted('"', LINES, nullptr);
    CHECK(registers.get('1', nullptr) == LINES);
    CHECK(registers.get('"', nullptr) == LINES);
    registers.deleted('"', Yanked{.pieces = {"x\ny"}, .linewise = false}, nullptr);
    CHECK(registers.get('1', nullptr) == Yanked{.pieces = {"x\ny"}, .linewise = false});
    CHECK(registers.get('2', nullptr) == LINES);
    // Within a line: -, and 1 stays.
    registers.deleted('"', SPIN, nullptr);
    CHECK(registers.get('-', nullptr) == SPIN);
    CHECK(registers.get('"', nullptr) == SPIN);
    CHECK(registers.get('2', nullptr) == LINES);
    // The motions Vim always numbers put even a little in 1.
    registers.deleted('"', Yanked{.pieces = {"(a)"}, .linewise = false}, nullptr, true);
    CHECK(registers.get('1', nullptr) == Yanked{.pieces = {"(a)"}, .linewise = false});
}

TEST_CASE("A named register takes the text, A to Z adding to it", "[vim][registers]") {
    Registers registers;
    registers.yank('"', SPIN, nullptr);
    registers.yank('a', Yanked{.pieces = {"ro"}, .linewise = false}, nullptr);
    CHECK(registers.get('a', nullptr) == Yanked{.pieces = {"ro"}, .linewise = false});
    CHECK(registers.get('"', nullptr) == Yanked{.pieces = {"ro"}, .linewise = false});
    // 0 keeps the last yank that named none.
    CHECK(registers.get('0', nullptr) == SPIN);
    registers.yank('A', Yanked{.pieces = {"ll"}, .linewise = false}, nullptr);
    CHECK(registers.get('a', nullptr) == Yanked{.pieces = {"roll"}, .linewise = false});
    CHECK(registers.get('A', nullptr) == Yanked{.pieces = {"roll"}, .linewise = false});
    CHECK(registers.get('"', nullptr) == Yanked{.pieces = {"roll"}, .linewise = false});
    // Lines added to text make lines, a break between.
    registers.yank('A', LINES, nullptr);
    CHECK(registers.get('a', nullptr) == Yanked{.pieces = {"roll\na\nb"}, .linewise = true});
    // Lines deleted into a named register go to 1 as well.
    registers.deleted('b', LINES, nullptr);
    CHECK(registers.get('b', nullptr) == LINES);
    CHECK(registers.get('1', nullptr) == LINES);
}

TEST_CASE("The black hole keeps nothing, and leaves the unnamed register", "[vim][registers]") {
    Registers registers;
    registers.yank('"', SPIN, nullptr);
    registers.deleted('_', LINES, nullptr);
    registers.yank('_', LINES, nullptr);
    CHECK(registers.get('"', nullptr) == SPIN);
    CHECK_FALSE(registers.get('1', nullptr));
    CHECK_FALSE(registers.get('_', nullptr));
}

TEST_CASE("+ and * are the clipboard, whole lines when its text ends with a break", "[vim][registers]") {
    Registers registers;
    TestClipboard clip;
    registers.yank('+', LINES, &clip.clipboard);
    CHECK(clip.text == "a\nb\n");
    CHECK(registers.get('"', &clip.clipboard) == LINES);
    CHECK(registers.get('*', &clip.clipboard) == LINES);
    clip.text = "from elsewhere";
    CHECK(registers.get('+', &clip.clipboard) == Yanked{.pieces = {"from elsewhere"}, .linewise = false});
    clip.text.clear();
    CHECK_FALSE(registers.get('+', &clip.clipboard));
    CHECK_FALSE(registers.get('+', nullptr));
}

TEST_CASE("A register's pieces, one for each caret, make one text", "[vim][registers]") {
    CHECK(Yanked{.pieces = {"a", "b"}, .linewise = false}.text() == "a\nb");
    CHECK(Yanked{.pieces = {"a", "b"}, .linewise = true}.text() == "a\nb\n");
    CHECK(Registers::valid('a'));
    CHECK(Registers::valid('Z'));
    CHECK(Registers::valid('7'));
    CHECK(Registers::valid('+'));
    CHECK_FALSE(Registers::valid('!'));
    CHECK_FALSE(Registers::valid(' '));
}

TEST_CASE("With the clipboard for the unnamed register, yanks and deletes go there too, and a put takes it",
          "[vim][registers]") {
    TestClipboard board;
    Registers registers;
    registers.use_clipboard(true);
    // Each caret's piece kept while the clipboard holds what was yanked.
    const Yanked carets{.pieces = {"a", "b"}, .linewise = false};
    registers.yank('"', carets, &board.clipboard);
    CHECK(board.text == "a\nb");
    CHECK(registers.get('"', &board.clipboard) == carets);
    // What another program put there.
    board.text = "from elsewhere";
    CHECK(registers.get('"', &board.clipboard)->text() == "from elsewhere");
    registers.deleted('"', SPIN, &board.clipboard);
    CHECK(board.text == "spin");
    // A register named keeps to itself.
    registers.yank('a', LINES, &board.clipboard);
    CHECK(board.text == "spin");
    registers.use_clipboard(false);
    registers.yank('"', SPIN, &board.clipboard);
    board.text = "elsewhere";
    CHECK(registers.get('"', &board.clipboard) == SPIN);
}
