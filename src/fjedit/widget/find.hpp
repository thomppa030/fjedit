#pragma once

// A code view's find (sheet 39, F1): what it looks for and replaces with,
// the matches in the text, going from one to the next, and the plate it
// floats on over the view's top right. The view keeps one FindState and
// runs what the plate asks for.

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/document.hpp"
#include "fjedit/core/find.hpp"
#include "fjedit/core/selections.hpp"

#include "ftk/ui/core/rect.hpp"
#include "ftk/ui/kit/find.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fjedit {

/// What a view's find looks for and replaces with, and what it found.
struct FindState {
    /// Its plate is shown.
    bool open{false};
    /// Its matches are drawn while the plate is closed: Vim's search.
    bool shown{false};
    std::string query;
    std::string replacement;
    ftk::ui::FindToggles toggles;
    /// For a regular expression, the group whose text a match is
    /// (FindOptions::group): Vim's \zs. The plate's searches take all.
    std::size_t group{0};

    /// Which of the plate's fields the keyboard goes to on its next draw.
    enum class Focus : uint8_t { None, Query, Replacement };
    Focus focus{Focus::None};

    /// Every match, in order. They are found again when the text changes
    /// from `version`, or when `stale` says the query or toggles did.
    std::vector<Span> matches;
    uint64_t version{0};
    bool stale{true};
};

/// The options the toggles stand for.
[[nodiscard]] FindOptions find_options(const ftk::ui::FindToggles& toggles);

/// Opens the find with the keyboard in the field `focus` names, the query
/// taken from the primary selection: its text when it lies on one line,
/// otherwise the word at its caret, otherwise the query as it was.
void open_find(FindState& find, const Buffer& buffer, const Selections& selections, FindState::Focus focus);

/// Finds the matches again if the text, the query or the toggles changed
/// since they were found.
void refresh_matches(FindState& find, const Document& document);

/// What a count of the matches says: the primary selection's place among
/// them ("3 of 17"), how many there are when it is none of them, "No
/// results" when there are none; empty while nothing is looked for.
[[nodiscard]] std::string match_count(const FindState& find, const Selections& selections);

/// Which match the primary selection is, when it is one.
[[nodiscard]] std::optional<std::size_t> current_match(const FindState& find, const Selections& selections);

/// The first match that begins at or after `from`, going round to the first
/// of all; nothing when there are none.
[[nodiscard]] std::optional<Span> match_from(const FindState& find, Position from);

/// The match after the primary selection, or before it, going round.
[[nodiscard]] std::optional<Span> next_match(const FindState& find, const Selections& selections);
[[nodiscard]] std::optional<Span> previous_match(const FindState& find, const Selections& selections);

/// What was done on the plate in a frame.
struct FindPlate {
    /// The query or the toggles changed: the view selects the first match
    /// from the start of its primary selection, so a match it is on stays
    /// while the query still matches there.
    bool searched{false};
    bool next{false};
    bool previous{false};
    /// Replace the selected match and go on to the next, or every one.
    bool replace{false};
    bool replace_all{false};
    /// The plate closed: the keyboard goes back to the view.
    bool closed{false};
};

/// Draws the find's plate over the top right of `over`, the view, and takes
/// what is done on it. The count names the primary selection's place among
/// the matches.
FindPlate find_plate(FindState& find, const Selections& selections, ftk::ui::core::Rect over);

} // namespace fjedit
