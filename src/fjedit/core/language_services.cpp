#include "fjedit/core/language_services.hpp"

#include <algorithm>
#include <utility>

namespace fjedit {

namespace fs = std::filesystem;

namespace {

// The file's extension, lower case and without its dot.
std::string extension_of(const fs::path& path) {
    std::string extension = path.extension().string();
    if (!extension.empty()) extension.erase(0, 1);
    for (char& c : extension) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    return extension;
}

template <typename T>
Asked<T> answered(T answer) {
    auto pending = std::make_shared<Pending<T>>();
    pending->answer = std::move(answer);
    pending->answered = true;
    return pending;
}

} // namespace

LanguageServices::LanguageServices(std::vector<ServiceMaker> makers, fs::path root) : root_(std::move(root)) {
    services_.reserve(makers.size());
    for (ServiceMaker& maker : makers) services_.push_back({.maker = std::move(maker), .service = nullptr, .told = {}});
}

bool LanguageServices::answers_for(const Running& running, const fs::path& path) const {
    const std::string extension = extension_of(path);
    return !extension.empty() && std::ranges::contains(running.maker.extensions, extension);
}

bool LanguageServices::serves(const fs::path& path) const {
    return std::ranges::any_of(services_, [&](const Running& running) { return answers_for(running, path); });
}

void LanguageServices::start(Running& running) {
    if (!running.maker.make) return;
    running.service = running.maker.make(root_);
    if (!running.service) return;
    for (const fs::path& path : running.told) {
        if (const auto document = documents_.find(path); document != documents_.end()) {
            running.service->open(path, *document->second);
        }
    }
}

void LanguageServices::open(const fs::path& path, const Document& document) {
    documents_[path] = &document;
    for (Running& running : services_) {
        if (!answers_for(running, path)) continue;
        const bool told = !running.told.insert(path).second;
        if (!running.service) {
            start(running);
        } else if (!told) {
            running.service->open(path, document);
        }
    }
}

void LanguageServices::close(const fs::path& path) {
    documents_.erase(path);
    problems_.erase(path);
    for (Running& running : services_) {
        if (running.told.erase(path) > 0 && running.service) running.service->close(path);
    }
}

void LanguageServices::saved(const fs::path& path) {
    for (Running& running : services_) {
        if (running.service && running.told.contains(path)) running.service->saved(path);
    }
}

void LanguageServices::update() {
    for (Running& running : services_) {
        if (running.service) running.service->update();
    }
    std::erase_if(merging_, [](const std::function<bool()>& merge) { return merge(); });
}

ServiceStatus LanguageServices::status() const {
    for (const Running& running : services_) {
        if (running.service) return running.service->status();
    }
    return {.state = ServiceState::Stopped, .note = "No language service is running"};
}

std::vector<LanguageServices::Running*> LanguageServices::told_of(const fs::path& path) {
    std::vector<Running*> told;
    for (Running& running : services_) {
        if (running.service && running.told.contains(path)) told.push_back(&running);
    }
    return told;
}

const std::vector<Diagnostic>& LanguageServices::diagnostics(const fs::path& path) {
    static const std::vector<Diagnostic> NONE;
    const std::vector<Running*> told = told_of(path);
    if (told.empty()) return NONE;
    if (told.size() == 1) return told.front()->service->diagnostics(path);
    std::vector<Diagnostic>& all = problems_[path];
    all.clear();
    for (Running* running : told) {
        const std::vector<Diagnostic>& found = running->service->diagnostics(path);
        all.insert(all.end(), found.begin(), found.end());
    }
    return all;
}

const SemanticTokens* LanguageServices::tokens(const fs::path& path) {
    for (Running* running : told_of(path)) {
        if (const SemanticTokens* colours = running->service->tokens(path)) return colours;
    }
    return nullptr;
}

const std::vector<ColourValue>& LanguageServices::colour_values(const fs::path& path) {
    static const std::vector<ColourValue> NONE;
    for (Running* running : told_of(path)) {
        const std::vector<ColourValue>& values = running->service->colour_values(path);
        if (!values.empty()) return values;
    }
    return NONE;
}

Asked<ColourForms> LanguageServices::colour_forms(const fs::path& path, Span span, const std::array<float, 4>& rgba) {
    for (Running* running : told_of(path)) {
        const bool named = std::ranges::any_of(running->service->colour_values(path),
                                               [&](const ColourValue& value) { return value.span == span; });
        if (named) return running->service->colour_forms(path, span, rgba);
    }
    return answered(ColourForms{});
}

std::vector<std::string> LanguageServices::completion_triggers(const fs::path& path) const {
    std::vector<std::string> all;
    for (const Running& running : services_) {
        if (!running.service || !running.told.contains(path)) continue;
        for (std::string& trigger : running.service->completion_triggers(path)) {
            if (!std::ranges::contains(all, trigger)) all.push_back(std::move(trigger));
        }
    }
    return all;
}

template <typename T>
Asked<T> LanguageServices::ask_all(std::vector<Running*> services, const std::function<Asked<T>(LanguageService&)>& ask,
                                   std::function<T(std::vector<T> answers)> fold, T none) {
    if (services.empty()) return answered(std::move(none));
    if (services.size() == 1) return ask(*services.front()->service);

    struct Part {
        Running* running;
        uint64_t generation;
        Asked<T> asked;
    };
    std::vector<Part> parts;
    for (Running* running : services) parts.push_back({running, running->generation, ask(*running->service)});
    auto merged = std::make_shared<Pending<T>>();
    std::weak_ptr<Pending<T>> asker = merged;
    merging_.push_back([parts = std::move(parts), asker, fold = std::move(fold), none = std::move(none)] {
        // Given up: the parts go with this, and their services give up too.
        const std::shared_ptr<Pending<T>> held = asker.lock();
        if (!held) return true;
        std::vector<T> answers;
        for (const Part& part : parts) {
            // Asked of a service since started again: it will never answer.
            if (part.running->generation != part.generation) continue;
            if (!part.asked->answered) return false;
            answers.push_back(part.asked->answer);
        }
        held->answer = answers.empty() ? none : fold(std::move(answers));
        held->answered = true;
        return true;
    });
    return merged;
}

Asked<Completions> LanguageServices::complete(const fs::path& path, Position at, const CompletionTrigger& trigger) {
    const auto fold = [at](std::vector<Completions> answers) {
        Completions all{.items = {}, .incomplete = false, .at = at, .version = 0};
        for (Completions& answer : answers) {
            all.items.insert(all.items.end(), std::make_move_iterator(answer.items.begin()),
                             std::make_move_iterator(answer.items.end()));
            all.incomplete = all.incomplete || answer.incomplete;
            // A service that could not answer says no version.
            all.version = std::max(all.version, answer.version);
        }
        return all;
    };
    std::vector<Running*> asked = told_of(path);
    // A character asks only the services that name it: another would take
    // it for a word begun, and answer with everything.
    if (trigger.kind == CompletionTrigger::Kind::Character) {
        std::erase_if(asked, [&](const Running* running) {
            return !std::ranges::contains(running->service->completion_triggers(path), trigger.character);
        });
    }
    return ask_all<Completions>(
        std::move(asked), [&](LanguageService& service) { return service.complete(path, at, trigger); }, fold,
        Completions{.items = {}, .incomplete = false, .at = at, .version = 0});
}

Asked<std::optional<Hover>> LanguageServices::hover(const fs::path& path, Position at) {
    const auto fold = [](std::vector<std::optional<Hover>> answers) -> std::optional<Hover> {
        std::optional<Hover> card;
        for (const std::optional<Hover>& answer : answers) {
            if (!answer || answer->markdown.empty()) continue;
            if (!card) {
                card = answer;
            } else {
                card->markdown += "\n\n---\n\n" + answer->markdown;
            }
        }
        return card;
    };
    return ask_all<std::optional<Hover>>(
        told_of(path), [&](LanguageService& service) { return service.hover(path, at); }, fold, std::nullopt);
}

Asked<std::vector<Location>> LanguageServices::definition(const fs::path& path, Position at) {
    const auto fold = [](std::vector<std::vector<Location>> answers) {
        const auto found = std::ranges::find_if(answers, [](const auto& answer) { return !answer.empty(); });
        return found != answers.end() ? std::move(*found) : std::vector<Location>{};
    };
    return ask_all<std::vector<Location>>(
        told_of(path), [&](LanguageService& service) { return service.definition(path, at); }, fold, {});
}

Asked<std::vector<Span>> LanguageServices::occurrences(const fs::path& path, Position at) {
    const auto fold = [](std::vector<std::vector<Span>> answers) {
        const auto found = std::ranges::find_if(answers, [](const auto& answer) { return !answer.empty(); });
        return found != answers.end() ? std::move(*found) : std::vector<Span>{};
    };
    return ask_all<std::vector<Span>>(
        told_of(path), [&](LanguageService& service) { return service.occurrences(path, at); }, fold, {});
}

Asked<ftk::Result<WorkspaceEdit>> LanguageServices::rename(const fs::path& path, Position at, std::string_view name) {
    // The first that can rename it; when none can, why the first can't.
    const auto fold = [](std::vector<ftk::Result<WorkspaceEdit>> answers) {
        const auto found = std::ranges::find_if(answers, [](const auto& answer) { return answer.has_value(); });
        return found != answers.end() ? std::move(*found) : std::move(answers.front());
    };
    return ask_all<ftk::Result<WorkspaceEdit>>(
        told_of(path), [&](LanguageService& service) { return service.rename(path, at, name); }, fold,
        ftk::make_error("No language service can rename here"));
}

Asked<std::optional<VersionedEdit>> LanguageServices::format(const fs::path& path) {
    const auto fold = [](std::vector<std::optional<VersionedEdit>> answers) {
        const auto found = std::ranges::find_if(answers, [](const auto& answer) { return answer.has_value(); });
        return found != answers.end() ? std::move(*found) : std::optional<VersionedEdit>{};
    };
    return ask_all<std::optional<VersionedEdit>>(
        told_of(path), [&](LanguageService& service) { return service.format(path); }, fold, std::nullopt);
}

std::set<fs::path> LanguageServices::files() const {
    std::set<fs::path> all;
    for (const Running& running : services_) all.insert(running.told.begin(), running.told.end());
    return all;
}

std::vector<ServiceShown> LanguageServices::shown(const fs::path& path) const {
    std::vector<ServiceShown> shown;
    for (const Running& running : services_) {
        if (running.service && answers_for(running, path)) {
            shown.push_back({.name = running.maker.name, .status = running.service->status()});
        }
    }
    return shown;
}

std::optional<ServiceStatus> LanguageServices::status_of(std::string_view name) const {
    for (const Running& running : services_) {
        if (running.maker.name == name && running.service) return running.service->status();
    }
    return std::nullopt;
}

void LanguageServices::restart(std::string_view name) {
    for (Running& running : services_) {
        if (running.maker.name != name || !running.service) continue;
        running.service.reset();
        ++running.generation;
        start(running);
    }
}

} // namespace fjedit
