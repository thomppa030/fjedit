#pragma once

// The places a group of tabs jumped from, as Vim keeps a window's jump
// list: each jump puts where it went from on the end, and Ctrl-O goes back
// through them, Ctrl-I forward again, across files. A place catches up with
// the edits made to its file, as a caret in another view does.
//
//     jumps.jumped({.path = path, .at = caret, .version = document.version()});
//     ...
//     jumps.catch_up(workspace);
//     if (const auto to = jumps.step(-1, here)) go_to(to->path, to->at);

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/document.hpp"
#include "fjedit/core/workspace.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace fjedit {

class JumpList {
public:
    /// A place in a file: where in its text, as the text was at `version`;
    /// with none, as its text is when the file next opens.
    struct Place {
        std::filesystem::path path;
        Position at;
        std::optional<uint64_t> version;

        bool operator==(const Place&) const = default;
    };

    /// How many places it keeps, as many as Vim: past that the oldest goes.
    static constexpr std::size_t SIZE = 100;

    /// A jump from `from`: it goes on the end, newest, and Ctrl-O starts
    /// back from there.
    void jumped(Place from);

    /// Ctrl-O `count` places back (a count below 0) or Ctrl-I forward: the
    /// place to go to; nothing when the list doesn't reach that far. First
    /// an older place on the same line of the same file as a newer one is
    /// let go, so each line comes back once; and the first step back from
    /// the end puts `here` on it, for Ctrl-I to come back to.
    [[nodiscard]] std::optional<Place> step(long count, const Place& here);

    /// Each place in a file `workspace` has open caught up with the edits
    /// made to it since.
    void catch_up(const Workspace& workspace);

    /// The file at `path` let go, as `document` holds it last: its places
    /// caught up with it and kept as they are, until the file opens again.
    void closed(const std::filesystem::path& path, const Document& document);

    /// The file at `path` opened again, its text at `version`: its places
    /// catch up from there.
    void opened(const std::filesystem::path& path, uint64_t version);

    [[nodiscard]] std::span<const Place> places() const { return places_; }

    /// Where Ctrl-O and Ctrl-I step from: past the newest after a jump.
    [[nodiscard]] std::size_t at() const { return at_; }

private:
    // An older place on the same line of the same file as a newer one let
    // go, as Vim cleans its list before stepping it.
    void drop_repeats();

    std::vector<Place> places_;
    std::size_t at_{0};
};

} // namespace fjedit
