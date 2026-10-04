#pragma once

// Completion (sheet 39, P1): what the language service says completes the
// word being typed, in a list under it, narrowed as more is typed, the
// selected entry's signature and documentation beside it. It opens at Ctrl
// Space, on typing a word, and after . -> and ::; Up and Down move through
// it, Enter and Tab take the entry, Escape closes it, and a click takes the
// entry clicked.

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/commands.hpp"
#include "fjedit/core/language_service.hpp"

#include <glm/vec2.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit {

struct CompletionState {
    bool open{false};
    /// Where the word being completed begins.
    Position start{};
    /// What the service was asked, and what had been typed of the word
    /// when it was.
    Asked<Completions> asked{};
    std::string asked_for{};
    /// What is typed of the word now; the entries that match it, best
    /// first, as indices into the answer's; the one selected.
    std::string typed{};
    std::vector<std::size_t> shown{};
    std::size_t selected{0};
    /// The keys moved the selection: the list brings it into view.
    bool reveal{false};
};

/// Whether `line`, with the caret at `caret` just after what was typed,
/// asks for completion: a word begun, or . -> or :: just typed.
[[nodiscard]] bool asks_completion(std::string_view line, std::size_t caret);

/// Opens completion for the word the caret is in or after, asking
/// `service` about the file at `path`.
void open_completion(CompletionState& state, LanguageService& service, const std::filesystem::path& path,
                     const Buffer& buffer, Position caret);

/// Follows the caret: what is typed of the word, and the entries that match
/// it. Closes once the caret leaves the word or nothing matches; asks again
/// when the answer said more would come for more typed.
void follow_completion(CompletionState& state, LanguageService& service, const std::filesystem::path& path,
                       const Buffer& buffer, Position caret);

/// Moves the selection `by` entries, held to the list's ends.
void move_completion(CompletionState& state, std::ptrdiff_t by);

/// Takes the selected entry, or entry `index` of those shown: its text for
/// what is typed of the word, with what it brings along, as one step of
/// the history, the caret after its text. Closes.
void accept_completion(CompletionState& state, CommandContext& context, std::optional<std::size_t> index = std::nullopt);

/// How many entries the list shows at once.
inline constexpr std::size_t COMPLETION_ROWS = 10;

/// Draws the list under the word, from `anchor_min` to `anchor_max`, inside
/// the view from `view_min` to `view_max`, code at `text_size`; the
/// signature beside it in the language of files ending `extension`.
/// @return the entry clicked, for the caller to take
std::optional<std::size_t> completion_card(CompletionState& state, glm::vec2 view_min, glm::vec2 view_max,
                                           glm::vec2 anchor_min, glm::vec2 anchor_max, float text_size,
                                           std::string_view extension);

} // namespace fjedit
