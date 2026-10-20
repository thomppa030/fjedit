#pragma once

// Vim's registers, which every view with Vim on shares: where a yank or a
// delete puts its text, and where a put takes it from. Each holds a piece of
// text for every caret the yank or delete had (decision 5 of the Vim plan),
// and says whether the text is whole lines.
//
//     registers.yank('"', {.pieces = {"spin"}}, &clipboard);
//     const std::optional<fjedit::vim::Yanked> put = registers.get('"', &clipboard);

#include "fjedit/core/commands.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace fjedit::vim {

/// What a register holds.
struct Yanked {
    /// The text taken at each caret, in their order: whole lines without
    /// the break after the last when `linewise`.
    std::vector<std::string> pieces;
    /// The text is whole lines, which a put puts on lines of their own.
    bool linewise{false};

    /// The pieces as one text, apart by line breaks; whole lines end with
    /// one.
    [[nodiscard]] std::string text() const;

    bool operator==(const Yanked&) const = default;
};

class Registers {
public:
    /// Whether `name` names a register: `"`, `0`–`9`, `a`–`z`, `A`–`Z`,
    /// `-`, `_`, `+`, `*`.
    [[nodiscard]] static bool valid(char name);

    /// What register `name` holds, `A`–`Z` read as `a`–`z`, `+` and `*`
    /// from `clipboard` (whole lines when its text ends with a line break).
    /// Nothing for a name that isn't a register, or one that holds nothing.
    [[nodiscard]] std::optional<Yanked> get(char name, const Clipboard* clipboard) const;

    /// `yanked` yanked into `name`, `"` when none was named: it goes to the
    /// unnamed register, and to `0` when none was named, or to the one named,
    /// `A`–`Z` adding to theirs, `+` and `*` to `clipboard`. `_` keeps
    /// nothing.
    void yank(char name, Yanked yanked, Clipboard* clipboard);

    /// `yanked` deleted into `name`: as a yank, but with none named it goes
    /// to `1`, the older ones moving up to `9`, when it is whole lines or
    /// reaches over a line break, else to `-`; and to `1` as well whatever
    /// its size when `numbered` (the motions Vim always numbers: `%`, `{`,
    /// `}`).
    void deleted(char name, Yanked yanked, Clipboard* clipboard, bool numbered = false);

    /// clipboard=unnamedplus: the unnamed register is the system's
    /// clipboard as well, what a yank or a delete with none named puts going
    /// there too, and a put with none named taking what the clipboard holds.
    void use_clipboard(bool unnamed) { clipboard_ = unnamed; }
    [[nodiscard]] bool uses_clipboard() const { return clipboard_; }

private:
    // `yanked` into the one register `name` names, besides the unnamed.
    void put_in(char name, const Yanked& yanked, Clipboard* clipboard);

    std::optional<Yanked> unnamed_;
    bool clipboard_{false};
    std::array<std::optional<Yanked>, 10> numbered_{};
    std::optional<Yanked> small_;
    std::array<std::optional<Yanked>, 26> named_{};
};

} // namespace fjedit::vim
