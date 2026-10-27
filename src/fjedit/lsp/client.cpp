#include "fjedit/lsp/client.hpp"

#include "fjedit/core/document.hpp"
#include "fjedit/core/text_file.hpp"

#include "ftk/platform/platform.hpp"

#include <algorithm>
#include <utility>

namespace fjedit::lsp {

namespace {

namespace fs = std::filesystem;
namespace jsonrpc = ftk::jsonrpc;

// The token types this side can colour, in the protocol's names and fjls's.
const Json TOKEN_TYPES = {"namespace", "type",     "class",    "enum",   "interface", "struct",  "typeParameter",
                          "parameter", "variable", "property", "enumMember", "function", "method", "macro",
                          "keyword",   "modifier", "comment",  "string", "number",    "operator", "bracket",
                          "concept",   "uniform",  "builtin",  "hint"};

// What fjedit tells a server it can do: positions in UTF-8 when the server
// counts that way too, no snippets in completions, why a completion was
// asked, documentation in Markdown, colour values, and the rest of what the
// editor asks.
Json capabilities() {
    return {
        {"general", {{"positionEncodings", {"utf-8", "utf-16"}}}},
        // clangd's name for the same, from before the protocol had one.
        {"offsetEncoding", {"utf-8", "utf-16"}},
        {"textDocument",
         {
             {"synchronization", {{"didSave", true}}},
             {"publishDiagnostics", {{"versionSupport", true}}},
             {"completion",
              {{"completionItem",
                {{"snippetSupport", false},
                 {"documentationFormat", {"markdown", "plaintext"}},
                 {"labelDetailsSupport", true}}},
               {"contextSupport", true}}},
             {"hover", {{"contentFormat", {"markdown", "plaintext"}}}},
             {"definition", {{"linkSupport", true}}},
             {"documentHighlight", Json::object()},
             {"rename", Json::object()},
             {"formatting", Json::object()},
             {"semanticTokens",
              {{"requests", {{"full", true}}},
               {"tokenTypes", TOKEN_TYPES},
               {"tokenModifiers", Json::array()},
               {"formats", {"relative"}}}},
             {"colorProvider", Json::object()},
         }},
        {"workspace", {{"workspaceEdit", {{"documentChanges", true}}}, {"workspaceFolders", true}}},
    };
}

// The whole text of `document` as the server should see it: lines as the
// editor has them, without the byte order mark.
std::string whole_text(const Document& document) {
    const Buffer& buffer = document.buffer();
    const std::size_t last = buffer.line_count() - 1;
    return buffer.text(Span{.from = {0, 0}, .to = {last, buffer.line(last).size()}});
}

// `span` carried through `edit`: it doesn't grow to take in text put in at
// its ends.
Span carried(const Edit& edit, Span span) {
    const Position from = map(edit, span.from, Bias::After);
    return {.from = from, .to = std::max(from, map(edit, span.to, Bias::Before))};
}

// That the server's program can't be found, as `program` names it: not on
// the PATH, or not where a path to it says; then what to do about it.
std::string missing(const std::string& program, const std::string& if_missing) {
    std::string note = program + (fs::path(program).has_parent_path() ? " doesn't exist" : " isn't on the PATH");
    if (!if_missing.empty()) note += "; " + if_missing;
    return note;
}

// The last line of `text` that says anything.
std::string last_line(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.remove_suffix(1);
    const std::size_t start = text.find_last_of('\n');
    return std::string(start == std::string_view::npos ? text : text.substr(start + 1));
}

} // namespace

Client::Client(ClientOptions options) : options_(std::move(options)) {
    const std::string name = options_.command.empty() ? std::string("the server")
                                                       : fs::path(options_.command.front()).stem().string();
    if (!options_.command.empty() && !ftk::platform::find_program(options_.command.front())) {
        status_ = {.state = ServiceState::Stopped, .note = missing(options_.command.front(), options_.if_missing)};
        return;
    }
    auto started = Connection::start(options_.command, options_.root, {.arrived = options_.arrived});
    if (!started) {
        status_ = {.state = ServiceState::Stopped, .note = started.error()};
        return;
    }
    connection_ = std::move(*started);
    status_ = {.state = ServiceState::Starting, .note = "Starting " + name};
    const std::string root = jsonrpc::uri_of(options_.root);
    const Json params = {
        {"processId", ftk::platform::process_id()},
        {"clientInfo", {{"name", "fjedit"}}},
        {"rootUri", root},
        {"workspaceFolders", Json::array({{{"uri", root}, {"name", options_.root.filename().string()}}})},
        {"capabilities", capabilities()},
    };
    connection_->peer().request("initialize", params, [this](jsonrpc::Answer answer) { initialized(answer); });
}

Client::~Client() = default;

void Client::initialized(const jsonrpc::Answer& answer) {
    if (!answer) {
        status_ = {.state = ServiceState::Stopped, .note = answer.error().message};
        fail_waiting(status_.note);
        return;
    }
    const Json capabilities = answer->value("capabilities", Json::object());
    const bool utf8 = capabilities.value("positionEncoding", std::string()) == "utf-8" ||
                      answer->value("offsetEncoding", std::string()) == "utf-8";
    encoding_ = utf8 ? jsonrpc::Encoding::Utf8 : jsonrpc::Encoding::Utf16;
    if (const auto sync = capabilities.find("textDocumentSync"); sync != capabilities.end()) {
        if (sync->is_number_integer()) {
            incremental_ = sync->get<int>() == 2;
        } else if (sync->is_object()) {
            incremental_ = sync->value("change", 0) == 2;
            const auto save = sync->find("save");
            wants_saves_ = save != sync->end() && (save->is_object() || (save->is_boolean() && save->get<bool>()));
        }
    }
    if (const auto provider = capabilities.find("semanticTokensProvider"); provider != capabilities.end() && provider->is_object()) {
        colour_kinds_ = token_kinds(provider->value("legend", Json::object()).value("tokenTypes", Json::array()));
    }
    if (const auto provider = capabilities.find("colorProvider"); provider != capabilities.end()) {
        finds_colour_values_ = provider->is_object() || (provider->is_boolean() && provider->get<bool>());
    }
    if (const auto provider = capabilities.find("completionProvider"); provider != capabilities.end() && provider->is_object()) {
        const auto triggers = provider->find("triggerCharacters");
        if (triggers != provider->end() && triggers->is_array()) {
            for (const Json& trigger : *triggers) {
                if (trigger.is_string() && !trigger.get<std::string>().empty()) triggers_.push_back(trigger.get<std::string>());
            }
        }
    }
    connection_->peer().notify("initialized", Json::object());
    status_ = {.state = ServiceState::Ready, .note = {}};
    // What is open first, so the questions asked meanwhile are about it.
    for (auto& [path, file] : open_) sync(path, file);
    for (const Waiting& waiting : std::exchange(waiting_, {})) waiting.send();
}

bool Client::serves(const fs::path& path) const {
    std::string extension = path.extension().string();
    if (extension.empty()) return false;
    extension.erase(0, 1);
    for (char& c : extension) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    return std::ranges::contains(options_.extensions, extension);
}

void Client::open(const fs::path& path, const Document& document) {
    Open& file = open_[path];
    file = Open{};
    file.document = &document;
    file.uri = jsonrpc::uri_of(path);
    if (ready()) sync(path, file);
}

void Client::close(const fs::path& path) {
    const auto found = open_.find(path);
    if (found == open_.end()) return;
    if (found->second.told && connection_) {
        connection_->peer().notify("textDocument/didClose", {{"textDocument", {{"uri", found->second.uri}}}});
    }
    open_.erase(found);
}

void Client::saved(const fs::path& path) {
    const auto found = open_.find(path);
    if (found == open_.end() || !ready() || !wants_saves_) return;
    sync(path, found->second);
    connection_->peer().notify("textDocument/didSave", {{"textDocument", {{"uri", found->second.uri}}}});
}

void Client::sync(const fs::path& path, Open& file) {
    const Document& document = *file.document;
    if (!file.told) {
        const std::string language = options_.language_id ? options_.language_id(path) : std::string();
        connection_->peer().notify("textDocument/didOpen", {{"textDocument",
                                                             {{"uri", file.uri},
                                                              {"languageId", language},
                                                              {"version", document.version()},
                                                              {"text", whole_text(document)}}}});
        file.told = true;
        file.sent = document.version();
        return;
    }
    if (document.version() == file.sent) return;
    // By the edit when the server takes changes that way and counts as the
    // editor does; the whole text otherwise, or when the edits made since
    // are no longer known.
    Json changes = Json::array();
    const bool by_edit = incremental_ && encoding_ == jsonrpc::Encoding::Utf8;
    const auto edits = by_edit ? document.edits_since(file.sent) : std::nullopt;
    if (edits) {
        for (const Edit& edit : *edits) {
            for (Json& change : content_changes(edit)) changes.push_back(std::move(change));
        }
    } else {
        changes.push_back({{"text", whole_text(document)}});
    }
    connection_->peer().notify("textDocument/didChange",
                               {{"textDocument", {{"uri", file.uri}, {"version", document.version()}}},
                                {"contentChanges", std::move(changes)}});
    file.sent = document.version();
}

void Client::ask_colours(const fs::path& path, Open& file) {
    const uint64_t version = file.document->version();
    if (colour_kinds_.empty() || file.colours_asked || file.colours_for == version) return;
    file.colours_asked = true;
    file.colours_for = version;
    const auto answered = [this, path, version](jsonrpc::Answer answer) {
        const auto found = open_.find(path);
        if (found == open_.end()) return;
        Open& asked = found->second;
        asked.colours_asked = false;
        // Colours for a text since changed are asked for again; the ones
        // before are carried meanwhile.
        if (!answer || !answer->is_object() || version != asked.document->version()) return;
        asked.colours = tokens_from(answer->value("data", Json::array()), colour_kinds_,
                                    asked.document->buffer().line_count(), positions_for(path));
        asked.colours_version = version;
        asked.carried_colours_version.reset();
    };
    connection_->peer().request("textDocument/semanticTokens/full", {{"textDocument", {{"uri", file.uri}}}},
                                answered);
}

void Client::ask_colour_values(const fs::path& path, Open& file) {
    const uint64_t version = file.document->version();
    if (!finds_colour_values_ || file.values_asked || file.values_for == version) return;
    file.values_asked = true;
    file.values_for = version;
    const auto answered = [this, path, version](jsonrpc::Answer answer) {
        const auto found = open_.find(path);
        if (found == open_.end()) return;
        Open& asked = found->second;
        asked.values_asked = false;
        // Values for a text since changed are asked for again; the ones
        // before are carried meanwhile.
        if (!answer || version != asked.document->version()) return;
        asked.values = colour_values_from(*answer, positions_for(path));
        asked.values_version = version;
        asked.carried_values_version.reset();
    };
    connection_->peer().request("textDocument/documentColor", {{"textDocument", {{"uri", file.uri}}}}, answered);
}

void Client::update() {
    if (!connection_) return;
    connection_->peer().poll([this](const jsonrpc::Incoming& message) { return heard(message); });
    if (!connection_->peer().open()) {
        if (status_.state != ServiceState::Stopped) {
            const std::string why = last_line(connection_->errors());
            status_ = {.state = ServiceState::Stopped,
                       .note = why.empty() ? fs::path(options_.command.front()).stem().string() + " stopped" : why};
            fail_waiting(status_.note);
        }
        return;
    }
    if (!ready()) return;
    for (auto& [path, file] : open_) {
        sync(path, file);
        ask_colours(path, file);
        ask_colour_values(path, file);
    }
    // Questions no one waits on any more are given up.
    std::erase_if(watched_, [this](const Watched& watched) {
        if (!watched.pending.expired()) return false;
        connection_->peer().cancel(watched.id);
        return true;
    });
}

bool Client::heard(const jsonrpc::Incoming& message) {
    if (message.method == "textDocument/publishDiagnostics") {
        const std::optional<fs::path> path = jsonrpc::path_of(message.params.value("uri", std::string()));
        const auto found = path ? open_.find(*path) : open_.end();
        if (found == open_.end()) return true;
        Open& file = found->second;
        file.published = diagnostics_from(message.params.value("diagnostics", Json::array()), positions_for(*path));
        const auto version = message.params.find("version");
        file.published_version = version != message.params.end() && version->is_number_integer()
                                     ? version->get<uint64_t>()
                                     : file.sent;
        file.carried_version.reset();
        return true;
    }
    if (message.method == "window/showMessage") {
        status_.note = message.params.value("message", std::string());
        return true;
    }
    if (!message.id) return true;
    // Requests answered with nothing to say: what it registers, and its
    // progress.
    if (message.method == "client/registerCapability" || message.method == "client/unregisterCapability" ||
        message.method == "window/workDoneProgress/create") {
        connection_->peer().answer(*message.id, nullptr);
        return true;
    }
    if (message.method == "workspace/configuration") {
        const Json items = message.params.value("items", Json::array());
        connection_->peer().answer(*message.id, Json(std::vector<Json>(items.size(), nullptr)));
        return true;
    }
    return false;
}

const std::vector<Diagnostic>& Client::diagnostics(const fs::path& path) {
    static const std::vector<Diagnostic> NONE;
    const auto found = open_.find(path);
    if (found == open_.end()) return NONE;
    Open& file = found->second;
    const Document& document = *file.document;
    if (file.carried_version == document.version()) return file.carried;
    file.carried = file.published;
    if (const auto edits = document.edits_since(file.published_version)) {
        for (const Edit& edit : *edits) {
            for (Diagnostic& diagnostic : file.carried) diagnostic.span = carried(edit, diagnostic.span);
        }
    } else {
        for (Diagnostic& diagnostic : file.carried) {
            diagnostic.span = {.from = document.buffer().clamp(diagnostic.span.from),
                               .to = document.buffer().clamp(diagnostic.span.to)};
        }
    }
    file.carried_version = document.version();
    return file.carried;
}

const SemanticTokens* Client::tokens(const fs::path& path) {
    const auto found = open_.find(path);
    if (found == open_.end() || !found->second.colours) return nullptr;
    Open& file = found->second;
    const Document& document = *file.document;
    if (file.colours_version == document.version()) return &*file.colours;
    if (file.carried_colours_version == document.version()) return &file.carried_colours;
    const auto edits = document.edits_since(file.colours_version);
    if (!edits) return nullptr;
    // Each token carried through the edits since; one an edit cut into or
    // broke across lines is dropped until the server's next colours.
    SemanticTokens carried_tokens;
    carried_tokens.lines.resize(document.buffer().line_count());
    for (std::size_t line = 0; line < file.colours->lines.size(); ++line) {
        for (const Token& token : file.colours->lines[line]) {
            Span span{.from = {line, token.begin}, .to = {line, token.end}};
            bool kept = true;
            for (const Edit& edit : *edits) {
                const Span moved = carried(edit, span);
                const bool touched = std::ranges::any_of(edit.changes, [&](const Change& change) {
                    return change.span.from < span.to && span.from < change.span.to;
                });
                if (touched || moved.from.line != moved.to.line) {
                    kept = false;
                    break;
                }
                span = moved;
            }
            if (kept && span.from.line < carried_tokens.lines.size() && span.from.byte < span.to.byte) {
                carried_tokens.lines[span.from.line].push_back(
                    {.begin = span.from.byte, .end = span.to.byte, .kind = token.kind});
            }
        }
    }
    file.carried_colours = std::move(carried_tokens);
    file.carried_colours_version = document.version();
    return &file.carried_colours;
}

const std::vector<ColourValue>& Client::colour_values(const fs::path& path) {
    static const std::vector<ColourValue> NONE;
    const auto found = open_.find(path);
    if (found == open_.end() || !found->second.values) return NONE;
    Open& file = found->second;
    const Document& document = *file.document;
    if (file.values_version == document.version()) return *file.values;
    if (file.carried_values_version == document.version()) return file.carried_values;
    // Each value carried through the edits since; one an edit cut into is
    // dropped until the server's next values. None while the edits are no
    // longer known.
    file.carried_values.clear();
    if (const auto edits = document.edits_since(file.values_version)) {
        for (ColourValue value : *file.values) {
            bool kept = true;
            for (const Edit& edit : *edits) {
                const bool touched = std::ranges::any_of(edit.changes, [&](const Change& change) {
                    return change.span.from < value.span.to && value.span.from < change.span.to;
                });
                if (touched) {
                    kept = false;
                    break;
                }
                value.span = carried(edit, value.span);
            }
            if (kept) file.carried_values.push_back(value);
        }
    }
    file.carried_values_version = document.version();
    return file.carried_values;
}

Positions Client::positions_for(const fs::path& path) const {
    if (encoding_ == jsonrpc::Encoding::Utf8) return Positions();
    if (const auto found = open_.find(path); found != open_.end()) {
        const Document* document = found->second.document;
        return Positions(jsonrpc::Encoding::Utf16, [document](std::size_t line) {
            const Buffer& buffer = document->buffer();
            return line < buffer.line_count() ? buffer.line(line) : std::string_view();
        });
    }
    // A file not open: its lines as they are on disk.
    auto text = std::make_shared<const Buffer>(read_text_file(path).value_or(std::string()));
    return Positions(jsonrpc::Encoding::Utf16, [text](std::size_t line) {
        return line < text->line_count() ? text->line(line) : std::string_view();
    });
}

Json Client::document_at(const fs::path& path, const std::string& uri, Position at) const {
    return {{"textDocument", {{"uri", uri}}}, {"position", positions_for(path).to_json(at)}};
}

template <typename T>
std::shared_ptr<Pending<T>> Client::ask(std::string_view method, std::function<Json()> params,
                                        std::function<T(const jsonrpc::Answer&)> read) {
    auto pending = std::make_shared<Pending<T>>();
    const std::weak_ptr<Pending<T>> weak = pending;
    const auto answered = [weak, read = std::move(read)](const jsonrpc::Answer& answer) {
        const std::shared_ptr<Pending<T>> held = weak.lock();
        if (!held) return;
        held->answer = read(answer);
        held->answered = true;
    };
    const auto fail = [answered](const std::string& why) {
        answered(std::unexpected(jsonrpc::ResponseError{.code = 0, .message = why}));
    };
    if (status_.state == ServiceState::Stopped || !connection_) {
        fail(status_.note);
        return pending;
    }
    auto send = [this, method = std::string(method), params = std::move(params), answered, weak] {
        const int id = connection_->peer().request(method, params(), answered);
        watched_.push_back({.id = id, .pending = std::weak_ptr<const void>(weak)});
    };
    if (ready()) {
        send();
    } else {
        waiting_.push_back({.send = std::move(send), .fail = fail});
    }
    return pending;
}

void Client::fail_waiting(const std::string& why) {
    for (const Waiting& waiting : std::exchange(waiting_, {})) waiting.fail(why);
}

std::vector<std::string> Client::completion_triggers(const fs::path& path) const {
    return serves(path) ? triggers_ : std::vector<std::string>{};
}

Asked<Completions> Client::complete(const fs::path& path, Position at, const CompletionTrigger& trigger) {
    const auto found = open_.find(path);
    if (found == open_.end()) return std::make_shared<const Pending<Completions>>(Pending<Completions>{.answered = true});
    Open& file = found->second;
    if (ready()) sync(path, file);
    const uint64_t version = file.document->version();
    const Position caret = file.document->buffer().clamp(at);
    const std::string line(file.document->buffer().line(caret.line));
    // Languages live as long as the program.
    const Language* language = &file.document->language();
    return ask<Completions>("textDocument/completion",
                            [this, path, uri = file.uri, caret, trigger] {
                                Json params = document_at(path, uri, caret);
                                params["context"] = completion_context(trigger);
                                return params;
                            },
                            [this, path, at = caret, version, line, language](const jsonrpc::Answer& answer) {
                                Completions out{.items = {}, .incomplete = false, .at = at, .version = version};
                                if (answer) {
                                    out.items = completions_from(*answer, at, line, *language, positions_for(path),
                                                                 &out.incomplete);
                                }
                                return out;
                            });
}

Asked<std::optional<Hover>> Client::hover(const fs::path& path, Position at) {
    const auto found = open_.find(path);
    if (found == open_.end()) return std::make_shared<const Pending<std::optional<Hover>>>(Pending<std::optional<Hover>>{.answered = true});
    if (ready()) sync(path, found->second);
    return ask<std::optional<Hover>>("textDocument/hover",
                                     [this, path, uri = found->second.uri, at] { return document_at(path, uri, at); },
                                     [this, path](const jsonrpc::Answer& answer) -> std::optional<Hover> {
                                         if (!answer) return std::nullopt;
                                         return hover_from(*answer, positions_for(path));
                                     });
}

Asked<std::vector<Location>> Client::definition(const fs::path& path, Position at) {
    const auto found = open_.find(path);
    if (found == open_.end()) {
        return std::make_shared<const Pending<std::vector<Location>>>(Pending<std::vector<Location>>{.answered = true});
    }
    if (ready()) sync(path, found->second);
    return ask<std::vector<Location>>("textDocument/definition",
                                      [this, path, uri = found->second.uri, at] { return document_at(path, uri, at); },
                                      [this](const jsonrpc::Answer& answer) -> std::vector<Location> {
                                          if (!answer) return {};
                                          return locations_from(*answer, [this](const fs::path& where) {
                                              return positions_for(where);
                                          });
                                      });
}

Asked<std::vector<Span>> Client::occurrences(const fs::path& path, Position at) {
    const auto found = open_.find(path);
    if (found == open_.end()) {
        return std::make_shared<const Pending<std::vector<Span>>>(Pending<std::vector<Span>>{.answered = true});
    }
    if (ready()) sync(path, found->second);
    return ask<std::vector<Span>>("textDocument/documentHighlight",
                                  [this, path, uri = found->second.uri, at] { return document_at(path, uri, at); },
                                  [this, path](const jsonrpc::Answer& answer) -> std::vector<Span> {
                                      if (!answer) return {};
                                      return occurrences_from(*answer, positions_for(path));
                                  });
}

Asked<ftk::Result<WorkspaceEdit>> Client::rename(const fs::path& path, Position at, std::string_view name) {
    const auto found = open_.find(path);
    if (found == open_.end()) {
        return std::make_shared<const Pending<ftk::Result<WorkspaceEdit>>>(
            Pending<ftk::Result<WorkspaceEdit>>{.answered = true, .answer = ftk::make_error("the file isn't open")});
    }
    if (ready()) sync(path, found->second);
    const auto params = [this, path, uri = found->second.uri, at, name = std::string(name)] {
        Json made = document_at(path, uri, at);
        made["newName"] = name;
        return made;
    };
    return ask<ftk::Result<WorkspaceEdit>>("textDocument/rename", params,
                                           [this](const jsonrpc::Answer& answer) -> ftk::Result<WorkspaceEdit> {
                                               if (!answer) return ftk::make_error(answer.error().message);
                                               return workspace_edit_from(*answer, [this](const fs::path& where) {
                                                   return positions_for(where);
                                               });
                                           });
}

Asked<std::optional<VersionedEdit>> Client::format(const fs::path& path) {
    const auto found = open_.find(path);
    if (found == open_.end()) {
        return std::make_shared<const Pending<std::optional<VersionedEdit>>>(
            Pending<std::optional<VersionedEdit>>{.answered = true});
    }
    Open& file = found->second;
    if (ready()) sync(path, file);
    const uint64_t version = file.document->version();
    const Indent indent = file.document->indent();
    const auto params = [uri = file.uri, indent] {
        return Json{{"textDocument", {{"uri", uri}}},
                    {"options", {{"tabSize", indent.width}, {"insertSpaces", !indent.tabs}}}};
    };
    return ask<std::optional<VersionedEdit>>("textDocument/formatting", params,
                                             [this, path, version](const jsonrpc::Answer& answer) -> std::optional<VersionedEdit> {
                                                 if (!answer || !answer->is_array()) return std::nullopt;
                                                 return VersionedEdit{.version = version,
                                                                      .edit = edit_from(*answer, positions_for(path))};
                                             });
}

Asked<ColourForms> Client::colour_forms(const fs::path& path, Span span, const std::array<float, 4>& rgba) {
    const auto found = open_.find(path);
    if (found == open_.end()) return std::make_shared<const Pending<ColourForms>>(Pending<ColourForms>{.answered = true});
    Open& file = found->second;
    if (ready()) sync(path, file);
    const uint64_t version = file.document->version();
    return ask<ColourForms>("textDocument/colorPresentation",
                            [this, path, uri = file.uri, span, rgba] {
                                return Json{{"textDocument", {{"uri", uri}}},
                                            {"color", colour_json(rgba)},
                                            {"range", positions_for(path).to_json(span)}};
                            },
                            [this, path, span, version](const jsonrpc::Answer& answer) {
                                ColourForms out{.changes = {}, .version = version};
                                if (answer) out.changes = colour_forms_from(*answer, span, positions_for(path));
                                return out;
                            });
}

} // namespace fjedit::lsp
