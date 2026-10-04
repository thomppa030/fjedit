#include "fjedit/core/folds.hpp"

#include <algorithm>
#include <utility>

namespace fjedit {

namespace {

// The fold of `document` that begins on `line`.
std::optional<FoldRange> fold_beginning(const Document& document, std::size_t line) {
    const std::vector<FoldRange>& folds = document.folds();
    const auto found = std::partition_point(folds.begin(), folds.end(),
                                            [&](const FoldRange& fold) { return fold.first < line; });
    if (found == folds.end() || found->first != line) return std::nullopt;
    return *found;
}

Position end_of(const Buffer& buffer, std::size_t line) {
    return {.line = line, .byte = buffer.line(line).size()};
}

} // namespace

bool Folds::close(const Document& document, std::size_t line) {
    catch_up(document);
    const std::optional<FoldRange> fold = fold_beginning(document, line);
    if (!fold || closed_at(line)) return false;
    const auto at = std::partition_point(closed_.begin(), closed_.end(),
                                         [&](const Closed& closed) { return closed.first_end.line < line; });
    closed_.insert(at, {.first_end = end_of(document.buffer(), fold->first),
                        .last_end = end_of(document.buffer(), fold->last)});
    rebuild();
    return true;
}

bool Folds::open(std::size_t line) {
    const std::size_t erased = std::erase_if(closed_, [&](const Closed& closed) { return closed.first_end.line == line; });
    if (erased == 0) return false;
    rebuild();
    return true;
}

bool Folds::open_row(std::size_t line) {
    bool opened = false;
    while (const std::optional<FoldRange> fold = closed_at(line)) {
        (void)open(line);
        opened = true;
        line = fold->last;
    }
    return opened;
}

bool Folds::reveal(std::size_t line) {
    const std::size_t erased = std::erase_if(closed_, [&](const Closed& closed) {
        return closed.first_end.line < line && line <= closed.last_end.line;
    });
    if (erased == 0) return false;
    rebuild();
    return true;
}

void Folds::close_all(const Document& document) {
    catch_up(document);
    for (const FoldRange& fold : document.folds()) {
        if (closed_at(fold.first)) continue;
        closed_.push_back({.first_end = end_of(document.buffer(), fold.first),
                           .last_end = end_of(document.buffer(), fold.last)});
    }
    std::ranges::sort(closed_, {}, [](const Closed& closed) { return closed.first_end.line; });
    rebuild();
}

void Folds::open_all() {
    closed_.clear();
    rebuild();
}

std::optional<FoldRange> Folds::closed_at(std::size_t line) const {
    const auto found = std::partition_point(closed_.begin(), closed_.end(),
                                            [&](const Closed& closed) { return closed.first_end.line < line; });
    if (found == closed_.end() || found->first_end.line != line) return std::nullopt;
    return found->range();
}

void Folds::catch_up(const Document& document) {
    const std::optional<uint64_t> seen = std::exchange(seen_, document.version());
    if (seen == document.version() || closed_.empty()) return;
    const auto edits = seen ? document.edits_since(*seen) : std::nullopt;
    if (!edits) {
        open_all();
        return;
    }
    for (const Edit& edit : *edits) {
        for (Closed& closed : closed_) {
            closed.first_end = map(edit, closed.first_end, Bias::Before);
            closed.last_end = map(edit, closed.last_end, Bias::Before);
        }
    }
    std::erase_if(closed_, [&](const Closed& closed) {
        const std::optional<FoldRange> fold = fold_beginning(document, closed.first_end.line);
        return !fold || fold->last != closed.last_end.line;
    });
    // Edits keep the folds in order, but two may now be one.
    std::ranges::sort(closed_, {}, [](const Closed& closed) { return closed.first_end.line; });
    const auto repeats = std::ranges::unique(closed_, {}, [](const Closed& closed) { return closed.first_end.line; });
    closed_.erase(repeats.begin(), repeats.end());
    rebuild();
}

void Folds::rebuild() {
    runs_.clear();
    std::size_t hidden = 0;
    for (const Closed& closed : closed_) {
        const FoldRange fold = closed.range();
        if (fold.last <= fold.first) continue;
        if (!runs_.empty() && fold.first <= runs_.back().last) {
            // Inside a closed fold, or folding on from where one ends.
            Run& run = runs_.back();
            if (fold.last > run.last) {
                hidden += fold.last - run.last;
                run.last = fold.last;
            }
            continue;
        }
        runs_.push_back({.first = fold.first, .last = fold.last, .hidden_before = hidden});
        hidden += fold.last - fold.first;
    }
}

const Folds::Run* Folds::run_before_or_at(std::size_t line) const {
    const auto after = std::partition_point(runs_.begin(), runs_.end(), [&](const Run& run) { return run.first < line; });
    return after == runs_.begin() ? nullptr : &*std::prev(after);
}

bool Folds::hidden(std::size_t line) const {
    const Run* run = run_before_or_at(line);
    return run != nullptr && line <= run->last;
}

std::size_t Folds::row_of(std::size_t line) const {
    const Run* run = run_before_or_at(line);
    if (run == nullptr) return line;
    if (line <= run->last) return run->first - run->hidden_before;
    return line - run->hidden_before - (run->last - run->first);
}

std::size_t Folds::line_of(std::size_t row) const {
    const auto after = std::partition_point(runs_.begin(), runs_.end(),
                                            [&](const Run& run) { return run.first - run.hidden_before < row; });
    if (after == runs_.begin()) return row;
    const Run& run = *std::prev(after);
    return row + run.hidden_before + (run.last - run.first);
}

std::size_t Folds::row_count(std::size_t line_count) const {
    if (runs_.empty()) return line_count;
    const Run& last = runs_.back();
    return line_count - (last.hidden_before + (last.last - last.first));
}

FoldRange Folds::row_lines(std::size_t line) const {
    const auto found = std::partition_point(runs_.begin(), runs_.end(), [&](const Run& run) { return run.first < line; });
    if (found == runs_.end() || found->first != line) return {.first = line, .last = line};
    return {.first = found->first, .last = found->last};
}

std::optional<std::size_t> Folds::line_below(const Buffer& buffer, std::size_t line, std::size_t count) const {
    const std::size_t row = row_of(line) + count;
    if (row >= row_count(buffer.line_count())) return std::nullopt;
    return line_of(row);
}

std::optional<std::size_t> Folds::line_above(std::size_t line, std::size_t count) const {
    const std::size_t row = row_of(line);
    if (count > row) return std::nullopt;
    return line_of(row - count);
}

void move_out_of_folds(Selections& selections, const Folds& folds, const Buffer& buffer) {
    std::vector<Selection> moved(selections.all().begin(), selections.all().end());
    bool any = false;
    for (Selection& selection : moved) {
        if (!folds.hidden(selection.caret.line)) continue;
        selection = Selection::at(end_of(buffer, folds.line_of(folds.row_of(selection.caret.line))));
        any = true;
    }
    if (any) selections.set(std::move(moved), selections.primary_index());
}

} // namespace fjedit
