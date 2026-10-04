#include "fjedit/core/syntax.hpp"

#include <optional>

namespace fjedit {

Syntax::Syntax(const Lexer& lexer, const Buffer& buffer) : lexer_(&lexer) {
    reset(buffer);
}

void Syntax::reset(const Buffer& buffer) {
    tokens_.assign(buffer.line_count(), {});
    ends_.assign(buffer.line_count(), {});
    for (std::size_t i = 0; i < buffer.line_count(); ++i) lex(buffer, i);
}

void Syntax::lex(const Buffer& buffer, std::size_t index) {
    static const LineState START;
    const LineState& start = index == 0 ? START : ends_[index - 1];
    tokens_[index].clear();
    ends_[index] = lexer_->lex(buffer.line(index), start, tokens_[index]);
}

void Syntax::edited(const Buffer& buffer, const Edit& edit) {
    if (edit.changes.empty()) return;

    // The lines from the first change to the last, as they were and as
    // they are now.
    const std::size_t first = edit.changes.front().span.from.line;
    const std::size_t old_last = edit.changes.back().span.to.line;
    const std::size_t new_last = spans_after(edit).back().to.line;
    const LineState old_end = ends_[old_last];

    const auto at = static_cast<std::ptrdiff_t>(first);
    tokens_.erase(tokens_.begin() + at, tokens_.begin() + static_cast<std::ptrdiff_t>(old_last) + 1);
    ends_.erase(ends_.begin() + at, ends_.begin() + static_cast<std::ptrdiff_t>(old_last) + 1);
    tokens_.insert(tokens_.begin() + at, new_last - first + 1, {});
    ends_.insert(ends_.begin() + at, new_last - first + 1, {});

    for (std::size_t line = first; line <= new_last; ++line) lex(buffer, line);
    if (ends_[new_last] == old_end) return;

    // The changed lines end in a different state: the lines after begin
    // differently, until one ends as it did before.
    for (std::size_t line = new_last + 1; line < buffer.line_count(); ++line) {
        const LineState before = ends_[line];
        lex(buffer, line);
        if (ends_[line] == before) break;
    }
}

std::vector<Token> overlaid(std::span<const Token> under, std::span<const Token> over) {
    std::vector<Token> out;
    out.reserve(under.size() + over.size());
    std::size_t next = 0;
    // What is left of the token of `under` that one of `over` cut into.
    std::optional<Token> rest;
    // Goes through `under` up to `end`, keeping what it passes or not, and
    // cutting the token `end` falls in.
    const auto pass = [&](std::size_t end, bool keep) {
        for (;;) {
            Token token;
            if (rest) {
                token = *rest;
            } else if (next < under.size()) {
                token = under[next];
            } else {
                return;
            }
            if (token.begin >= end) return;
            if (rest) {
                rest.reset();
            } else {
                ++next;
            }
            if (token.end > end) {
                if (keep) out.push_back({.begin = token.begin, .end = end, .kind = token.kind});
                rest = Token{.begin = end, .end = token.end, .kind = token.kind};
                return;
            }
            if (keep) out.push_back(token);
        }
    };
    for (const Token& top : over) {
        pass(top.begin, true);
        out.push_back(top);
        pass(top.end, false);
    }
    pass(static_cast<std::size_t>(-1), true);
    return out;
}

} // namespace fjedit
