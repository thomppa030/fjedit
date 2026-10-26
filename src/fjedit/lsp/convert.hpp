#pragma once

// Between the language server protocol's JSON and the editor's own types
// (language_service.hpp): file URIs and paths, positions counted as the
// server counts them, and what the server answers with read into
// diagnostics, completions, hovers, locations, edits and colours.

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/language.hpp"
#include "fjedit/core/language_service.hpp"
#include "fjedit/core/syntax.hpp"
#include "ftk/jsonrpc/documents.hpp"

#include <nlohmann/json.hpp>

#include <array>
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

/// Positions between the editor's and a server's, in one text. Counting in
/// UTF-16 needs the text's lines, which `line` gives; UTF-8 needs nothing.
class Positions {
public:
    using Line = std::function<std::string_view(std::size_t line)>;

    explicit Positions(ftk::jsonrpc::Encoding encoding = ftk::jsonrpc::Encoding::Utf8, Line line = {});

    [[nodiscard]] Json to_json(Position at) const;
    [[nodiscard]] Json to_json(Span span) const;
    [[nodiscard]] Position position(const Json& json) const;
    [[nodiscard]] Span span(const Json& range) const;
    /// The byte `units` along line `line` reaches.
    [[nodiscard]] std::size_t byte(std::size_t line, std::size_t units) const;

private:
    ftk::jsonrpc::Encoding encoding_;
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

/// Why a completion is asked, as the protocol's context tells the server:
/// the character typed, when one was.
[[nodiscard]] Json completion_context(const CompletionTrigger& trigger);

/// A completion's answer (a list, or an object holding one) for the caret
/// at `at` on `line`, written in `language`. An entry with no edit of its
/// own replaces the word before the caret, as the language has its words.
[[nodiscard]] std::vector<Completion> completions_from(const Json& result, Position at, std::string_view line,
                                                       const Language& language, const Positions& positions,
                                                       bool* incomplete = nullptr);

/// A hover's answer; nothing when it says nothing.
[[nodiscard]] std::optional<Hover> hover_from(const Json& result, const Positions& positions);

/// A definition's answer: one location, several, or links to them.
[[nodiscard]] std::vector<Location> locations_from(const Json& result, const PositionsFor& positions);

/// Where a symbol stands in a file, read from a documentHighlight answer:
/// each highlight's range, in order; none for a null answer.
[[nodiscard]] std::vector<Span> occurrences_from(const Json& result, const Positions& positions);

/// A documentColor answer: each colour written and where, in order; none
/// for a null answer. A channel the server leaves out is 0, alpha 1.
[[nodiscard]] std::vector<ColourValue> colour_values_from(const Json& result, const Positions& positions);

/// `rgba` as the protocol's colour: red, green, blue and alpha.
[[nodiscard]] Json colour_json(const std::array<float, 4>& rgba);

/// A colorPresentation answer for the value at `span`: each presentation's
/// change, its text edit or else its label over `span`, in order. What else
/// a presentation would change is left out.
[[nodiscard]] std::vector<Change> colour_forms_from(const Json& result, Span span, const Positions& positions);

/// A list of the protocol's text edits as one edit, its changes in order.
[[nodiscard]] Edit edit_from(const Json& text_edits, const Positions& positions);

/// A workspace edit, by its changes or its documentChanges.
[[nodiscard]] WorkspaceEdit workspace_edit_from(const Json& edit, const PositionsFor& positions);

/// What each of the server's token types (its legend) colours as: the
/// protocol's own names, and fjls's for a shader's uniforms (`uniform`), the
/// engine's built-ins (`builtin`) and FJSL's hints (`hint`). Any other name
/// is plain text.
[[nodiscard]] std::vector<TokenKind> token_kinds(const Json& token_types);

/// The colours of semantic tokens' data, five numbers a token, each
/// counted from the one before, for a text of `line_count` lines. A name
/// the server calls plain (a variable) is kept, as plain text, over what
/// the lexer took it for.
[[nodiscard]] SemanticTokens tokens_from(const Json& data, const std::vector<TokenKind>& kinds,
                                         std::size_t line_count, const Positions& positions);

} // namespace fjedit::lsp
