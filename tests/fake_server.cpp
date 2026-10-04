// A language server for the connection's tests (test_lsp.cpp): it speaks
// the protocol's framing and answers what the tests send it.
//
//     echo            answers with its params
//     fail            answers with an error, code 7
//     tell            notifies "told" with its params, then answers null
//     ask             asks this side "client/question" with its params,
//                     then notifies "heard" with what this side answered
//     slow            answers only once cancelled, as cancelled
//     die             writes "dying" to its errors and ends with code 5
//     shutdown        answers null; the notification exit ends it

#include "ftk/jsonrpc/frames.hpp"
#include "ftk/platform/platform.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <string>

using Json = nlohmann::json;

namespace {

void send(const Json& message) {
    (void)ftk::platform::write_standard_output(ftk::jsonrpc::framed(message.dump()));
}

void answer(const Json& id, const Json& result) {
    send({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}

} // namespace

int main() {
    ftk::jsonrpc::FrameReader frames;
    Json slow_id;
    std::array<char, 4096> buffer{};
    for (;;) {
        const auto got = ftk::platform::read_standard_input(buffer);
        if (!got || *got == 0) return 0;
        frames.add(std::string_view(buffer.data(), *got));
        while (const auto body = frames.next()) {
            const Json message = Json::parse(*body);
            const std::string method = message.value("method", std::string());
            const Json params = message.value("params", Json());
            if (method.empty()) {
                // This side's answer to "ask".
                send({{"jsonrpc", "2.0"},
                      {"method", "heard"},
                      {"params", message.contains("error") ? message["error"] : message["result"]}});
            } else if (method == "echo") {
                answer(message["id"], params);
            } else if (method == "fail") {
                send({{"jsonrpc", "2.0"}, {"id", message["id"]}, {"error", {{"code", 7}, {"message", "failed"}}}});
            } else if (method == "tell") {
                send({{"jsonrpc", "2.0"}, {"method", "told"}, {"params", params}});
                answer(message["id"], nullptr);
            } else if (method == "ask") {
                send({{"jsonrpc", "2.0"}, {"id", "q1"}, {"method", "client/question"}, {"params", params}});
                answer(message["id"], nullptr);
            } else if (method == "slow") {
                slow_id = message["id"];
            } else if (method == "$/cancelRequest" && params.value("id", Json()) == slow_id) {
                send({{"jsonrpc", "2.0"}, {"id", slow_id}, {"error", {{"code", -32800}, {"message", "cancelled"}}}});
            } else if (method == "die") {
                std::fputs("dying", stderr);
                std::fflush(stderr);
                return 5;
            } else if (method == "shutdown") {
                answer(message["id"], nullptr);
            } else if (method == "exit") {
                return 0;
            }
        }
    }
}
