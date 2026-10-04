#include "fjedit/lsp/connection.hpp"

#include <array>
#include <chrono>
#include <utility>

namespace fjedit::lsp {

namespace {

// How much of the server's standard error is kept: its last words.
constexpr std::size_t ERRORS_KEPT = 8 * 1024;

// How long a server is given to end once asked to.
constexpr std::chrono::milliseconds SHUTDOWN_PATIENCE{500};

} // namespace

ftk::Result<std::unique_ptr<Connection>> Connection::start(std::span<const std::string> args,
                                                           const std::filesystem::path& folder,
                                                           ftk::jsonrpc::PeerOptions options) {
    auto child = ftk::platform::ChildProcess::start(
        args, {.folder = folder, .errors = ftk::platform::ChildErrors::Kept});
    if (!child) return ftk::make_error(child.error());
    return std::unique_ptr<Connection>(new Connection(std::move(*child), std::move(options)));
}

Connection::Connection(std::unique_ptr<ftk::platform::ChildProcess> child, ftk::jsonrpc::PeerOptions options)
    : child_(std::move(child)),
      peer_({.read = [this](std::span<char> into) { return child_->read(into); },
             .write = [this](std::string_view bytes) { return child_->write(bytes); }},
            std::move(options)) {
    error_reader_ = std::thread([this] { read_errors(); });
}

Connection::~Connection() {
    // Exit follows shutdown at once: what shutdown answers changes nothing.
    peer_.request("shutdown", nullptr, {});
    peer_.notify("exit", nullptr);
    peer_.stop();
    const auto until = std::chrono::steady_clock::now() + SHUTDOWN_PATIENCE;
    while (!child_->exit_code() && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // A server that didn't go is ended, which ends the reads and writes on
    // its streams: this thread's, and the peer's, joined as it goes.
    child_->kill();
    error_reader_.join();
}

std::string Connection::errors() const {
    const std::lock_guard held(lock_);
    return errors_;
}

void Connection::read_errors() {
    std::array<char, 4096> buffer{};
    for (;;) {
        const ftk::Result<std::size_t> got = child_->read_errors(buffer);
        if (!got || *got == 0) return;
        const std::lock_guard held(lock_);
        errors_.append(buffer.data(), *got);
        if (errors_.size() > ERRORS_KEPT) errors_.erase(0, errors_.size() - ERRORS_KEPT);
    }
}

} // namespace fjedit::lsp
