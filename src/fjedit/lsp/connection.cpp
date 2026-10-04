#include "fjedit/lsp/connection.hpp"

#include "fjedit/lsp/frames.hpp"

#include <array>
#include <utility>

namespace fjedit::lsp {

namespace {

// How much of the server's standard error is kept: its last words.
constexpr std::size_t ERRORS_KEPT = 8 * 1024;

} // namespace

ftk::Result<std::unique_ptr<Connection>> Connection::start(std::span<const std::string> args,
                                                           const std::filesystem::path& folder,
                                                           ConnectionOptions options) {
    auto child = ftk::platform::ChildProcess::start(
        args, {.folder = folder, .errors = ftk::platform::ChildErrors::Kept});
    if (!child) return ftk::make_error(child.error());
    return std::unique_ptr<Connection>(new Connection(std::move(*child), std::move(options)));
}

Connection::Connection(std::unique_ptr<ftk::platform::ChildProcess> child, ConnectionOptions options)
    : child_(std::move(child)), options_(std::move(options)) {
    writer_ = std::thread([this] { write_all(); });
    reader_ = std::thread([this] { read_all(); });
    error_reader_ = std::thread([this] { read_errors(); });
}

Connection::~Connection() {
    shut_down();
}

void Connection::send(const Json& message) {
    if (shut_) return;
    // Text that isn't valid UTF-8 goes as the replacement character rather
    // than stopping the message.
    std::string body = message.dump(-1, ' ', false, Json::error_handler_t::replace);
    {
        const std::lock_guard held(lock_);
        outgoing_.push_back(framed(body));
    }
    more_to_write_.notify_one();
}

int Connection::request(std::string_view method, const Json& params, Answered answered) {
    const int id = next_id_++;
    Json message{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
    if (!params.is_null()) message["params"] = params;
    waiting_[id] = std::move(answered);
    send(message);
    return id;
}

void Connection::notify(std::string_view method, const Json& params) {
    Json message{{"jsonrpc", "2.0"}, {"method", method}};
    if (!params.is_null()) message["params"] = params;
    send(message);
}

void Connection::cancel(int id) {
    if (waiting_.erase(id) == 0) return;
    notify("$/cancelRequest", {{"id", id}});
}

void Connection::answer(const Json& id, const Json& result) {
    send({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}

void Connection::answer_error(const Json& id, int code, std::string_view message) {
    send({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}});
}

void Connection::poll(const std::function<bool(const ServerMessage&)>& heard) {
    std::vector<Json> came;
    {
        const std::lock_guard held(lock_);
        came.swap(incoming_);
    }
    for (Json& message : came) {
        const auto method = message.find("method");
        const auto id = message.find("id");
        if (method == message.end()) {
            // An answer, to a request still waited on.
            if (id == message.end() || !id->is_number_integer()) continue;
            const auto waiting = waiting_.find(id->get<int>());
            if (waiting == waiting_.end()) continue;
            const Answered answered = std::move(waiting->second);
            waiting_.erase(waiting);
            if (!answered) continue;
            if (const auto error = message.find("error"); error != message.end() && error->is_object()) {
                answered(std::unexpected(ResponseError{.code = error->value("code", 0),
                                                       .message = error->value("message", std::string())}));
            } else {
                answered(message.value("result", Json()));
            }
            continue;
        }
        if (!method->is_string()) continue;
        ServerMessage said{.method = method->get<std::string>(),
                           .params = message.value("params", Json()),
                           .id = id != message.end() ? std::optional<Json>(*id) : std::nullopt};
        const bool handled = heard && heard(said);
        if (said.id && !handled) answer_error(*said.id, METHOD_NOT_FOUND, "fjedit doesn't answer " + said.method);
    }
}

bool Connection::running() const {
    const std::lock_guard held(lock_);
    return !ended_;
}

void Connection::shut_down(std::chrono::milliseconds patience) {
    if (shut_) return;
    // Exit follows shutdown at once: what shutdown answers changes nothing.
    request("shutdown", nullptr, {});
    notify("exit", nullptr);
    shut_ = true;
    {
        const std::lock_guard held(lock_);
        stopping_ = true;
    }
    more_to_write_.notify_one();

    const auto until = std::chrono::steady_clock::now() + patience;
    while (!child_->exit_code() && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // A server that didn't go is ended, which ends the threads' reads and
    // writes too.
    child_->kill();
    for (std::thread* thread : {&writer_, &reader_, &error_reader_}) {
        if (thread->joinable()) thread->join();
    }
    waiting_.clear();
}

std::string Connection::errors() const {
    const std::lock_guard held(lock_);
    return errors_;
}

void Connection::write_all() {
    std::unique_lock held(lock_);
    for (;;) {
        more_to_write_.wait(held, [this] { return stopping_ || !outgoing_.empty(); });
        if (outgoing_.empty()) return;
        const std::string message = std::move(outgoing_.front());
        outgoing_.pop_front();
        held.unlock();
        const bool written = child_->write(message).has_value();
        held.lock();
        // The server is gone: nothing more can reach it.
        if (!written) {
            outgoing_.clear();
            return;
        }
    }
}

void Connection::read_all() {
    FrameReader frames;
    std::array<char, 16 * 1024> buffer{};
    for (;;) {
        const ftk::Result<std::size_t> got = child_->read(buffer);
        if (!got || *got == 0) break;
        frames.add(std::string_view(buffer.data(), *got));
        bool came = false;
        while (const std::optional<std::string> body = frames.next()) {
            Json message = Json::parse(*body, nullptr, false);
            if (message.is_discarded() || !message.is_object()) continue;
            const std::lock_guard held(lock_);
            incoming_.push_back(std::move(message));
            came = true;
        }
        if (came && options_.arrived) options_.arrived();
    }
    {
        const std::lock_guard held(lock_);
        ended_ = true;
    }
    if (options_.arrived) options_.arrived();
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
