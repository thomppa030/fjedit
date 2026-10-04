#pragma once

// A language server, run as a child process and talked to in JSON-RPC 2.0
// over its standard streams, each message framed as the protocol frames
// them (frames.hpp). Writing and reading each have a thread of their own,
// so nothing the caller does waits on the server. What comes back is
// handed out on the caller's thread by poll(): each answer to the callback
// of the request it answers, and the server's own notifications and
// requests to a handler.
//
//     auto server = lsp::Connection::start(args, folder, {.arrived = [] { ftk::Window::wake(); }});
//     (*server)->request("initialize", params, [](lsp::Answer answer) { ... });
//     // each frame:
//     (*server)->poll([](const lsp::ServerMessage& message) { ...; return true; });

#include "ftk/base/result.hpp"
#include "ftk/platform/child_process.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fjedit::lsp {

using Json = nlohmann::json;

/// The error a server answered a request with.
struct ResponseError {
    int code{0};
    std::string message;
};

/// JSON-RPC's and the protocol's error codes this side knows.
inline constexpr int METHOD_NOT_FOUND = -32601;
/// The server gave up on the request because it was asked to.
inline constexpr int REQUEST_CANCELLED = -32800;
/// The text changed while the server worked: the answer would be stale.
inline constexpr int CONTENT_MODIFIED = -32801;

/// What a request comes back with: its result, or the server's error.
using Answer = std::expected<Json, ResponseError>;
using Answered = std::function<void(Answer answer)>;

/// What the server says of its own accord: a notification, or a request it
/// waits for an answer to.
struct ServerMessage {
    std::string method;
    Json params;
    /// Set on a request: what its answer names.
    std::optional<Json> id;
};

struct ConnectionOptions {
    /// Called on the reading thread whenever something has come for poll()
    /// to hand out, and when the server stops talking: to wake the loop
    /// that calls poll().
    std::function<void()> arrived{};
};

class Connection {
public:
    /// Starts the server `args` (the program, then its arguments) in
    /// `folder`.
    /// @return the connection, or why the server couldn't be run
    [[nodiscard]] static ftk::Result<std::unique_ptr<Connection>> start(std::span<const std::string> args,
                                                                        const std::filesystem::path& folder,
                                                                        ConnectionOptions options = {});

    /// Ends the server as shut_down() does.
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) = delete;
    Connection& operator=(Connection&&) = delete;

    /// Sends the request `method` with `params` (none when null);
    /// `answered`, when given, is called by poll() with its answer.
    /// @return its id, to cancel it by
    int request(std::string_view method, const Json& params, Answered answered);

    /// Sends the notification `method` with `params` (none when null).
    void notify(std::string_view method, const Json& params);

    /// Asks the server to give up on the request `id`; whatever it answers
    /// is never handed out.
    void cancel(int id);

    /// Answers the server's request `id` with `result`, or with an error.
    void answer(const Json& id, const Json& result);
    void answer_error(const Json& id, int code, std::string_view message);

    /// Hands out what has come since the last call, in the order it came:
    /// answers to their requests' callbacks, and the server's notifications
    /// and requests to `heard`. A request `heard` doesn't answer (returning
    /// false) is answered as a method this side doesn't know.
    void poll(const std::function<bool(const ServerMessage&)>& heard);

    /// Whether the server still talks: false once its output has ended.
    [[nodiscard]] bool running() const;

    /// Asks the server to end as the protocol has it (shutdown, then exit),
    /// waits up to `patience` for it, and ends it then if it hasn't. The
    /// connection says nothing more after.
    void shut_down(std::chrono::milliseconds patience = std::chrono::milliseconds(500));

    /// The last of what the server wrote to its standard error, which says
    /// why when it stops.
    [[nodiscard]] std::string errors() const;

private:
    Connection(std::unique_ptr<ftk::platform::ChildProcess> child, ConnectionOptions options);

    void send(const Json& message);
    void write_all();
    void read_all();
    void read_errors();

    std::unique_ptr<ftk::platform::ChildProcess> child_;
    ConnectionOptions options_;

    // Between the caller's thread and the connection's own.
    mutable std::mutex lock_;
    std::condition_variable more_to_write_;
    std::deque<std::string> outgoing_;
    bool stopping_{false};
    std::vector<Json> incoming_;
    bool ended_{false};
    std::string errors_;

    // The caller's alone.
    int next_id_{1};
    std::map<int, Answered> waiting_;
    bool shut_{false};

    std::thread writer_;
    std::thread reader_;
    std::thread error_reader_;
};

} // namespace fjedit::lsp
