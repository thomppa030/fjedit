#include "fjedit/lsp/convert.hpp"


#include <algorithm>
#include <format>
#include <string>

namespace fjedit::lsp {

namespace {

namespace fs = std::filesystem;

std::size_t size_of(const Json& json, const char* key) {
    const auto found = json.find(key);
    if (found == json.end() || !found->is_number_integer()) return 0;
    return static_cast<std::size_t>(std::max<int64_t>(found->get<int64_t>(), 0));
}

std::string string_of(const Json& json, const char* key) {
    const auto found = json.find(key);
    return found != json.end() && found->is_string() ? found->get<std::string>() : std::string();
}

// Documentation as Markdown: a MarkupContent's value, or a plain string.
std::string markdown_of(const Json& documentation) {
    if (documentation.is_string()) return documentation.get<std::string>();
    if (documentation.is_object()) return string_of(documentation, "value");
    return {};
}

// A hover's contents: MarkupContent, a MarkedString (a string, or code in
// a language), or a list of MarkedStrings.
std::string hover_markdown(const Json& contents) {
    if (contents.is_string()) return contents.get<std::string>();
    if (contents.is_array()) {
        std::string out;
        for (const Json& part : contents) {
            const std::string text = hover_markdown(part);
            if (text.empty()) continue;
            if (!out.empty()) out += "\n\n";
            out += text;
        }
        return out;
    }
    if (!contents.is_object()) return {};
    if (contents.contains("kind")) return string_of(contents, "value");
    // A MarkedString with a language is a block of code.
    return std::format("```{}\n{}\n```", string_of(contents, "language"), string_of(contents, "value"));
}

Severity severity_of(const Json& diagnostic) {
    switch (size_of(diagnostic, "severity")) {
        case 2: return Severity::Warning;
        case 3: return Severity::Information;
        case 4: return Severity::Hint;
        default: return Severity::Error;
    }
}

// A change of a text edit list or a completion's edit.
Change change_of(const Json& text_edit, const Positions& positions) {
    return {.span = positions.span(text_edit.value("range", Json::object())),
            .text = string_of(text_edit, "newText")};
}

} // namespace

Positions::Positions(ftk::jsonrpc::Encoding encoding, Line line) : encoding_(encoding), line_(std::move(line)) {}

std::size_t Positions::byte(std::size_t line, std::size_t units) const {
    if (encoding_ == ftk::jsonrpc::Encoding::Utf8 || !line_) return units;
    return ftk::jsonrpc::byte_at(line_(line), units, encoding_);
}

Json Positions::to_json(Position at) const {
    const std::size_t units = encoding_ == ftk::jsonrpc::Encoding::Utf8 || !line_
                                  ? at.byte
                                  : ftk::jsonrpc::units_at(line_(at.line), at.byte, encoding_);
    return {{"line", at.line}, {"character", units}};
}

Json Positions::to_json(Span span) const {
    return {{"start", to_json(span.from)}, {"end", to_json(span.to)}};
}

Position Positions::position(const Json& json) const {
    if (!json.is_object()) return {};
    const std::size_t line = size_of(json, "line");
    return {.line = line, .byte = byte(line, size_of(json, "character"))};
}

Span Positions::span(const Json& range) const {
    if (!range.is_object()) return {};
    Span out{.from = position(range.value("start", Json::object())), .to = position(range.value("end", Json::object()))};
    if (out.to < out.from) std::swap(out.from, out.to);
    return out;
}

Json content_changes(const Edit& edit) {
    const Positions utf8;
    Json changes = Json::array();
    for (auto change = edit.changes.rbegin(); change != edit.changes.rend(); ++change) {
        changes.push_back({{"range", utf8.to_json(change->span)}, {"text", change->text}});
    }
    return changes;
}

std::vector<Diagnostic> diagnostics_from(const Json& diagnostics, const Positions& positions) {
    std::vector<Diagnostic> out;
    if (!diagnostics.is_array()) return out;
    for (const Json& diagnostic : diagnostics) {
        if (!diagnostic.is_object()) continue;
        std::string code;
        if (const auto found = diagnostic.find("code"); found != diagnostic.end()) {
            code = found->is_string() ? found->get<std::string>() : found->is_number() ? found->dump() : std::string();
        }
        out.push_back({.span = positions.span(diagnostic.value("range", Json::object())),
                       .severity = severity_of(diagnostic),
                       .message = string_of(diagnostic, "message"),
                       .source = string_of(diagnostic, "source"),
                       .code = std::move(code)});
    }
    return out;
}

Json completion_context(const CompletionTrigger& trigger) {
    // The protocol's CompletionTriggerKind, counted from 1.
    switch (trigger.kind) {
        case CompletionTrigger::Kind::Character:
            return {{"triggerKind", 2}, {"triggerCharacter", trigger.character}};
        case CompletionTrigger::Kind::Incomplete:
            return {{"triggerKind", 3}};
        case CompletionTrigger::Kind::Invoked:
            break;
    }
    return {{"triggerKind", 1}};
}

std::vector<Completion> completions_from(const Json& result, Position at, std::string_view line,
                                         const Language& language, const Positions& positions, bool* incomplete) {
    std::vector<Completion> out;
    const Json* items = &result;
    if (result.is_object()) {
        if (incomplete != nullptr) *incomplete = result.value("isIncomplete", false);
        const auto found = result.find("items");
        if (found == result.end()) return out;
        items = &*found;
    } else if (incomplete != nullptr) {
        *incomplete = false;
    }
    if (!items->is_array()) return out;

    // The word being typed, which an entry with no edit of its own replaces.
    std::size_t word = std::min(at.byte, line.size());
    while (word > 0 && word_byte(language, line[word - 1])) --word;
    const Span typed{.from = {at.line, word}, .to = at};

    for (const Json& item : *items) {
        if (!item.is_object()) continue;
        Completion completion;
        completion.label = string_of(item, "label");
        const std::size_t kind = size_of(item, "kind");
        if (kind >= 1 && kind <= 25) completion.kind = static_cast<CompletionKind>(kind - 1);
        completion.detail = string_of(item, "detail");
        if (const auto details = item.find("labelDetails"); details != item.end() && details->is_object()) {
            completion.parameters = string_of(*details, "detail");
            if (completion.detail.empty()) completion.detail = string_of(*details, "description");
        }
        completion.documentation = markdown_of(item.value("documentation", Json()));
        if (const auto text_edit = item.find("textEdit"); text_edit != item.end() && text_edit->is_object()) {
            completion.change = text_edit->contains("range")
                                    ? change_of(*text_edit, positions)
                                    : Change{.span = positions.span(text_edit->value("replace", Json::object())),
                                             .text = string_of(*text_edit, "newText")};
        } else {
            const std::string insert = string_of(item, "insertText");
            completion.change = {.span = typed, .text = insert.empty() ? completion.label : insert};
        }
        if (const auto more = item.find("additionalTextEdits"); more != item.end() && more->is_array()) {
            for (const Json& text_edit : *more) completion.more.push_back(change_of(text_edit, positions));
        }
        completion.filter = string_of(item, "filterText");
        if (completion.filter.empty()) completion.filter = completion.label;
        completion.sort = string_of(item, "sortText");
        if (completion.sort.empty()) completion.sort = completion.label;
        out.push_back(std::move(completion));
    }
    return out;
}

std::optional<Hover> hover_from(const Json& result, const Positions& positions) {
    if (!result.is_object()) return std::nullopt;
    Hover hover{.markdown = hover_markdown(result.value("contents", Json())), .span = std::nullopt};
    if (hover.markdown.empty()) return std::nullopt;
    if (const auto range = result.find("range"); range != result.end() && range->is_object()) {
        hover.span = positions.span(*range);
    }
    return hover;
}

std::vector<Location> locations_from(const Json& result, const PositionsFor& positions) {
    std::vector<Location> out;
    const auto add = [&](const Json& location) {
        if (!location.is_object()) return;
        // A Location names its uri and range; a LocationLink its target's,
        // the name's own range within it first.
        const bool link = location.contains("targetUri");
        const std::optional<fs::path> path = ftk::jsonrpc::path_of(string_of(location, link ? "targetUri" : "uri"));
        if (!path) return;
        const Json range = link ? location.value("targetSelectionRange", location.value("targetRange", Json::object()))
                                : location.value("range", Json::object());
        out.push_back({.path = *path, .span = positions(*path).span(range)});
    };
    if (result.is_array()) {
        for (const Json& location : result) add(location);
    } else {
        add(result);
    }
    return out;
}

std::vector<Span> occurrences_from(const Json& result, const Positions& positions) {
    std::vector<Span> out;
    if (!result.is_array()) return out;
    for (const Json& highlight : result) {
        if (highlight.is_object() && highlight.contains("range")) out.push_back(positions.span(highlight["range"]));
    }
    std::ranges::sort(out, {}, &Span::from);
    return out;
}

std::vector<ColourValue> colour_values_from(const Json& result, const Positions& positions) {
    std::vector<ColourValue> out;
    if (!result.is_array()) return out;
    for (const Json& information : result) {
        if (!information.is_object() || !information.contains("range")) continue;
        const Json colour = information.value("color", Json::object());
        const auto channel = [&](const char* name, float otherwise) {
            const auto found = colour.find(name);
            return found != colour.end() && found->is_number() ? found->get<float>() : otherwise;
        };
        out.push_back({.span = positions.span(information["range"]),
                       .rgba = {channel("red", 0.0f), channel("green", 0.0f), channel("blue", 0.0f),
                                channel("alpha", 1.0f)}});
    }
    std::ranges::sort(out, {}, [](const ColourValue& value) { return value.span.from; });
    return out;
}

Json colour_json(const std::array<float, 4>& rgba) {
    return {{"red", rgba[0]}, {"green", rgba[1]}, {"blue", rgba[2]}, {"alpha", rgba[3]}};
}

std::vector<Change> colour_forms_from(const Json& result, Span span, const Positions& positions) {
    std::vector<Change> out;
    if (!result.is_array()) return out;
    for (const Json& presentation : result) {
        if (!presentation.is_object()) continue;
        if (const auto text_edit = presentation.find("textEdit"); text_edit != presentation.end() && text_edit->is_object()) {
            out.push_back(change_of(*text_edit, positions));
        } else if (const auto label = presentation.find("label"); label != presentation.end() && label->is_string()) {
            out.push_back({.span = span, .text = label->get<std::string>()});
        }
    }
    return out;
}

Edit edit_from(const Json& text_edits, const Positions& positions) {
    Edit edit;
    if (!text_edits.is_array()) return edit;
    for (const Json& text_edit : text_edits) {
        if (text_edit.is_object()) edit.changes.push_back(change_of(text_edit, positions));
    }
    std::ranges::stable_sort(edit.changes, {}, [](const Change& change) { return change.span.from; });
    return edit;
}

WorkspaceEdit workspace_edit_from(const Json& edit, const PositionsFor& positions) {
    WorkspaceEdit out;
    if (!edit.is_object()) return out;
    if (const auto changes = edit.find("changes"); changes != edit.end() && changes->is_object()) {
        for (const auto& [uri, text_edits] : changes->items()) {
            const std::optional<fs::path> path = ftk::jsonrpc::path_of(uri);
            if (path) out[*path] = FileEdits{.version = std::nullopt, .edit = edit_from(text_edits, positions(*path))};
        }
    }
    if (const auto documents = edit.find("documentChanges"); documents != edit.end() && documents->is_array()) {
        for (const Json& document : *documents) {
            // Files made, renamed or deleted carry a kind; only edits to
            // text are taken.
            if (!document.is_object() || document.contains("kind")) continue;
            const Json identifier = document.value("textDocument", Json::object());
            const std::optional<fs::path> path = ftk::jsonrpc::path_of(string_of(identifier, "uri"));
            if (!path) continue;
            FileEdits file{.version = std::nullopt, .edit = edit_from(document.value("edits", Json()), positions(*path))};
            if (const auto version = identifier.find("version"); version != identifier.end() && version->is_number_integer()) {
                file.version = version->get<uint64_t>();
            }
            out[*path] = std::move(file);
        }
    }
    return out;
}

std::vector<TokenKind> token_kinds(const Json& token_types) {
    std::vector<TokenKind> kinds;
    if (!token_types.is_array()) return kinds;
    for (const Json& type : token_types) {
        const std::string name = type.is_string() ? type.get<std::string>() : std::string();
        TokenKind kind = TokenKind::Text;
        if (name == "namespace" || name == "type" || name == "class" || name == "enum" || name == "interface" ||
            name == "struct" || name == "typeParameter" || name == "concept") {
            kind = TokenKind::Type;
        } else if (name == "function" || name == "method") {
            kind = TokenKind::Function;
        } else if (name == "property") {
            kind = TokenKind::Member;
        } else if (name == "macro" || name == "hint") {
            kind = TokenKind::Meta;
        } else if (name == "uniform") {
            kind = TokenKind::Uniform;
        } else if (name == "builtin") {
            kind = TokenKind::Builtin;
        } else if (name == "operator") {
            kind = TokenKind::Punctuation;
        } else if (name == "bracket") {
            kind = TokenKind::Bracket;
        } else if (name == "keyword" || name == "modifier") {
            kind = TokenKind::Keyword;
        } else if (name == "comment") {
            kind = TokenKind::Comment;
        } else if (name == "string") {
            kind = TokenKind::String;
        } else if (name == "number") {
            kind = TokenKind::Number;
        }
        kinds.push_back(kind);
    }
    return kinds;
}

SemanticTokens tokens_from(const Json& data, const std::vector<TokenKind>& kinds, std::size_t line_count,
                           const Positions& positions) {
    SemanticTokens out;
    out.lines.resize(line_count);
    if (!data.is_array()) return out;
    std::size_t line = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i + 4 < data.size(); i += 5) {
        const auto number = [&](std::size_t at) {
            return data[at].is_number_integer() ? static_cast<std::size_t>(std::max<int64_t>(data[at].get<int64_t>(), 0))
                                                : std::size_t{0};
        };
        const std::size_t down = number(i);
        line += down;
        start = down > 0 ? number(i + 1) : start + number(i + 1);
        if (line >= line_count) break;
        const std::size_t type = number(i + 3);
        const TokenKind kind = type < kinds.size() ? kinds[type] : TokenKind::Text;
        out.lines[line].push_back({.begin = positions.byte(line, start),
                                   .end = positions.byte(line, start + number(i + 2)),
                                   .kind = kind});
    }
    return out;
}

} // namespace fjedit::lsp
