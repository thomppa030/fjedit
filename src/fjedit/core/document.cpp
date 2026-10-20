#include "fjedit/core/document.hpp"

namespace fjedit {

Document::Document(std::string_view text, const Language& language, Indent fallback)
    : buffer_(text), language_(&language), syntax_(language.lexer, buffer_), indent_(detect_indent(buffer_, fallback)),
      journal_start_(buffer_.version()) {}

Edit Document::apply(const Edit& edit) {
    Edit undo = buffer_.apply(edit);
    syntax_.edited(buffer_, edit);
    for (auto& [name, at] : marks_) at = map(edit, at, Bias::Before);

    journal_.push_back(edit);
    // Trimmed in batches, so most edits move nothing.
    if (journal_.size() >= 2 * JOURNAL) {
        const std::size_t drop = journal_.size() - JOURNAL;
        journal_.erase(journal_.begin(), journal_.begin() + static_cast<std::ptrdiff_t>(drop));
        journal_start_ += drop;
    }
    return undo;
}

std::optional<Position> Document::mark(char name) const {
    const auto found = marks_.find(name);
    if (found == marks_.end()) return std::nullopt;
    return buffer_.clamp(found->second);
}

std::optional<Selections> Document::undo() {
    std::optional<History::Move> move = history_.undo();
    if (!move) return std::nullopt;
    for (const Edit& edit : move->edits) (void)apply(edit);
    return std::move(move->selections);
}

std::optional<Selections> Document::take_back(std::size_t steps) {
    std::optional<History::Move> move = history_.take_back(steps);
    if (!move) return std::nullopt;
    for (const Edit& edit : move->edits) (void)apply(edit);
    return std::move(move->selections);
}

std::optional<Selections> Document::redo() {
    std::optional<History::Move> move = history_.redo();
    if (!move) return std::nullopt;
    for (const Edit& edit : move->edits) (void)apply(edit);
    return std::move(move->selections);
}

std::optional<std::span<const Edit>> Document::edits_since(uint64_t version) const {
    if (version < journal_start_ || version > buffer_.version()) return std::nullopt;
    return std::span<const Edit>(journal_).subspan(static_cast<std::size_t>(version - journal_start_));
}

const std::vector<FoldRange>& Document::folds() const {
    if (folds_version_ != buffer_.version()) {
        folds_ = fold_ranges(buffer_, syntax_, language_->folding, indent_.width);
        folds_version_ = buffer_.version();
    }
    return folds_;
}

} // namespace fjedit
