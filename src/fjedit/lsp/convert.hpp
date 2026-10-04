#pragma once

// Between the language server protocol's JSON and the editor's own types
// (language_service.hpp): file URIs and paths, positions counted as the
// server counts them, and what the server answers with read into
// diagnostics, completions, hovers, locations, edits and colours.

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/language_service.hpp"
#include "fjedit/core/syntax.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit::lsp {

using Json = nlohmann::json;

/// The file URI of `path`, an absolute path, its bytes outside the letters,
/// digits, - . _ ~ and the slashes percent-encoded: file:///home/kay/a%20b.cpp,
/// file:///C:/x.cpp.
[[nodiscard]] std::string uri_of(const std::filesystem::path& path);

/// The path the file URI `uri` names; nothing when it names no local file.
[[nodiscard]] std::optional<std::filesystem::path> path_of(std::string_view uri);

/// How a server counts along a line: in UTF-8 bytes, as the editor does, or
/// in UTF-16 units, the protocol's default.
enum class Encoding : uint8_t { Utf8, Utf16 };

/// Where along `line` a count of `units` of `encoding` reaches, as a byte;
/// the end of the line when it is past it.
[[nodiscard]] std::size_t byte_at(std::string_view line, std::size_t units, Encoding encoding);

/// How many units of `encoding` the first `byte` bytes of `line` are.
[[nodiscard]] std::size_t units_at(std::string_view line, std::size_t byte, Encoding encoding);

/// Positions between the editor's and a server's, in one text. Counting in
/// UTF-16 needs the text's lines, which `line` gives; UTF-8 needs nothing.
class Positions {
public:
    using Line = std::function<std::string_view(std::size_t line)>;

    explicit Positions(Encoding encoding = Encoding::Utf8, Line line = {});

    [[nodiscard]] Json to_json(Position at) const;
    [[nodiscard]] Json to_json(Span span) const;
    [[nodiscard]] Position position(const Json& json) const;
    [[nodiscard]] Span span(const Json& range) const;
    /// The byte `units` along line `line` reaches.
    [[nodiscard]] std::size_t byte(std::size_t line, std::size_t units) const;

private:
    Encoding encoding_;
    Line line_;
};

/// The positions in the file at a path, for answers about other files.
using PositionsFor = std::function<Positions(const std::filesystem::path& path)>;

/// The protocol's change to a document for each change of `edit`, in the
/// order the protocol makes them: one after the other, each in the text
/// the one before left, so the last of the edit's goes first. Positions in
/// UTF-8.
[[nodiscard]] Json content_changes(const Edit& edit);

/// A publishDiagnostics notification's diagnostics.
[[nodiscard]] std::vector<Diagnostic> diagnostics_from(const Json& diagnostics, const Positions& positions);

/// A completion's answer (a list, or an object holding one) for the caret
/// at `at` on `line`. An entry with no edit of its own replaces the word
/// before the caret.
[[nodiscard]] std::vector<Completion> completions_from(const Json& result, Position at, std::string_view line,
                                                       const Positions& positions, bool* incomplete = nullptr);

/// A hover's answer; nothing when it says nothing.
[[nodiscard]] std::optional<Hover> hover_from(const Json& result, const Positions& positions);

/// A definition's answer: one location, several, or links to them.
[[nodiscard]] std::vector<Location> locations_from(const Json& result, const PositionsFor& positions);

/// A list of the protocol's text edits as one edit, its changes in order.
[[nodiscard]] Edit edit_from(const Json& text_edits, const Positions& positions);

/// A workspace edit, by its changes or its documentChanges.
[[nodiscard]] WorkspaceEdit workspace_edit_from(const Json& edit, const PositionsFor& positions);

/// What each of the server's token types (its legend) colours as.
[[nodiscard]] std::vector<TokenKind> token_kinds(const Json& token_types);

/// The colours of semantic tokens' data, five numbers a token, each
/// counted from the one before, for a text of `line_count` lines. A name
/// the server calls plain (a variable) is kept, as plain text, over what
/// the lexer took it for.
[[nodiscard]] SemanticTokens tokens_from(const Json& data, const std::vector<TokenKind>& kinds,
                                         std::size_t line_count, const Positions& positions);

} // namespace fjedit::lsp
