#pragma once

// A language service for tests of what draws and asks (the code view, the
// program): it says what the test gives it, and keeps every question asked
// with the Pending its answer goes into, for the test to answer when it
// likes.
//
//     FakeLanguage language;
//     language.problems = {{.span = {{1, 4}, {1, 7}}, .message = "x"}};
//     view.service = &language;
//     ... later:
//     language.answer(language.hovers.back(), Hover{.markdown = "int x"});

#include "fjedit/core/language_service.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// A question asked, and where its answer goes.
template <typename T>
struct Question {
    std::filesystem::path path;
    fjedit::Position at;
    std::shared_ptr<fjedit::Pending<T>> pending;
};

struct FakeLanguage final : fjedit::LanguageService {
    std::vector<fjedit::Diagnostic> problems;
    std::optional<fjedit::SemanticTokens> colours;
    /// The colour values it says are written.
    std::vector<fjedit::ColourValue> values;
    fjedit::ServiceStatus state{.state = fjedit::ServiceState::Ready, .note = {}};
    /// What, typed, asks it for completion.
    std::vector<std::string> triggers;

    std::vector<Question<fjedit::Completions>> completions;
    /// Why each completion was asked, in order.
    std::vector<fjedit::CompletionTrigger> completions_why;
    std::vector<Question<std::optional<fjedit::Hover>>> hovers;
    std::vector<Question<std::vector<fjedit::Location>>> definitions;
    /// Where a symbol stands, asked (documentHighlight).
    std::vector<Question<std::vector<fjedit::Span>>> highlights;
    std::vector<Question<ftk::Result<fjedit::WorkspaceEdit>>> renames;
    std::vector<std::string> new_names;
    std::vector<Question<std::optional<fjedit::VersionedEdit>>> formats;
    /// A colour's forms asked, at the start of the value's span, and the
    /// span and colour each was asked for.
    std::vector<Question<fjedit::ColourForms>> forms;
    std::vector<fjedit::Span> forms_spans;
    std::vector<std::array<float, 4>> forms_colours;
    std::vector<std::filesystem::path> opened;
    std::vector<std::filesystem::path> closed;
    std::vector<std::filesystem::path> saved_paths;

    // Answers `question` with `answer`.
    template <typename T>
    static void answer(const Question<T>& question, T answer) {
        question.pending->answer = std::move(answer);
        question.pending->answered = true;
    }

    [[nodiscard]] bool serves(const std::filesystem::path&) const override { return true; }
    void open(const std::filesystem::path& path, const fjedit::Document&) override { opened.push_back(path); }
    void close(const std::filesystem::path& path) override { closed.push_back(path); }
    void saved(const std::filesystem::path& path) override { saved_paths.push_back(path); }
    void update() override {}
    [[nodiscard]] fjedit::ServiceStatus status() const override { return state; }
    [[nodiscard]] const std::vector<fjedit::Diagnostic>& diagnostics(const std::filesystem::path&) override {
        return problems;
    }
    [[nodiscard]] const fjedit::SemanticTokens* tokens(const std::filesystem::path&) override {
        return colours ? &*colours : nullptr;
    }
    [[nodiscard]] const std::vector<fjedit::ColourValue>& colour_values(const std::filesystem::path&) override {
        return values;
    }
    [[nodiscard]] fjedit::Asked<fjedit::ColourForms> colour_forms(const std::filesystem::path& path, fjedit::Span span,
                                                                  const std::array<float, 4>& rgba) override {
        forms_spans.push_back(span);
        forms_colours.push_back(rgba);
        return ask(forms, path, span.from);
    }

    [[nodiscard]] std::vector<std::string> completion_triggers(const std::filesystem::path&) const override {
        return triggers;
    }
    [[nodiscard]] fjedit::Asked<fjedit::Completions> complete(const std::filesystem::path& path, fjedit::Position at,
                                                              const fjedit::CompletionTrigger& trigger) override {
        completions_why.push_back(trigger);
        return ask(completions, path, at);
    }
    [[nodiscard]] fjedit::Asked<std::optional<fjedit::Hover>> hover(const std::filesystem::path& path,
                                                                    fjedit::Position at) override {
        return ask(hovers, path, at);
    }
    [[nodiscard]] fjedit::Asked<std::vector<fjedit::Location>> definition(const std::filesystem::path& path,
                                                                          fjedit::Position at) override {
        return ask(definitions, path, at);
    }
    [[nodiscard]] fjedit::Asked<std::vector<fjedit::Span>> occurrences(const std::filesystem::path& path,
                                                                       fjedit::Position at) override {
        return ask(highlights, path, at);
    }
    [[nodiscard]] fjedit::Asked<ftk::Result<fjedit::WorkspaceEdit>> rename(const std::filesystem::path& path,
                                                                           fjedit::Position at,
                                                                           std::string_view name) override {
        new_names.emplace_back(name);
        return ask(renames, path, at);
    }
    [[nodiscard]] fjedit::Asked<std::optional<fjedit::VersionedEdit>> format(const std::filesystem::path& path) override {
        return ask(formats, path, {});
    }

private:
    template <typename T>
    static fjedit::Asked<T> ask(std::vector<Question<T>>& asked, const std::filesystem::path& path, fjedit::Position at) {
        auto pending = std::make_shared<fjedit::Pending<T>>();
        asked.push_back({.path = path, .at = at, .pending = pending});
        return pending;
    }
};
