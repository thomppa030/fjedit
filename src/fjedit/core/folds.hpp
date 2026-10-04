#pragma once

// Which of a document's folds one view has closed, and so which of its lines
// the view shows, row by row. A closed fold shows its first line and hides
// the rest, through the line its closer is on; the view draws a mark for
// what is hidden and the closer's line after it (`begin_play() {⋯ }`). A
// fold that begins on the line a closed one ends on (`} else {`) folds on
// along the same row. Each view keeps its own, and they stay with their
// lines through edits.
//
//     fjedit::Folds folds;
//     folds.close(document, 11);
//     folds.catch_up(document);   // after edits, before drawing
//     const std::size_t row = folds.row_of(line);

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/document.hpp"
#include "fjedit/core/selections.hpp"
#include "fjedit/core/structure.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace fjedit {

class Folds {
public:
    /// Closes the fold of `document` that begins on `line`; false when none
    /// does, or it is closed already.
    bool close(const Document& document, std::size_t line);

    /// Opens the closed fold that begins on `line`; false when none does.
    bool open(std::size_t line);

    /// Opens the closed folds drawn on the row `line` begins: the one that
    /// begins on it and those folding on along the row. Folds inside them
    /// stay as they are. False when none was closed.
    bool open_row(std::size_t line);

    /// Opens every closed fold that hides `line`; false when none does.
    bool reveal(std::size_t line);

    void close_all(const Document& document);
    void open_all();

    /// The closed fold that begins on `line`, whether or not the row shows
    /// it: a fold inside a closed one stays closed for when that opens.
    [[nodiscard]] std::optional<FoldRange> closed_at(std::size_t line) const;

    /// Brings them up to date with the edits made to `document` since they
    /// last looked. A closed fold stays closed while the ends of its first
    /// and last lines, carried through the edits, are still on the lines
    /// one of the document's folds begins and ends on; otherwise it opens,
    /// as every one does when the document no longer has the edits.
    void catch_up(const Document& document);

    /// Whether a closed fold hides `line`.
    [[nodiscard]] bool hidden(std::size_t line) const;

    /// The row `line` is drawn on; a hidden line is on its fold's row.
    [[nodiscard]] std::size_t row_of(std::size_t line) const;

    /// The line a row begins with: the one drawn at its start.
    [[nodiscard]] std::size_t line_of(std::size_t row) const;

    /// How many rows a text of `line_count` lines takes.
    [[nodiscard]] std::size_t row_count(std::size_t line_count) const;

    /// The lines on the row `line` begins: from it through the last line a
    /// closed fold hides there, or `line` alone.
    [[nodiscard]] FoldRange row_lines(std::size_t line) const;

    /// The line `count` rows below or above `line`'s, as Layout::line_below()
    /// and Layout::line_above() answer it.
    [[nodiscard]] std::optional<std::size_t> line_below(const Buffer& buffer, std::size_t line,
                                                        std::size_t count) const;
    [[nodiscard]] std::optional<std::size_t> line_above(std::size_t line, std::size_t count) const;

private:
    // A closed fold, held by where its first and last lines end, which edits
    // carry along.
    struct Closed {
        Position first_end;
        Position last_end;

        [[nodiscard]] FoldRange range() const { return {.first = first_end.line, .last = last_end.line}; }
    };

    // A row that hides lines: closed folds, merged where one hides another
    // or folds on from where it ends; and how many lines the rows before it
    // hide.
    struct Run {
        std::size_t first;
        std::size_t last;
        std::size_t hidden_before;
    };

    // The runs drawn from what is closed now.
    void rebuild();
    // The run hiding lines after `line`'s, or after the last before it.
    [[nodiscard]] const Run* run_before_or_at(std::size_t line) const;

    std::vector<Closed> closed_;
    std::vector<Run> runs_;
    std::optional<uint64_t> seen_;
};

/// Moves each caret a closed fold hides to the end of the line its row
/// begins with, where a fold closed over carets leaves them.
void move_out_of_folds(Selections& selections, const Folds& folds, const Buffer& buffer);

} // namespace fjedit
