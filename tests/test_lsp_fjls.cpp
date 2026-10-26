#include "fjedit/lsp/client.hpp"

#include "fjedit/core/document.hpp"
#include "fjedit/core/language.hpp"

#include "temp_folder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

using namespace fjedit;
namespace fs = std::filesystem;

namespace {

std::string variable(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? value : "";
}

std::string_view name_of(TokenKind kind) {
    constexpr std::array<std::string_view, 12> NAMES = {"Text",    "Keyword", "Type",        "Function",
                                                        "String",  "Number",  "Comment",     "Meta",
                                                        "Punctuation", "Member", "Uniform", "Builtin"};
    return NAMES.at(static_cast<std::size_t>(kind));
}

// What each byte of a line `size` long is coloured as by `tokens`; Text
// where none is.
std::vector<TokenKind> by_byte(std::size_t size, std::span<const Token> tokens) {
    std::vector<TokenKind> kinds(size, TokenKind::Text);
    for (const Token& token : tokens) {
        for (std::size_t i = token.begin; i < std::min(token.end, size); ++i) kinds[i] = token.kind;
    }
    return kinds;
}

// Whether fjls's colour `told` may stand over the lexer's `guessed`: the
// same, or what only fjls knows over plain text or over what the lexer
// guesses from a name's shape, a type or a function: a uniform, a built-in, a
// macro's use, a struct made like a call, a keyword written as one.
bool agrees(TokenKind guessed, TokenKind told) {
    if (told == guessed || guessed == TokenKind::Text) return true;
    const bool known = told == TokenKind::Uniform || told == TokenKind::Builtin || told == TokenKind::Meta ||
                       told == TokenKind::Type || told == TokenKind::Keyword;
    return known && (guessed == TokenKind::Type || guessed == TokenKind::Function);
}

std::string read_text(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

// fjell's own files are the samples, in the tree $FJELL_SOURCE_DIR names, and
// the fjls asked is $FJLS's, else the PATH's; without them, as on CI, this is
// skipped.
TEST_CASE("fjls colours fjell's own files as fjedit's lexers do, its uniforms and built-ins besides",
          "[lsp][fjls]") {
    const fs::path source = variable("FJELL_SOURCE_DIR");
    if (source.empty()) SKIP("FJELL_SOURCE_DIR doesn't name fjell's tree, whose files are the samples");
    const std::string named = variable("FJLS");

    // Its own GLSL too: the stages and the includes they take in.
    constexpr std::array<std::string_view, 7> SHADERS = {".fjsl", ".vert", ".frag", ".comp", ".mesh", ".task", ".glsl"};
    std::vector<fs::path> samples{source / "assets" / "ui" / "test.fjss", source / "assets" / "ui" / "test.fjwidget"};
    for (const fs::path& folder : {source / "shaders", source / "shaders" / "include"}) {
        for (const fs::directory_entry& entry : fs::directory_iterator(folder)) {
            if (std::ranges::contains(SHADERS, entry.path().extension().string())) samples.push_back(entry.path());
        }
    }
    std::ranges::sort(samples);

    // The documents outlive the client that follows them, whatever ends the test.
    std::vector<std::unique_ptr<Document>> documents;
    lsp::Client fjls({.command = {named.empty() ? std::string("fjls") : named},
                      .root = source,
                      .extensions = {"fjsl", "fjss", "fjwidget", "vert", "frag", "comp", "mesh", "task", "glsl"},
                      .language_id =
                          [](const fs::path& path) {
                              const std::string extension = path.extension().string().substr(1);
                              return extension == "fjsl" || extension == "fjss" || extension == "fjwidget"
                                         ? extension
                                         : std::string("glsl");
                          },
                      .arrived = {}});
    if (fjls.status().state == ServiceState::Stopped) SKIP("fjls isn't there: " << fjls.status().note);

    std::vector<std::string> differences;
    bool uniforms = false;
    bool builtins = false;
    for (const fs::path& path : samples) {
        const Document* document =
            documents.emplace_back(std::make_unique<Document>(read_text(path), language_for(path.filename().string())))
                .get();
        fjls.open(path, *document);
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (fjls.tokens(path) == nullptr && std::chrono::steady_clock::now() < until) {
            fjls.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const SemanticTokens* told = fjls.tokens(path);
        INFO(path.string());
        REQUIRE(told != nullptr);

        for (std::size_t line = 0; line < document->buffer().line_count(); ++line) {
            const std::string_view text = document->buffer().line(line);
            const std::vector<TokenKind> guessed = by_byte(text.size(), document->syntax().tokens(line));
            const std::vector<TokenKind> said =
                by_byte(text.size(), line < told->lines.size() ? std::span<const Token>(told->lines[line])
                                                               : std::span<const Token>());
            uniforms = uniforms || std::ranges::contains(said, TokenKind::Uniform);
            builtins = builtins || std::ranges::contains(said, TokenKind::Builtin);
            for (std::size_t at = 0; at < text.size();) {
                std::size_t end = at + 1;
                while (end < text.size() && guessed[end] == guessed[at] && said[end] == said[at]) ++end;
                if (!agrees(guessed[at], said[at])) {
                    differences.push_back(std::format("{}:{}:{} '{}': the lexer says {}, fjls {}",
                                                      path.filename().string(), line + 1, at + 1,
                                                      text.substr(at, end - at), name_of(guessed[at]),
                                                      name_of(said[at])));
                }
                at = end;
            }
        }
        fjls.close(path);
    }

    std::string shown;
    for (std::size_t i = 0; i < std::min<std::size_t>(differences.size(), 40); ++i) shown += differences[i] + "\n";
    INFO(differences.size() << " differences:\n" << shown);
    CHECK(differences.empty());
    // And what only fjls knows is told: the shaders read uniforms and built-ins.
    CHECK(uniforms);
    CHECK(builtins);
}

// F12 in a shader through fjls, FJSL's and plain GLSL's alike: a call to a
// library function goes to where its include writes it.
TEST_CASE("fjls takes a shader's call to a library function to where its include writes it", "[lsp][fjls]") {
    const fs::path source = variable("FJELL_SOURCE_DIR");
    if (source.empty()) SKIP("FJELL_SOURCE_DIR doesn't name fjell's tree, whose files are the samples");
    const std::string named = variable("FJLS");

    std::vector<std::unique_ptr<Document>> documents;
    lsp::Client fjls({.command = {named.empty() ? std::string("fjls") : named},
                      .root = source,
                      .extensions = {"fjsl", "frag"},
                      .language_id =
                          [](const fs::path& path) {
                              return path.extension() == ".fjsl" ? std::string("fjsl") : std::string("glsl");
                          },
                      .arrived = {}});
    if (fjls.status().state == ServiceState::Stopped) SKIP("fjls isn't there: " << fjls.status().note);

    struct Call {
        std::string_view shader;
        std::string_view function;
        std::string_view include;
    };
    for (const Call& call : {Call{"grass_wind.fjsl", "grass_camera_tilt", "fjell_wind.glsl"},
                             Call{"placement_mask.frag", "overlay_surface", "fjell_overlay_surface.glsl"}}) {
        INFO(call.shader);
        const fs::path path = source / "shaders" / call.shader;
        const Document* document = documents
                                       .emplace_back(std::make_unique<Document>(read_text(path),
                                                                                language_for(path.filename().string())))
                                       .get();
        fjls.open(path, *document);
        // Its first call in code: the name, then its parenthesis.
        Position at{};
        const std::string called = std::string(call.function) + "(";
        for (std::size_t line = 0; line < document->buffer().line_count(); ++line) {
            const std::string_view text = document->buffer().line(line);
            const std::size_t found = text.find(called);
            if (found != std::string_view::npos && text.find("//") > found) {
                at = {.line = line, .byte = found + 1};
                break;
            }
        }
        REQUIRE(at.line > 0);

        const Asked<std::vector<Location>> asked = fjls.definition(path, at);
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!asked->answered && std::chrono::steady_clock::now() < until) {
            fjls.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(asked->answered);
        REQUIRE(asked->answer.size() == 1);
        const Location& written = asked->answer.front();
        CHECK(written.path.filename() == call.include);
        // The name as its include writes it, where the answer says.
        const Document include(read_text(written.path), language_for(written.path.filename().string()));
        REQUIRE(written.span.from.line < include.buffer().line_count());
        CHECK(include.buffer().line(written.span.from.line).substr(written.span.from.byte).starts_with(call.function));
        fjls.close(path);
    }
}

TEST_CASE("fjls names a sheet's colour values, they follow an edit, and a picked one comes back in its form",
          "[lsp][fjls]") {
    // Run with the others, where FJELL_SOURCE_DIR says fjell's tree and its
    // fjls are there; the sheet is the test's own.
    if (variable("FJELL_SOURCE_DIR").empty()) SKIP("FJELL_SOURCE_DIR doesn't name fjell's tree");
    const std::string named = variable("FJLS");
    TempFolder folder;
    const fs::path path = folder.write("hud.fjss", "");
    lsp::Client fjls({.command = {named.empty() ? std::string("fjls") : named},
                      .root = folder.path,
                      .extensions = {"fjss"},
                      .language_id = [](const fs::path&) { return std::string("fjss"); },
                      .arrived = {}});
    if (fjls.status().state == ServiceState::Stopped) SKIP("fjls isn't there: " << fjls.status().note);
    const auto until = [&](const std::function<bool()>& done) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < end) {
            fjls.update();
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    };

    Document document(".panel {\n    color: #44cc44;\n    background: rgba(34, 34, 34, 0.8);\n}\n",
                      language_for("hud.fjss"));
    fjls.open(path, document);
    REQUIRE(until([&] { return fjls.colour_values(path).size() == 2; }));
    const std::vector<ColourValue> values = fjls.colour_values(path);
    CHECK(values[0].span == Span{{1, 11}, {1, 18}});
    CHECK(values[1].span == Span{{2, 16}, {2, 37}});
    CHECK(std::lround(values[0].rgba[1] * 255.0f) == 0xcc);
    CHECK(values[1].rgba[3] == 0.8f);

    // Before fjls hears of it: two spaces put in before the first value
    // moves it, and the second stays.
    (void)document.apply(Edit{.changes = {{.span = {{1, 10}, {1, 10}}, .text = "  "}}});
    const std::vector<ColourValue>& carried = fjls.colour_values(path);
    REQUIRE(carried.size() == 2);
    CHECK(carried[0].span == Span{{1, 13}, {1, 20}});
    CHECK(carried[1].span == values[1].span);

    // The teal sheet 56 picks, written as the value is.
    const Asked<ColourForms> forms =
        fjls.colour_forms(path, carried[0].span, {63 / 255.0f, 184 / 255.0f, 196 / 255.0f, 1.0f});
    REQUIRE(until([&] { return forms->answered; }));
    REQUIRE(forms->answer.changes.size() == 2);
    CHECK(forms->answer.changes[0] == Change{.span = {{1, 13}, {1, 20}}, .text = "#3fb8c4"});
    CHECK(forms->answer.changes[1].text == "rgb(63, 184, 196)");
    CHECK(forms->answer.version == document.version());
}
