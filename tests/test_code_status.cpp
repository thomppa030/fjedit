#include "fjedit/editor/code_status.hpp"

#include "code_area_running.hpp"
#include "fake_language.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace fjedit;
using ftk::ui::Key;

namespace {

// A fake clangd for C++ files, whose problems the test gives once it is
// made.
struct FakeClangd {
    FakeLanguage* made{nullptr};

    [[nodiscard]] std::vector<ServiceMaker> makers() {
        return {{.name = "clangd",
                 .extensions = {"cpp"},
                 .make = [this](const std::filesystem::path&) -> std::unique_ptr<LanguageService> {
                     auto language = std::make_unique<FakeLanguage>();
                     made = language.get();
                     return language;
                 }}};
    }
};

} // namespace

TEST_CASE("The status says Vim's mode, the keys half typed and what the last key said; with Vim off, nothing of Vim",
          "[editor][status][vim]") {
    Area a({{"a.txt", "abc\n"}});
    a.open("a.txt");
    CHECK_FALSE(a.code->status().vim.has_value());

    a.preferences.vim = true;
    a.h.step(2);
    REQUIRE(a.code->status().vim.has_value());
    CHECK(a.code->status().vim->mode == "Normal");
    a.h.type("i");
    a.h.step();
    CHECK(a.code->status().vim->mode == "Insert");
    CHECK(a.code->status().vim->tone == ftk::ui::StatusTone::On);
    a.h.press(Key::Escape);
    a.h.type("d");
    a.h.step();
    CHECK(a.code->status().vim->keys == "d");
    a.h.press(Key::Escape);
    a.ex("nothing");
    CHECK(a.code->status().vim->said.error);
}

TEST_CASE("The status says the first line of keys.vim that couldn't be used, with Vim on", "[editor][status][vim]") {
    Area a({{"a.txt", "abc\n"}}, [](const TempFolder& folder) {
        folder.write(".config/keys.vim", "\" fine\nbind normal <F7> game.nothing\n");
    });
    a.preferences.vim = true;
    a.open("a.txt");
    const CodeStatus status = a.code->status();
    REQUIRE(status.vim.has_value());
    REQUIRE(status.vim->keys_error.has_value());
    CHECK(status.vim->keys_error->line == 2);
}

TEST_CASE("The status counts the problems in the files open, and gives the first of the worst in the file in front",
          "[editor][status]") {
    FakeClangd language;
    Area a({{"a.cpp", "int a;\nint b;\n"}}, {}, language.makers());
    a.open("a.cpp");
    REQUIRE(language.made != nullptr);
    language.made->problems = {{.span = {{0, 4}, {0, 5}}, .severity = Severity::Warning, .message = "unused a"},
                               {.span = {{1, 4}, {1, 5}}, .severity = Severity::Error, .message = "b is bad\nsee here"}};
    const CodeStatus status = a.code->status();
    REQUIRE(status.problems.has_value());
    CHECK(status.problems->errors == 1);
    CHECK(status.problems->warnings == 1);
    REQUIRE(status.first_problem.has_value());
    CHECK(status.first_problem->message == "b is bad");
    CHECK(status.first_problem->where == "a.cpp:2");
}

TEST_CASE("The status has no problems to count while no file is served and none has any", "[editor][status]") {
    Area a({{"a.txt", "abc\n"}});
    a.open("a.txt");
    CHECK_FALSE(a.code->status().problems.has_value());
    CHECK_FALSE(a.code->status().first_problem.has_value());
}

TEST_CASE("The status says where the caret is, how the file is written, its language and each service it asks",
          "[editor][status]") {
    FakeClangd language;
    Area a({{"a.cpp", "\xEF\xBB\xBF\tint a;\n"}}, {}, language.makers());
    CHECK_FALSE(a.code->status().place.has_value());
    a.open("a.cpp");
    a.code->go_to(a.folder.path / "a.cpp", {{0, 1}, {0, 1}});
    a.h.step(2);
    const CodeStatus status = a.code->status();
    REQUIRE(status.place.has_value());
    CHECK(status.place->line == 1);
    // After the tab, as wide as the indent lays it out.
    CHECK(status.place->column == status.place->indent.width + 1);
    CHECK(status.place->indent.tabs);
    CHECK(status.place->byte_order_mark);
    CHECK(status.place->language == "C++");
    REQUIRE(status.place->services.size() == 1);
    CHECK(status.place->services.front().name == "clangd");
    CHECK(status.place->services.front().status.state == ServiceState::Ready);
}

TEST_CASE("The status says what went wrong, until something works", "[editor][status]") {
    Area a({{"a.txt", "abc\n"}});
    a.code->open(a.folder.path / "gone.txt");
    a.h.step();
    CHECK_FALSE(a.code->status().note.empty());
    a.open("a.txt");
    CHECK(a.code->status().note.empty());
}
