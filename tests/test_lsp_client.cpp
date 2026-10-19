#include "fjedit/lsp/client.hpp"

#include "fjedit/core/document.hpp"
#include "fjedit/core/language.hpp"

#include "print.hpp"
#include "temp_folder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

using namespace fjedit;
namespace fs = std::filesystem;

namespace {

// A project of a test's own with clangd on it, its files given C++20.
struct Project {
    TempFolder folder;
    std::unique_ptr<lsp::Client> clangd;

    Project() {
        folder.write("compile_flags.txt", "-std=c++20\n");
        clangd = std::make_unique<lsp::Client>(lsp::ClientOptions{
            // No mark before a completion's name for whether it brings an
            // #include: the label is the name.
            .command = {"clangd", "--log=error", "--header-insertion-decorators=0"},
            .root = folder.path,
            .extensions = {"cpp", "hpp"},
            .language_id = [](const fs::path&) { return std::string("cpp"); },
            .arrived = {},
        });
    }

    // Brings the client up to date until `done`, for at most 30 seconds:
    // clangd parses a file first, which takes a while.
    bool until(const std::function<bool()>& done) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < until) {
            clangd->update();
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    // Waits for `asked` to be answered, and gives a copy of the answer: the
    // question asked in the same statement goes with it.
    template <typename T>
    T answer(const Asked<T>& asked) {
        REQUIRE(until([&] { return asked->answered; }));
        return asked->answer;
    }
};

// A document of C++ for `text`.
std::unique_ptr<Document> cpp(std::string_view text) {
    return std::make_unique<Document>(text, language_for("coin.cpp"));
}

} // namespace

// clangd is found on the PATH; where it isn't, as on CI, these are skipped.
#define SKIP_WITHOUT_CLANGD(project)                                                                         \
    if ((project).clangd->status().state == ServiceState::Stopped) {                                         \
        SKIP("clangd isn't installed: " << (project).clangd->status().note);                                 \
    }

TEST_CASE("clangd finds the problems in a file opened, and they follow an edit there", "[lsp][clangd]") {
    Project project;
    SKIP_WITHOUT_CLANGD(project);
    const fs::path path = project.folder.write("coin.cpp", "");
    auto document = cpp("int spin() {\n    return speed;\n}\n");
    project.clangd->open(path, *document);
    REQUIRE(project.until([&] { return !project.clangd->diagnostics(path).empty(); }));
    const Diagnostic& problem = project.clangd->diagnostics(path).front();
    REQUIRE(problem.severity == Severity::Error);
    REQUIRE(problem.span == Span{{1, 11}, {1, 16}});
    REQUIRE(problem.message.find("speed") != std::string::npos);
    REQUIRE(project.clangd->status().state == ServiceState::Ready);

    // One edit of two changes: speed declared at the top, then used.
    (void)document->apply(Edit{.changes = {{.span = {{0, 0}, {0, 0}}, .text = "int speed = 2;\n"},
                                           {.span = {{1, 11}, {1, 16}}, .text = "speed * 3"}}});
    REQUIRE(project.until([&] { return project.clangd->diagnostics(path).empty(); }));

    // And a mistake made on the line the edit moved.
    (void)document->apply(Edit{.changes = {{.span = {{2, 20}, {2, 20}}, .text = " + oops"}}});
    REQUIRE(project.until([&] { return !project.clangd->diagnostics(path).empty(); }));
    REQUIRE(project.clangd->diagnostics(path).front().span == Span{{2, 23}, {2, 27}});
}

TEST_CASE("Problems found before an edit are carried to where their text moved", "[lsp][clangd]") {
    Project project;
    SKIP_WITHOUT_CLANGD(project);
    const fs::path path = project.folder.write("coin.cpp", "");
    auto document = cpp("int spin() {\n    return speed;\n}\n");
    project.clangd->open(path, *document);
    REQUIRE(project.until([&] { return !project.clangd->diagnostics(path).empty(); }));
    // Before clangd hears of it: two lines put in above the problem.
    (void)document->apply(Edit{.changes = {{.span = {{0, 0}, {0, 0}}, .text = "// a\n// b\n"}}});
    REQUIRE(project.clangd->diagnostics(path).front().span == Span{{3, 11}, {3, 16}});
}

TEST_CASE("clangd completes a member, and says what code is and where it is defined", "[lsp][clangd]") {
    Project project;
    SKIP_WITHOUT_CLANGD(project);
    const fs::path path = project.folder.write("coin.cpp", "");
    auto document = cpp("struct Coin {\n    int spin_speed;\n    int spin_count;\n};\n"
                        "int f(Coin c) { return c.spin_speed + c.spin; }\n");
    project.clangd->open(path, *document);

    // A hover waits for clangd to have parsed the file; completion before
    // then gives the file's words alone.
    const std::optional<Hover> hover = project.answer(project.clangd->hover(path, Position{4, 27}));
    REQUIRE(hover.has_value());
    REQUIRE(hover->markdown.find("spin_speed") != std::string::npos);

    const std::vector<Location> definition = project.answer(project.clangd->definition(path, Position{4, 27}));
    REQUIRE(definition.size() == 1);
    REQUIRE(definition.front().path == path);
    REQUIRE(definition.front().span == Span{{1, 8}, {1, 18}});

    const Completions completions = project.answer(project.clangd->complete(path, Position{4, 44}, {}));
    std::vector<std::string> labels;
    for (const Completion& completion : completions.items) labels.push_back(completion.label);
    REQUIRE(std::ranges::contains(labels, "spin_speed"));
    REQUIRE(std::ranges::contains(labels, "spin_count"));
    const auto count = std::ranges::find(completions.items, "spin_count", &Completion::label);
    REQUIRE(count->kind == CompletionKind::Field);
    REQUIRE(count->detail == "int");
    REQUIRE(count->change.span == Span{{4, 40}, {4, 44}});
}

TEST_CASE("clangd renames everywhere a name is used, and formats a file", "[lsp][clangd]") {
    Project project;
    SKIP_WITHOUT_CLANGD(project);
    const fs::path path = project.folder.write("coin.cpp", "");
    auto document = cpp("int  spin_speed=2;\nint f() { return spin_speed; }\n");
    project.clangd->open(path, *document);

    const ftk::Result<WorkspaceEdit> renamed = project.answer(project.clangd->rename(path, Position{1, 18}, "speed"));
    REQUIRE(renamed.has_value());
    REQUIRE(renamed->contains(path));
    REQUIRE(renamed->at(path).edit.changes.size() == 2);
    REQUIRE(renamed->at(path).edit.changes.front().text == "speed");

    const std::optional<VersionedEdit> formatted = project.answer(project.clangd->format(path));
    REQUIRE(formatted.has_value());
    REQUIRE(formatted->version == document->version());
    (void)document->apply(formatted->edit);
    REQUIRE(document->buffer().line(0) == "int spin_speed = 2;");
}

TEST_CASE("clangd's colours come for the text, and are carried through an edit until the next", "[lsp][clangd]") {
    Project project;
    SKIP_WITHOUT_CLANGD(project);
    const fs::path path = project.folder.write("coin.cpp", "");
    auto document = cpp("struct Coin {};\nCoin coin;\n");
    project.clangd->open(path, *document);
    REQUIRE(project.until([&] { return project.clangd->tokens(path) != nullptr; }));
    const auto kind_at = [&](std::size_t line, std::size_t byte) {
        for (const Token& token : project.clangd->tokens(path)->lines[line]) {
            if (token.begin == byte) return token.kind;
        }
        return TokenKind::Number;
    };
    REQUIRE(kind_at(1, 0) == TokenKind::Type);

    // A line put in above: the colours move with their text at once.
    (void)document->apply(Edit{.changes = {{.span = {{0, 0}, {0, 0}}, .text = "\n"}}});
    REQUIRE(project.clangd->tokens(path) != nullptr);
    REQUIRE(kind_at(2, 0) == TokenKind::Type);
}

TEST_CASE("A client whose server isn't there is stopped, and answers at once with nothing", "[lsp]") {
    const TempFolder folder;
    lsp::Client client({.command = {"fjedit-no-such-server-anywhere"},
                        .root = folder.path,
                        .extensions = {"cpp"},
                        .language_id = {},
                        .arrived = {}});
    REQUIRE(client.status().state == ServiceState::Stopped);
    REQUIRE(client.status().note == "fjedit-no-such-server-anywhere isn't on the PATH");
    auto document = cpp("int a;\n");
    const fs::path path = folder.write("a.cpp", "int a;\n");
    client.open(path, *document);
    client.update();
    const Asked<std::optional<Hover>> hover = client.hover(path, Position{0, 4});
    REQUIRE(hover->answered);
    REQUIRE_FALSE(hover->answer.has_value());
    REQUIRE(client.serves("x/coin.CPP"));
    REQUIRE_FALSE(client.serves("x/coin.fjsl"));
}

TEST_CASE("A server's trigger characters are kept from initialize, and a completion tells it why", "[lsp]") {
    const TempFolder folder;
    lsp::Client fake({.command = {FJEDIT_FAKE_SERVER},
                      .root = folder.path,
                      .extensions = {"fjss"},
                      .language_id = [](const fs::path&) { return std::string("fjss"); },
                      .arrived = {}});
    // Brings the client up to date until `done`, for at most 10 seconds.
    const auto until = [&](const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            fake.update();
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    };
    // None until the server has said.
    REQUIRE(fake.completion_triggers("hud.fjss").empty());
    REQUIRE(until([&] { return fake.status().state == ServiceState::Ready; }));
    REQUIRE(fake.completion_triggers("hud.fjss") == std::vector<std::string>{".", "@"});
    REQUIRE(fake.completion_triggers("coin.cpp").empty());

    const fs::path path = folder.write("hud.fjss", "@\n");
    const Document sheet("@\n", language_for("hud.fjss"));
    fake.open(path, sheet);
    // The fake labels its one entry with the context it was sent.
    const auto told = [&](const CompletionTrigger& trigger) {
        const Asked<Completions> asked = fake.complete(path, Position{0, 1}, trigger);
        REQUIRE(until([&] { return asked->answered; }));
        REQUIRE(asked->answer.items.size() == 1);
        return asked->answer.items.front().label;
    };
    CHECK(told({.kind = CompletionTrigger::Kind::Character, .character = "@"}) ==
          R"({"triggerCharacter":"@","triggerKind":2})");
    CHECK(told({}) == R"({"triggerKind":1})");
    CHECK(told({.kind = CompletionTrigger::Kind::Incomplete, .character = {}}) == R"({"triggerKind":3})");
}

TEST_CASE("A server named by a path that isn't there says so, and what to do about it", "[lsp]") {
    const TempFolder folder;
    const std::string gone = (folder.path / "gone" / "server").string();
    const lsp::Client client({.command = {gone},
                              .root = folder.path,
                              .extensions = {"cpp"},
                              .language_id = {},
                              .arrived = {},
                              .if_missing = "set its path in Preferences"});
    REQUIRE(client.status().state == ServiceState::Stopped);
    REQUIRE(client.status().note == gone + " doesn't exist; set its path in Preferences");
}
