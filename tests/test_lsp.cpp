#include "fjedit/lsp/connection.hpp"
#include "fjedit/lsp/frames.hpp"

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

namespace {

// Polls `connection` until `done` or five seconds have gone, handing the
// server's own messages to `heard`.
void poll_until(Connection& connection, const std::function<bool()>& done,
                const std::function<bool(const ServerMessage&)>& heard = {}) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done() && std::chrono::steady_clock::now() < until) {
        connection.poll(heard);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// The fake server (fake_server.cpp), in the test's folder.
std::unique_ptr<Connection> fake(ConnectionOptions options = {}) {
    const std::vector<std::string> args{FJEDIT_FAKE_SERVER};
    auto connection = Connection::start(args, std::filesystem::current_path(), std::move(options));
    REQUIRE(connection.has_value());
    return std::move(*connection);
}

} // namespace

TEST_CASE("A message is framed by its length", "[lsp]") {
    REQUIRE(framed("{}") == "Content-Length: 2\r\n\r\n{}");
}

TEST_CASE("Frames are read back whole, however the bytes come", "[lsp]") {
    const std::string bytes = framed(R"({"a":1})") + framed(R"({"b":"two"})");
    SECTION("a byte at a time") {
        FrameReader frames;
        std::vector<std::string> read;
        for (const char c : bytes) {
            frames.add(std::string_view(&c, 1));
            while (auto body = frames.next()) read.push_back(*body);
        }
        REQUIRE(read == std::vector<std::string>{R"({"a":1})", R"({"b":"two"})"});
    }
    SECTION("all at once") {
        FrameReader frames;
        frames.add(bytes);
        REQUIRE(frames.next() == R"({"a":1})");
        REQUIRE(frames.next() == R"({"b":"two"})");
        REQUIRE_FALSE(frames.next().has_value());
    }
}

TEST_CASE("A header's other lines, its case and one with no length are read as the protocol has them", "[lsp]") {
    FrameReader frames;
    frames.add("content-length: 2\r\nContent-Type: application/vscode-jsonrpc; charset=utf-8\r\n\r\n{}");
    frames.add("X-Nothing: 1\r\n\r\n");
    frames.add(framed("[]"));
    REQUIRE(frames.next() == "{}");
    REQUIRE(frames.next() == "[]");
}

TEST_CASE("A request is answered through its callback on the caller's thread", "[lsp]") {
    std::atomic<int> arrived{0};
    auto server = fake({.arrived = [&] { ++arrived; }});
    std::optional<Answer> answer;
    const std::thread::id caller = std::this_thread::get_id();
    std::thread::id answered_on;
    server->request("echo", {{"x", 3}}, [&](Answer got) {
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
    server->request("fail", nullptr, [&](Answer got) { answer = std::move(got); });
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
    const auto handler = [&](const ServerMessage& message) {
        heard.push_back(message.method);
        if (message.method == "heard") what_heard = message.params;
        if (message.method == "client/question" && message.params == "answer me") {
            server->answer(*message.id, "an answer");
            return true;
        }
        return message.method != "client/question";
    };
    server->request("tell", "news", {});
    server->request("ask", "answer me", {});
    poll_until(*server, [&] { return what_heard.has_value(); }, handler);
    REQUIRE(heard == std::vector<std::string>{"told", "client/question", "heard"});
    REQUIRE(*what_heard == "an answer");

    // A request the handler leaves is answered as unknown.
    what_heard.reset();
    server->request("ask", "leave me", {});
    poll_until(*server, [&] { return what_heard.has_value(); }, handler);
    REQUIRE(what_heard.has_value());
    REQUIRE((*what_heard)["code"] == METHOD_NOT_FOUND);
}

TEST_CASE("A request cancelled is never answered", "[lsp]") {
    auto server = fake();
    bool answered = false;
    const int id = server->request("slow", nullptr, [&](Answer) { answered = true; });
    server->cancel(id);
    // The server answers the cancelled request; an echo sent after it comes
    // back after that answer.
    bool echoed = false;
    server->request("echo", nullptr, [&](Answer) { echoed = true; });
    poll_until(*server, [&] { return echoed; });
    REQUIRE(echoed);
    REQUIRE_FALSE(answered);
}

TEST_CASE("A server that stops is no longer running, and its errors say why", "[lsp]") {
    auto server = fake();
    REQUIRE(server->running());
    server->notify("die", nullptr);
    // Its output and its errors end on threads of their own.
    poll_until(*server, [&] { return !server->running() && !server->errors().empty(); });
    REQUIRE_FALSE(server->running());
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
    server.request("initialize",
                   {{"processId", nullptr},
                    {"rootUri", nullptr},
                    {"capabilities", Json::object()}},
                   [&](Answer got) { answer = std::move(got); });
    poll_until(server, [&] { return answer.has_value(); });
    REQUIRE(answer.has_value());
    REQUIRE(answer->has_value());
    REQUIRE((**answer).contains("capabilities"));
}
