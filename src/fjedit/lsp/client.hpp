#pragma once

// A language served by a language server: the LanguageService the editor
// asks, answered by a server it starts in the project's folder, such as
// clangd for C++. The server is told what is open and every change, one
// edit at a time, and asked what the editor asks; what it says comes back
// through update() once a frame, so nothing the editor does waits on it.
//
//     lsp::Client clangd({.command = {"clangd"}, .root = folder, .extensions = {"cpp", "hpp"},
//                         .language_id = [](const auto&) { return "cpp"; }, .arrived = wake});
//     clangd.open(path, document);
//     // each frame:
//     clangd.update();

#include "fjedit/core/language_service.hpp"
#include "fjedit/lsp/connection.hpp"
#include "fjedit/lsp/convert.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fjedit::lsp {

struct ClientOptions {
    /// The server's program, then its arguments.
    std::vector<std::string> command;
    /// The project's folder: where the server runs, and the workspace it
    /// is told of.
    std::filesystem::path root;
    /// The extensions of the files it answers for, lower case ("cpp").
    std::vector<std::string> extensions;
    /// What the protocol calls the language of the file at a path ("cpp").
    std::function<std::string(const std::filesystem::path& path)> language_id;
    /// Called on another thread when the server has said something: to
    /// wake the frame loop that calls update().
    std::function<void()> arrived;
};

class Client final : public LanguageService {
public:
    /// Starts the server. One that can't be started leaves the client
    /// stopped, its status saying why.
    explicit Client(ClientOptions options);
    ~Client() override;

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;

    [[nodiscard]] bool serves(const std::filesystem::path& path) const override;
    void open(const std::filesystem::path& path, const Document& document) override;
    void close(const std::filesystem::path& path) override;
    void saved(const std::filesystem::path& path) override;
    void update() override;
    [[nodiscard]] ServiceStatus status() const override { return status_; }
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics(const std::filesystem::path& path) override;
    [[nodiscard]] const SemanticTokens* tokens(const std::filesystem::path& path) override;
    [[nodiscard]] Asked<Completions> complete(const std::filesystem::path& path, Position at) override;
    [[nodiscard]] Asked<std::optional<Hover>> hover(const std::filesystem::path& path, Position at) override;
    [[nodiscard]] Asked<std::vector<Location>> definition(const std::filesystem::path& path, Position at) override;
    [[nodiscard]] Asked<ftk::Result<WorkspaceEdit>> rename(const std::filesystem::path& path, Position at,
                                                           std::string_view name) override;
    [[nodiscard]] Asked<std::optional<VersionedEdit>> format(const std::filesystem::path& path) override;

private:
    // A document the server is told of, and what it said about it.
    struct Open {
        const Document* document{nullptr};
        std::string uri;
        // The version the server has; told of at all.
        uint64_t sent{0};
        bool told{false};
        // The problems as published, for the version they were found in,
        // and as carried to the document's version now.
        std::vector<Diagnostic> published;
        uint64_t published_version{0};
        std::vector<Diagnostic> carried;
        std::optional<uint64_t> carried_version;
        // The colours, for the version they were found for; the version
        // they were last asked for, and whether that question is under
        // way; and the colours carried to the document's version now.
        std::optional<SemanticTokens> colours;
        uint64_t colours_version{0};
        std::optional<uint64_t> colours_for;
        bool colours_asked{false};
        SemanticTokens carried_colours;
        std::optional<uint64_t> carried_colours_version;
    };

    // A question under way: its id, and what asked, held weakly so letting
    // it go gives the question up.
    struct Watched {
        int id{0};
        std::weak_ptr<const void> pending;
    };

    // A question asked while the server starts: sent once it is ready, or
    // answered with why not if it stops first.
    struct Waiting {
        std::function<void()> send;
        std::function<void(const std::string& why)> fail;
    };

    void initialized(const ftk::jsonrpc::Answer& answer);
    bool heard(const ftk::jsonrpc::Incoming& message);
    // Tells the server what changed in `file` since it last heard.
    void sync(const std::filesystem::path& path, Open& file);
    void ask_colours(const std::filesystem::path& path, Open& file);
    [[nodiscard]] Positions positions_for(const std::filesystem::path& path) const;
    // The protocol's document and position: where a question is about.
    [[nodiscard]] Json document_at(const std::filesystem::path& path, const std::string& uri, Position at) const;
    // Answers every question waiting on the server with `why` it won't be.
    void fail_waiting(const std::string& why);
    [[nodiscard]] bool ready() const { return status_.state == ServiceState::Ready && connection_ != nullptr; }

    // Asks `method` with what `params` makes when it is sent, the answer
    // read by `read` into a Pending: at once when the server is ready, once
    // it is while it starts, and answered at once with why not when it has
    // stopped.
    template <typename T>
    std::shared_ptr<Pending<T>> ask(std::string_view method, std::function<Json()> params,
                                    std::function<T(const ftk::jsonrpc::Answer&)> read);

    ClientOptions options_;
    std::unique_ptr<Connection> connection_;
    ServiceStatus status_;
    // How the server counts along a line, how it takes changes (whole, or
    // by the edit), whether it wants saves, and its colours' legend.
    Encoding encoding_{Encoding::Utf16};
    bool incremental_{false};
    bool wants_saves_{false};
    std::vector<TokenKind> colour_kinds_;
    std::map<std::filesystem::path, Open> open_;
    std::vector<Watched> watched_;
    std::vector<Waiting> waiting_;
};

} // namespace fjedit::lsp
