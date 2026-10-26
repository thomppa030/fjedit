#include "fake_language.hpp"

#include "fjedit/core/document.hpp"
#include "fjedit/core/language.hpp"
#include "fjedit/core/language_services.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace fjedit;
namespace fs = std::filesystem;

namespace {

// Two services, as a program runs clangd and fjls: the first answers for C++,
// the second for headers as well and for its own files.
struct Two {
    FakeLanguage* first{nullptr};
    FakeLanguage* second{nullptr};
    int made{0};
    LanguageServices services;

    Two()
        : services({{.name = "clangd",
                     .extensions = {"cpp", "hpp"},
                     .make = [this](const fs::path&) -> std::unique_ptr<LanguageService> {
                         auto service = std::make_unique<FakeLanguage>();
                         first = service.get();
                         ++made;
                         return service;
                     }},
                    {.name = "fjls",
                     .extensions = {"hpp", "fjss"},
                     .make = [this](const fs::path&) -> std::unique_ptr<LanguageService> {
                         auto service = std::make_unique<FakeLanguage>();
                         second = service.get();
                         ++made;
                         return service;
                     }}},
                   "/game") {}
};

Document document(const fs::path& path) {
    return Document("int speed;\n", language_for(path.string()));
}

// Answers what the services are asked, and has them put their answers
// together.
template <typename T>
void answer_both(Two& two, std::vector<Question<T>>& first, std::vector<Question<T>>& second, T a, T b) {
    FakeLanguage::answer(first.back(), std::move(a));
    FakeLanguage::answer(second.back(), std::move(b));
    two.services.update();
}

} // namespace

TEST_CASE("A file is told to each service that answers for its kind, started when first wanted", "[services]") {
    Two two;
    const Document code = document("x.cpp");
    const Document header = document("a.hpp");
    const Document sheet = document("s.fjss");
    REQUIRE(two.made == 0);
    REQUIRE_FALSE(two.services.status_of("clangd").has_value());

    two.services.open("x.cpp", code);
    REQUIRE(two.first != nullptr);
    REQUIRE(two.second == nullptr);
    two.services.open("a.hpp", header);
    two.services.open("s.fjss", sheet);
    REQUIRE(two.made == 2);
    CHECK(two.first->opened == std::vector<fs::path>{"x.cpp", "a.hpp"});
    CHECK(two.second->opened == std::vector<fs::path>{"a.hpp", "s.fjss"});
    CHECK(two.services.files() == std::set<fs::path>{"x.cpp", "a.hpp", "s.fjss"});

    const auto shown = two.services.shown("a.hpp");
    REQUIRE(shown.size() == 2);
    CHECK(shown[0].name == "clangd");
    CHECK(shown[1].name == "fjls");
    CHECK(two.services.shown("x.cpp").size() == 1);

    two.services.saved("x.cpp");
    two.services.close("a.hpp");
    CHECK(two.first->saved_paths == std::vector<fs::path>{"x.cpp"});
    CHECK(two.second->saved_paths.empty());
    CHECK(two.first->closed == std::vector<fs::path>{"a.hpp"});
    CHECK(two.second->closed == std::vector<fs::path>{"a.hpp"});
}

TEST_CASE("A file's problems are all of its services' together, its colours the first's that has any", "[services]") {
    Two two;
    const Document code = document("x.cpp");
    const Document header = document("a.hpp");
    two.services.open("x.cpp", code);
    two.services.open("a.hpp", header);
    two.first->problems = {{.span = {{0, 0}, {0, 3}}, .message = "clang's"}};
    two.second->problems = {{.span = {{0, 4}, {0, 9}}, .message = "fjreflect's"}};

    const std::vector<Diagnostic>& both = two.services.diagnostics("a.hpp");
    REQUIRE(both.size() == 2);
    CHECK(both[0].message == "clang's");
    CHECK(both[1].message == "fjreflect's");
    CHECK(two.services.diagnostics("x.cpp").size() == 1);
    CHECK(two.services.diagnostics("never.cpp").empty());

    CHECK(two.services.tokens("a.hpp") == nullptr);
    two.second->colours = SemanticTokens{};
    CHECK(two.services.tokens("a.hpp") == &*two.second->colours);
}

TEST_CASE("A completion is one list once every service has answered", "[services]") {
    Two two;
    const Document header = document("a.hpp");
    two.services.open("a.hpp", header);
    const auto asked = two.services.complete("a.hpp", {0, 1}, {});
    REQUIRE(two.first->completions.size() == 1);
    REQUIRE(two.second->completions.size() == 1);

    FakeLanguage::answer(two.first->completions.back(),
                         Completions{.items = {{.label = "speed"}}, .incomplete = false, .at = {0, 1}, .version = 4});
    two.services.update();
    REQUIRE_FALSE(asked->answered);
    FakeLanguage::answer(two.second->completions.back(),
                         Completions{.items = {{.label = "property"}}, .incomplete = true, .at = {0, 1}, .version = 4});
    two.services.update();
    REQUIRE(asked->answered);
    REQUIRE(asked->answer.items.size() == 2);
    CHECK(asked->answer.items[0].label == "speed");
    CHECK(asked->answer.items[1].label == "property");
    CHECK(asked->answer.incomplete);
    CHECK(asked->answer.version == 4);

    // A file one service answers for has its answers as they are.
    const Document code = document("x.cpp");
    two.services.open("x.cpp", code);
    const auto alone = two.services.complete("x.cpp", {0, 1}, {});
    CHECK(alone.get() == two.first->completions.back().pending.get());
}

TEST_CASE("A character typed is asked only of the services that name it", "[services]") {
    Two two;
    const Document header = document("a.hpp");
    two.services.open("a.hpp", header);
    two.first->triggers = {".", ":"};
    two.second->triggers = {":", "("};
    // What any of the file's services names, each once.
    CHECK(two.services.completion_triggers("a.hpp") == std::vector<std::string>{".", ":", "("});
    const auto character = [](std::string typed) {
        return CompletionTrigger{.kind = CompletionTrigger::Kind::Character, .character = std::move(typed)};
    };

    // Named by fjls alone: its answer, as it is.
    const auto paren = two.services.complete("a.hpp", {0, 4}, character("("));
    REQUIRE(two.first->completions.empty());
    REQUIRE(two.second->completions.size() == 1);
    CHECK(paren.get() == two.second->completions.back().pending.get());
    CHECK(two.second->completions_why.back() == character("("));

    // Named by both: both asked, told why.
    (void)two.services.complete("a.hpp", {0, 4}, character(":"));
    CHECK(two.first->completions.size() == 1);
    CHECK(two.second->completions.size() == 2);
    CHECK(two.first->completions_why.back() == character(":"));

    // Named by neither: nothing asked, nothing to show.
    const auto at = two.services.complete("a.hpp", {0, 4}, character("@"));
    CHECK(two.first->completions.size() == 1);
    CHECK(two.second->completions.size() == 2);
    REQUIRE(at->answered);
    CHECK(at->answer.items.empty());

    // A word begun asks every service.
    (void)two.services.complete("a.hpp", {0, 4}, {});
    CHECK(two.first->completions.size() == 2);
    CHECK(two.second->completions.size() == 3);

    // A file one service answers for has that service's alone.
    const Document code = document("x.cpp");
    two.services.open("x.cpp", code);
    CHECK(two.services.completion_triggers("x.cpp") == std::vector<std::string>{".", ":"});
}

TEST_CASE("A hover is one card, a rule between its services' answers", "[services]") {
    Two two;
    const Document header = document("a.hpp");
    two.services.open("a.hpp", header);

    auto asked = two.services.hover("a.hpp", {0, 5});
    answer_both<std::optional<Hover>>(two, two.first->hovers, two.second->hovers,
                                      Hover{.markdown = "int speed", .span = Span{{0, 4}, {0, 9}}},
                                      Hover{.markdown = "**[[fj::range]]**", .span = std::nullopt});
    REQUIRE(asked->answered);
    REQUIRE(asked->answer.has_value());
    CHECK(asked->answer->markdown == "int speed\n\n---\n\n**[[fj::range]]**");
    CHECK(asked->answer->span == Span{{0, 4}, {0, 9}});

    asked = two.services.hover("a.hpp", {0, 5});
    answer_both<std::optional<Hover>>(two, two.first->hovers, two.second->hovers, std::nullopt,
                                      Hover{.markdown = "fjls alone", .span = std::nullopt});
    REQUIRE(asked->answer.has_value());
    CHECK(asked->answer->markdown == "fjls alone");

    asked = two.services.hover("a.hpp", {0, 5});
    answer_both<std::optional<Hover>>(two, two.first->hovers, two.second->hovers, std::nullopt, std::nullopt);
    REQUIRE(asked->answered);
    CHECK_FALSE(asked->answer.has_value());
}

TEST_CASE("A definition, a rename and a format come from the first service that has one", "[services]") {
    Two two;
    const Document header = document("a.hpp");
    two.services.open("a.hpp", header);
    const Location there{.path = "b.hpp", .span = {{2, 0}, {2, 5}}};

    const auto definition = two.services.definition("a.hpp", {0, 5});
    answer_both<std::vector<Location>>(two, two.first->definitions, two.second->definitions, {}, {there});
    REQUIRE(definition->answered);
    CHECK(definition->answer == std::vector<Location>{there});

    const std::vector<Span> uses{{{0, 4}, {0, 9}}, {{2, 0}, {2, 5}}};
    const auto occurrences = two.services.occurrences("a.hpp", {0, 5});
    answer_both<std::vector<Span>>(two, two.first->highlights, two.second->highlights, {}, uses);
    REQUIRE(occurrences->answered);
    CHECK(occurrences->answer == uses);

    const auto rename = two.services.rename("a.hpp", {0, 5}, "pace");
    answer_both<ftk::Result<WorkspaceEdit>>(two, two.first->renames, two.second->renames,
                                            ftk::make_error("clangd can't"), WorkspaceEdit{{"a.hpp", {}}});
    REQUIRE(rename->answered);
    CHECK(rename->answer.has_value());

    const auto refused = two.services.rename("a.hpp", {0, 5}, "pace");
    answer_both<ftk::Result<WorkspaceEdit>>(two, two.first->renames, two.second->renames,
                                            ftk::make_error("clangd can't"), ftk::make_error("fjls can't"));
    REQUIRE_FALSE(refused->answer.has_value());
    CHECK(refused->answer.error() == "clangd can't");

    const auto format = two.services.format("a.hpp");
    answer_both<std::optional<VersionedEdit>>(two, two.first->formats, two.second->formats,
                                              VersionedEdit{.version = 1, .edit = {}}, std::nullopt);
    REQUIRE(format->answered);
    CHECK(format->answer.has_value());
}

TEST_CASE("A question given up lets go of what its services were asked", "[services]") {
    Two two;
    const Document header = document("a.hpp");
    two.services.open("a.hpp", header);
    auto asked = two.services.hover("a.hpp", {0, 5});
    // The fake keeps what it was asked; the question holds the other end.
    CHECK(two.first->hovers.back().pending.use_count() == 2);
    asked.reset();
    two.services.update();
    CHECK(two.first->hovers.back().pending.use_count() == 1);
    CHECK(two.second->hovers.back().pending.use_count() == 1);
}

TEST_CASE("A service started again is told of its files, what the one before was asked answered without it",
          "[services]") {
    Two two;
    const Document header = document("a.hpp");
    two.services.open("a.hpp", header);
    const auto definition = two.services.definition("a.hpp", {0, 5});

    two.services.restart("clangd");
    REQUIRE(two.made == 3);
    CHECK(two.first->opened == std::vector<fs::path>{"a.hpp"});
    CHECK(two.first->definitions.empty());

    const Location there{.path = "b.hpp", .span = {{2, 0}, {2, 5}}};
    FakeLanguage::answer(two.second->definitions.back(), std::vector<Location>{there});
    two.services.update();
    REQUIRE(definition->answered);
    CHECK(definition->answer == std::vector<Location>{there});
}

TEST_CASE("A file's colour values are the first's that has any, a colour's forms asked of the one that named it",
          "[services]") {
    Two two;
    const Document header = document("a.hpp");
    two.services.open("a.hpp", header);
    constexpr std::array<float, 4> TEAL{63 / 255.0f, 184 / 255.0f, 196 / 255.0f, 1.0f};
    const Span tint{{0, 4}, {0, 11}};

    CHECK(two.services.colour_values("a.hpp").empty());
    two.second->values = {{.span = tint, .rgba = {1.0f, 1.0f, 1.0f, 1.0f}}};
    CHECK(&two.services.colour_values("a.hpp") == &two.second->values);

    // Asked of fjls, which named the value, and of no one else.
    const auto forms = two.services.colour_forms("a.hpp", tint, TEAL);
    CHECK(two.first->forms.empty());
    REQUIRE(two.second->forms.size() == 1);
    CHECK(two.second->forms_spans.back() == tint);
    CHECK(two.second->forms_colours.back() == TEAL);
    CHECK(forms.get() == two.second->forms.back().pending.get());

    // Where no service named a value, no one is asked, and the answer is none.
    const auto nowhere = two.services.colour_forms("a.hpp", {{0, 0}, {0, 3}}, TEAL);
    CHECK(two.second->forms.size() == 1);
    REQUIRE(nowhere->answered);
    CHECK(nowhere->answer.changes.empty());
}
