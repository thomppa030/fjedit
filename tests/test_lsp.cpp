#include "fjedit/lsp/connection.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace fjedit::lsp;
using ftk::jsonrpc::Answer;
using ftk::jsonrpc::Incoming;
using ftk::jsonrpc::Json;

namespace {

// Polls `connection` until `done` or five seconds have gone, handing the
// server's own messages to `heard`.
void poll_until(Connection& connection, const std::function<bool()>& done,
                const std::function<bool(const Incoming&)>& heard = {}) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done() && std::chrono::steady_clock::now() < until) {
        connection.peer().poll(heard);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// The fake server (fake_server.cpp), in the test's folder.
std::unique_ptr<Connection> fake(ftk::jsonrpc::PeerOptions options = {}) {
    const std::vector<std::string> args{FJEDIT_FAKE_SERVER};
    auto connection = Connection::start(args, std::filesystem::current_path(), std::move(options));
    REQUIRE(connection.has_value());
    return std::move(*connection);
}

} // namespace

TEST_CASE("A request is answered through its callback on the caller's thread", "[lsp]") {
    std::atomic<int> arrived{0};
    auto server = fake({.arrived = [&] { ++arrived; }});
    std::optional<Answer> answer;
    const std::thread::id caller = std::this_thread::get_id();
    std::thread::id answered_on;
    server->peer().request("echo", {{"x", 3}}, [&](Answer got) {
        answer = std::move(got);
        answered_on = std::this_thread::get_id();
    });
    poll_until(*server, [&] { return answer.has_value(); });
    REQUIRE(answer.has_value());
    REQUIRE(answer->has_value());
    REQUIRE((**answer)["x"] == 3);
    REQUIRE(answered_on == caller);
    REQUIRE(arrived > 0);
}

TEST_CASE("An error the server answers with comes back with its code", "[lsp]") {
    auto server = fake();
    std::optional<Answer> answer;
    server->peer().request("fail", nullptr, [&](Answer got) { answer = std::move(got); });
    poll_until(*server, [&] { return answer.has_value(); });
    REQUIRE(answer.has_value());
    REQUIRE_FALSE(answer->has_value());
    REQUIRE(answer->error().code == 7);
    REQUIRE(answer->error().message == "failed");
}

TEST_CASE("The server's notifications and requests reach the handler, and its requests are answered", "[lsp]") {
    auto server = fake();
    std::vector<std::string> heard;
    std::optional<Json> what_heard;
    const auto handler = [&](const Incoming& message) {
        heard.push_back(message.method);
        if (message.method == "heard") what_heard = message.params;
        if (message.method == "client/question" && message.params == "answer me") {
            server->peer().answer(*message.id, "an answer");
            return true;
        }
        return message.method != "client/question";
    };
    server->peer().request("tell", "news", {});
    server->peer().request("ask", "answer me", {});
    poll_until(*server, [&] { return what_heard.has_value(); }, handler);
    REQUIRE(heard == std::vector<std::string>{"told", "client/question", "heard"});
    REQUIRE(*what_heard == "an answer");

    // A request the handler leaves is answered as unknown.
    what_heard.reset();
    server->peer().request("ask", "leave me", {});
    poll_until(*server, [&] { return what_heard.has_value(); }, handler);
    REQUIRE(what_heard.has_value());
    REQUIRE((*what_heard)["code"] == ftk::jsonrpc::METHOD_NOT_FOUND);
}

TEST_CASE("A request cancelled is never answered", "[lsp]") {
    auto server = fake();
    bool answered = false;
    const int id = server->peer().request("slow", nullptr, [&](Answer) { answered = true; });
    server->peer().cancel(id);
    // The server answers the cancelled request; an echo sent after it comes
    // back after that answer.
    bool echoed = false;
    server->peer().request("echo", nullptr, [&](Answer) { echoed = true; });
    poll_until(*server, [&] { return echoed; });
    REQUIRE(echoed);
    REQUIRE_FALSE(answered);
}

TEST_CASE("A server that stops is no longer running, and its errors say why", "[lsp]") {
    auto server = fake();
    REQUIRE(server->peer().open());
    server->peer().notify("die", nullptr);
    // Its output and its errors end on threads of their own.
    poll_until(*server, [&] { return !server->peer().open() && !server->errors().empty(); });
    REQUIRE_FALSE(server->peer().open());
    REQUIRE(server->errors() == "dying");
}

TEST_CASE("A program that isn't there is no server", "[lsp]") {
    const std::vector<std::string> args{"fjedit-no-such-server-anywhere"};
    REQUIRE_FALSE(Connection::start(args, std::filesystem::current_path()).has_value());
}

TEST_CASE("clangd answers initialize with what it can do", "[lsp][clangd]") {
    const std::vector<std::string> args{"clangd", "--log=error"};
    auto started = Connection::start(args, std::filesystem::current_path());
    if (!started) SKIP("clangd isn't installed: " << started.error());
    Connection& server = **started;
    std::optional<Answer> answer;
    server.peer().request("initialize",
                          {{"processId", nullptr}, {"rootUri", nullptr}, {"capabilities", Json::object()}},
                          [&](Answer got) { answer = std::move(got); });
    poll_until(server, [&] { return answer.has_value(); });
    REQUIRE(answer.has_value());
    REQUIRE(answer->has_value());
    REQUIRE((**answer).contains("capabilities"));
}
