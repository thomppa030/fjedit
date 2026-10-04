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

#include "fjedit/lsp/frames.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
// The protocol's \r\n must pass as it is, which a Windows program's
// standard streams only let it in binary mode.
#include <fcntl.h>
#include <io.h>
#endif

using Json = nlohmann::json;

namespace {

void send(const Json& message) {
    const std::string bytes = fjedit::lsp::framed(message.dump());
    std::fwrite(bytes.data(), 1, bytes.size(), stdout);
    std::fflush(stdout);
}

void answer(const Json& id, const Json& result) {
    send({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}

} // namespace

int main() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    fjedit::lsp::FrameReader frames;
    Json slow_id;
    // A byte at a time: a read of more waits until all of it has come.
    for (int c = std::getchar(); c != EOF; c = std::getchar()) {
        const char byte = static_cast<char>(c);
        frames.add(std::string_view(&byte, 1));
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
    return 0;
}
