#pragma once

// A language server, run as a child process and talked to over its
// standard streams: the conversation is an ftk::jsonrpc::Peer, and the
// connection keeps what the server writes to its standard error, which
// says why when it stops.
//
//     auto server = lsp::Connection::start(args, folder, {.arrived = [] { ftk::Window::wake(); }});
//     (*server)->peer().request("initialize", params, [](ftk::jsonrpc::Answer answer) { ... });
//     // each frame:
//     (*server)->peer().poll([](const ftk::jsonrpc::Incoming& message) { ...; return true; });

#include "ftk/base/result.hpp"
#include "ftk/jsonrpc/peer.hpp"
#include "ftk/platform/child_process.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>

namespace fjedit::lsp {

class Connection {
public:
    /// Starts the server `args` (the program, then its arguments) in
    /// `folder`.
    /// @return the connection, or why the server couldn't be run
    [[nodiscard]] static ftk::Result<std::unique_ptr<Connection>> start(std::span<const std::string> args,
                                                                        const std::filesystem::path& folder,
                                                                        ftk::jsonrpc::PeerOptions options = {});

    /// Asks the server to end as the protocol has it (shutdown, then exit),
    /// gives it a moment to, and ends it then if it hasn't.
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;

    /// The conversation with the server; open() says whether it still
    /// talks.
    [[nodiscard]] ftk::jsonrpc::Peer& peer() { return peer_; }

    /// The last of what the server wrote to its standard error, which says
    /// why when it stops.
    [[nodiscard]] std::string errors() const;

private:
    Connection(std::unique_ptr<ftk::platform::ChildProcess> child, ftk::jsonrpc::PeerOptions options);

    void read_errors();

    std::unique_ptr<ftk::platform::ChildProcess> child_;
    // Reads and writes the child's streams, so it goes before the child.
    ftk::jsonrpc::Peer peer_;

    mutable std::mutex lock_;
    std::string errors_;
    std::thread error_reader_;
};

} // namespace fjedit::lsp
