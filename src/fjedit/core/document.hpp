#pragma once

// A file's text as the editor holds it, with everything that follows from
// the text: its undo history, its colours, its language and its indent.
// Every edit goes through the document, so what follows keeps step with the
// text; and it notes the edits it makes, so each view of it, and anything
// else holding a copy of the text (a language server), catches up from the
// version it last saw.
//
//     fjedit::Document document(file_text, fjedit::language_for(path));
//     const fjedit::Edit undo = document.apply(edit);
//     if (auto edits = document.edits_since(seen)) for (const fjedit::Edit& e : *edits) selections.map(e);

#include "fjedit/core/buffer.hpp"
#include "fjedit/core/history.hpp"
#include "fjedit/core/language.hpp"
#include "fjedit/core/structure.hpp"
#include "fjedit/core/syntax.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fjedit {

class Document {
public:
    /// `text` as its file holds it, in `language`, which must outlive the
    /// document; indented as the text shows, `fallback` where it doesn't say.
    Document(std::string_view text, const Language& language, Indent fallback = {});

    // Views and the syntax hold on to it: it stays where it was made.
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&&) = delete;
    Document& operator=(Document&&) = delete;

    [[nodiscard]] const Buffer& buffer() const { return buffer_; }
    [[nodiscard]] const Syntax& syntax() const { return syntax_; }
    [[nodiscard]] const Language& language() const { return *language_; }

    /// What can be undone. Commands record their edits in it; undo() and
    /// redo() below make them.
    [[nodiscard]] History& history() { return history_; }
    [[nodiscard]] const History& history() const { return history_; }

    [[nodiscard]] Indent indent() const { return indent_; }
    void set_indent(Indent indent) { indent_ = indent; }

    /// Counts the edits made; see Buffer::version().
    [[nodiscard]] uint64_t version() const { return buffer_.version(); }

    /// Makes `edit` (see Buffer::apply()), brings the colours up to date,
    /// notes it, and returns the edit that undoes it.
    [[nodiscard]] Edit apply(const Edit& edit);

    /// Undoes the last step done, or makes the last one undone again, and
    /// returns the selections it leaves; nothing when there is none.
    std::optional<Selections> undo();
    std::optional<Selections> redo();

    /// The edits made since the document was at `version`, in order; empty
    /// when it still is. Nothing when the document never was, or when its
    /// note of edits no longer reaches back that far: the one asking then
    /// starts again from the text as it is (Selections::clamp(), a copy
    /// sent whole).
    [[nodiscard]] std::optional<std::span<const Edit>> edits_since(uint64_t version) const;

    /// Where the text folds, by its language. Worked out when first asked
    /// for after an edit.
    [[nodiscard]] const std::vector<FoldRange>& folds() const;

    /// How many of the latest edits the document keeps a note of.
    static constexpr std::size_t JOURNAL = 4096;

private:
    Buffer buffer_;
    History history_;
    const Language* language_;
    Syntax syntax_;
    Indent indent_;

    // The latest edits made, in order: the first took the document from
    // version journal_start_ to the one after.
    std::vector<Edit> journal_;
    uint64_t journal_start_{0};

    mutable std::vector<FoldRange> folds_;
    mutable std::optional<uint64_t> folds_version_;
};

} // namespace fjedit
