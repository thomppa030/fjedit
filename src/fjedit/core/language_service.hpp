#pragma once

// What the editor asks about a language, from whoever knows it: clangd for
// C++ over the language server protocol, a program's own parsers for its
// own languages. The editor never knows which it is talking to.
//
// A service follows the documents it is told are open, and is brought up
// to date once a frame by update(): what changed goes to it, what it found
// comes back. Its problems, colours and colour values are carried to the
// text as it is now. A question (completion, hover, a definition, a rename,
// a format, a colour's forms) comes back as a Pending filled in when the
// answer comes; one that no one holds any more is given up.
//
//     auto asked = service.complete(path, caret, {});
//     // frames later:
//     if (asked->answered) show(asked->answer);

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/syntax.hpp"

#include "ftk/base/result.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit {

class Document;

/// How serious a problem is.
enum class Severity : uint8_t { Error, Warning, Information, Hint };

/// A problem found in a file.
struct Diagnostic {
    Span span{};
    Severity severity{Severity::Error};
    std::string message{};
    /// What found it ("clang", "clang-tidy"), and its name for the problem
    /// ("-Wunused-variable"), when it gives them.
    std::string source{};
    std::string code{};

    bool operator==(const Diagnostic&) const = default;
};

/// What an entry of a completion names, as the language server protocol
/// sorts them.
enum class CompletionKind : uint8_t {
    Text,
    Method,
    Function,
    Constructor,
    Field,
    Variable,
    Class,
    Interface,
    Module,
    Property,
    Unit,
    Value,
    Enum,
    Keyword,
    Snippet,
    Color,
    File,
    Reference,
    Folder,
    EnumMember,
    Constant,
    Struct,
    Event,
    Operator,
    TypeParameter,
};

/// One entry of a completion.
struct Completion {
    /// The name the list shows ("rotate").
    std::string label{};
    CompletionKind kind{CompletionKind::Text};
    /// Its parameters, which its signature shows after the name
    /// ("(float speed)"); empty for what takes none.
    std::string parameters{};
    /// The type it gives ("qua<T, Q>"), or what else the server says of it.
    std::string detail{};
    /// What it is, in Markdown.
    std::string documentation{};
    /// What choosing it does: `change.span`, from where the word being
    /// typed begins to the caret, replaced with its text.
    Change change{};
    /// Other changes it brings with it, such as an #include for the header
    /// it is in.
    std::vector<Change> more{};
    /// What typing filters it by, and sorts it by.
    std::string filter{};
    std::string sort{};

    bool operator==(const Completion&) const = default;
};

/// The entries that complete what is typed at `at`, in the document's
/// `version`.
struct Completions {
    std::vector<Completion> items{};
    /// More would come for more typed: asked again rather than filtered.
    bool incomplete{false};
    Position at{};
    uint64_t version{0};
};

/// What asked for a completion, as the protocol tells a server why.
struct CompletionTrigger {
    enum class Kind : uint8_t {
        /// A word begun, or asked for by hand (Ctrl Space).
        Invoked,
        /// `character` typed, one the service named
        /// (LanguageService::completion_triggers()).
        Character,
        /// More typed of a word whose answer said more would come.
        Incomplete,
    };
    Kind kind{Kind::Invoked};
    std::string character{};

    bool operator==(const CompletionTrigger&) const = default;
};

/// What the code at a place is, in Markdown, and the span it is about.
struct Hover {
    std::string markdown{};
    std::optional<Span> span{};

    bool operator==(const Hover&) const = default;
};

/// A place in a file.
struct Location {
    std::filesystem::path path{};
    Span span{};

    bool operator==(const Location&) const = default;
};

/// An edit to the document as it was in `version`, a whole file's changes
/// as one step.
struct VersionedEdit {
    uint64_t version{0};
    Edit edit{};
};

/// Edits to several files at once, as a rename makes: each file's in the
/// text its document had at `version` when it is open, the file on disk
/// otherwise (no version).
struct FileEdits {
    std::optional<uint64_t> version{};
    Edit edit{};
};
using WorkspaceEdit = std::map<std::filesystem::path, FileEdits>;

/// A line's colours as the service knows them, over the lexer's.
struct SemanticTokens {
    std::vector<std::vector<Token>> lines{};
};

/// A colour written in a file: where its value is (`#44cc44`,
/// `vec3(1.0, 0.5, 0.2)`), and the colour, red, green, blue and alpha from 0
/// to 1, in sRGB.
struct ColourValue {
    Span span{};
    std::array<float, 4> rgba{0.0f, 0.0f, 0.0f, 1.0f};

    bool operator==(const ColourValue&) const = default;
};

/// How a colour value can be written as another colour, each the change
/// that writes it: in the form it is written in first, then its others. For
/// the document's `version`; none where no colour value was written.
struct ColourForms {
    std::vector<Change> changes{};
    uint64_t version{0};
};

/// An answer still to come. The one who asked keeps it while waiting and
/// reads `answer` once `answered`; letting it go gives the question up.
template <typename T>
struct Pending {
    bool answered{false};
    T answer{};
};

template <typename T>
using Asked = std::shared_ptr<const Pending<T>>;

/// Where a service is.
enum class ServiceState : uint8_t {
    /// Being started, and not yet answering.
    Starting,
    Ready,
    /// Stopped, or never started: `note` says why.
    Stopped,
};

struct ServiceStatus {
    ServiceState state{ServiceState::Starting};
    /// What it last said of itself, or why it stopped.
    std::string note{};
};

class LanguageService {
public:
    virtual ~LanguageService() = default;

    /// Whether it knows the language of the file at `path`.
    [[nodiscard]] virtual bool serves(const std::filesystem::path& path) const = 0;

    /// Follows `document`, the file at `path` open, until close(). The
    /// document outlives that.
    virtual void open(const std::filesystem::path& path, const Document& document) = 0;
    virtual void close(const std::filesystem::path& path) = 0;
    /// The file at `path` was saved as its document holds it.
    virtual void saved(const std::filesystem::path& path) = 0;

    /// Tells it what changed in the documents it follows, and takes in
    /// what came back. Once a frame.
    virtual void update() = 0;

    [[nodiscard]] virtual ServiceStatus status() const = 0;

    /// The problems in the file at `path`, where they are in its document
    /// now; none for one it doesn't follow.
    [[nodiscard]] virtual const std::vector<Diagnostic>& diagnostics(const std::filesystem::path& path) = 0;

    /// The colours it knows for the file at `path`, for its document as it
    /// is now; nothing when it knows none, or none that still fit. Colours
    /// that change come as another object or with the document's version
    /// changed, so what is worked out from them (the brackets' levels)
    /// holds while both stay.
    [[nodiscard]] virtual const SemanticTokens* tokens(const std::filesystem::path& path) = 0;

    /// The colour values written in the file at `path`, in order, where they
    /// are in its document now; none for a file it doesn't follow or knows
    /// none in.
    [[nodiscard]] virtual const std::vector<ColourValue>& colour_values(const std::filesystem::path& path) = 0;

    /// How the colour value at `span` in the file at `path` is written as
    /// `rgba` (sRGB, from 0 to 1) instead; no changes where it names no
    /// colour value there.
    [[nodiscard]] virtual Asked<ColourForms> colour_forms(const std::filesystem::path& path, Span span,
                                                          const std::array<float, 4>& rgba) = 0;

    /// What, typed in the file at `path`, asks it for completion besides a
    /// word begun: "." and "->" for C++. None while it starts, or from a
    /// service that names none.
    [[nodiscard]] virtual std::vector<std::string> completion_triggers(const std::filesystem::path& path) const = 0;

    /// What completes the word typed at `at`, asked for by `trigger`.
    [[nodiscard]] virtual Asked<Completions> complete(const std::filesystem::path& path, Position at,
                                                      const CompletionTrigger& trigger) = 0;

    /// What the code at `at` is; nothing when there is nothing to say.
    [[nodiscard]] virtual Asked<std::optional<Hover>> hover(const std::filesystem::path& path, Position at) = 0;

    /// Where what is named at `at` is defined; none when nowhere known.
    [[nodiscard]] virtual Asked<std::vector<Location>> definition(const std::filesystem::path& path,
                                                                  Position at) = 0;

    /// Where in the file at `path` what is named at `at` stands: where it is
    /// made and each use, in its document as it is when asked, in order;
    /// none when the service can't say.
    [[nodiscard]] virtual Asked<std::vector<Span>> occurrences(const std::filesystem::path& path, Position at) = 0;

    /// The edits that rename what is named at `at` to `name`, everywhere
    /// it is used, or why it can't be.
    [[nodiscard]] virtual Asked<ftk::Result<WorkspaceEdit>> rename(const std::filesystem::path& path, Position at,
                                                                   std::string_view name) = 0;

    /// The edit that formats the whole file; nothing when it can't.
    [[nodiscard]] virtual Asked<std::optional<VersionedEdit>> format(const std::filesystem::path& path) = 0;
};

} // namespace fjedit
