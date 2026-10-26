#pragma once

// Every language service a program runs, answering as one: a file is asked
// of each service that answers for its kind, in the order they were given,
// and their answers come back as one. Problems are all of theirs together; a
// completion is one list, each entry keeping its own service's change, and a
// character typed is asked only of the services that name it; a hover is one
// card, each answer under the one before with a rule between.
// A definition, where a symbol stands, a rename and a format come from the
// first service that has one, colours and colour values from the first that
// knows any, a colour's forms from the service that named it. A file one
// service answers for is that service's alone, its answers passed on as they
// are.
//
//     LanguageServices services({clangd, fjls}, folder);
//     view.service = &services;
//     services.open(path, document);
//     // each frame:
//     services.update();

#include "fjedit/core/language_service.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit {

/// A language service a program may run: made when the first file it
/// answers for is opened, and told of every such file open after.
struct ServiceMaker {
    /// What the status bar calls it ("clangd").
    std::string name{};
    /// The extensions of the files it answers for, lower case ("cpp").
    std::vector<std::string> extensions{};
    /// Makes it for the folder `root`; null when it can't be.
    std::function<std::unique_ptr<LanguageService>(const std::filesystem::path& root)> make{};
};

/// A running service, as a status bar shows it.
struct ServiceShown {
    std::string name{};
    ServiceStatus status{};
};

class LanguageServices final : public LanguageService {
public:
    /// The services `makers` make, for the folder `root`; none runs until a
    /// file it answers for opens.
    LanguageServices(std::vector<ServiceMaker> makers, std::filesystem::path root);

    LanguageServices(const LanguageServices&) = delete;
    LanguageServices& operator=(const LanguageServices&) = delete;
    LanguageServices(LanguageServices&&) = delete;
    LanguageServices& operator=(LanguageServices&&) = delete;

    /// Whether a service answers for the file at `path`.
    [[nodiscard]] bool serves(const std::filesystem::path& path) const override;
    /// Tells every service that answers for the file of it, starting those
    /// not running yet.
    void open(const std::filesystem::path& path, const Document& document) override;
    void close(const std::filesystem::path& path) override;
    void saved(const std::filesystem::path& path) override;
    /// Brings each running service up to date, and puts together the
    /// answers of every question all its services have answered.
    void update() override;
    /// How the first running service is; each one's is shown().
    [[nodiscard]] ServiceStatus status() const override;
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics(const std::filesystem::path& path) override;
    [[nodiscard]] const SemanticTokens* tokens(const std::filesystem::path& path) override;
    /// The first's that has any.
    [[nodiscard]] const std::vector<ColourValue>& colour_values(const std::filesystem::path& path) override;
    /// Asked of the service whose colour values hold one at `span`.
    [[nodiscard]] Asked<ColourForms> colour_forms(const std::filesystem::path& path, Span span,
                                                  const std::array<float, 4>& rgba) override;
    /// What any of the file's services names, each once, in order.
    [[nodiscard]] std::vector<std::string> completion_triggers(const std::filesystem::path& path) const override;
    [[nodiscard]] Asked<Completions> complete(const std::filesystem::path& path, Position at,
                                              const CompletionTrigger& trigger) override;
    [[nodiscard]] Asked<std::optional<Hover>> hover(const std::filesystem::path& path, Position at) override;
    [[nodiscard]] Asked<std::vector<Location>> definition(const std::filesystem::path& path, Position at) override;
    [[nodiscard]] Asked<std::vector<Span>> occurrences(const std::filesystem::path& path, Position at) override;
    [[nodiscard]] Asked<ftk::Result<WorkspaceEdit>> rename(const std::filesystem::path& path, Position at,
                                                           std::string_view name) override;
    [[nodiscard]] Asked<std::optional<VersionedEdit>> format(const std::filesystem::path& path) override;

    /// Every file a service was told of and not yet of its close.
    [[nodiscard]] std::set<std::filesystem::path> files() const;

    /// How each running service that answers for the file at `path` is, in
    /// order.
    [[nodiscard]] std::vector<ServiceShown> shown(const std::filesystem::path& path) const;

    /// How the service `name` is; nothing while it isn't running.
    [[nodiscard]] std::optional<ServiceStatus> status_of(std::string_view name) const;

    /// Ends the service `name`, when it runs, and starts it again, told of
    /// every file it was told of. What was asked of it before is answered
    /// with nothing.
    void restart(std::string_view name);

private:
    struct Running {
        ServiceMaker maker;
        std::unique_ptr<LanguageService> service;
        std::set<std::filesystem::path> told;
        // Moves when the service starts again: what the one before was
        // asked will never be answered.
        uint64_t generation{0};
    };

    [[nodiscard]] bool answers_for(const Running& running, const std::filesystem::path& path) const;
    // Makes the service, and tells it of every file it was told of before.
    void start(Running& running);
    // The running services told of the file at `path`, in order.
    [[nodiscard]] std::vector<Running*> told_of(const std::filesystem::path& path);

    // Asks each of `services` with `ask`; puts their answers together with
    // `fold`, in order, once all have answered. A lone service's answer is
    // its own. `none` is the answer when there is no service to ask, or none
    // of those asked could answer.
    template <typename T>
    Asked<T> ask_all(std::vector<Running*> services, const std::function<Asked<T>(LanguageService&)>& ask,
                     std::function<T(std::vector<T> answers)> fold, T none);

    std::vector<Running> services_;
    std::filesystem::path root_;
    // What each open file's document is, to tell a service started again.
    std::map<std::filesystem::path, const Document*> documents_;
    // Each file's problems as all of its services have them.
    std::map<std::filesystem::path, std::vector<Diagnostic>> problems_;
    // The questions asked of several services: each puts their answers
    // together once it can, and is done then or once given up.
    std::vector<std::function<bool()>> merging_;
};

} // namespace fjedit
