#include "fjedit/core/commands.hpp"

#include "fjedit/core/find.hpp"
#include "fjedit/core/motion.hpp"
#include "fjedit/core/structure.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace fjedit {

namespace {

// ── Moving and extending ────────────────────────────────────────────────────

// A motion that needs only the text.
using Motion = Position (*)(const Buffer&, Position);

// Every selection's caret moved by `motion`; with `extend`, its anchor stays.
// A caret move ends the step typing was in.
void move(CommandContext& c, Motion motion, bool extend) {
    std::vector<Selection> moved;
    moved.reserve(c.selections.size());
    for (const Selection& selection : c.selections.all()) {
        const Position caret = motion(c.document.buffer(), selection.caret);
        moved.push_back({.anchor = extend ? selection.anchor : caret, .caret = caret});
    }
    c.selections.set(std::move(moved), c.selections.primary_index());
    c.document.history().close();
}

// One character left or right. Without extending, a selection gives way to
// its own end first, as a caret leaving it on that side.
void step(CommandContext& c, bool right, bool extend) {
    std::vector<Selection> moved;
    moved.reserve(c.selections.size());
    for (const Selection& selection : c.selections.all()) {
        Position caret;
        if (!extend && !selection.empty()) {
            caret = right ? selection.span().to : selection.span().from;
        } else {
            caret = right ? char_right(c.document.buffer(), selection.caret) : char_left(c.document.buffer(), selection.caret);
        }
        moved.push_back({.anchor = extend ? selection.anchor : caret, .caret = caret});
    }
    c.selections.set(std::move(moved), c.selections.primary_index());
    c.document.history().close();
}

// `lines` lines up or down, each caret keeping to its goal along the line.
// Without extending, a selection moves on from its end on that side.
void vertical(CommandContext& c, bool down, std::size_t lines, bool extend) {
    std::vector<Selection> moved;
    moved.reserve(c.selections.size());
    for (const Selection& selection : c.selections.all()) {
        const Position from = extend || selection.empty() ? selection.caret
                              : down                     ? selection.span().to
                                                         : selection.span().from;
        const float goal = selection.goal.value_or(c.layout.x_of(c.document.buffer(), from));
        const Position caret = down ? lines_down(c.document.buffer(), c.layout, from, goal, lines)
                                    : lines_up(c.document.buffer(), c.layout, from, goal, lines);
        moved.push_back({.anchor = extend ? selection.anchor : caret, .caret = caret, .goal = goal});
    }
    c.selections.set(std::move(moved), c.selections.primary_index());
    c.document.history().close();
}

Position text_start(const Buffer&, Position) {
    return {};
}

Position text_end(const Buffer& buffer, Position) {
    return buffer.end();
}

// ── Editing ─────────────────────────────────────────────────────────────────

// Makes `edit`, sets the selections `after` gives once it is made, one for
// each selection there was and in their order, and records the step.
template <typename After>
void commit(CommandContext& c, Edit edit, EditKind kind, After after) {
    if (edit.changes.empty()) return;
    const Selections before = c.selections;
    Edit undo = c.document.apply(edit);
    c.selections.set(after(edit), before.primary_index());
    c.document.history().record({.made = std::move(edit), .undo = std::move(undo), .before = before,
                      .after = c.selections, .kind = kind});
}

// Each selection replaced by its own text from `texts`, a caret after it.
void replace_each(CommandContext& c, const std::vector<std::string>& texts, EditKind kind) {
    Edit edit;
    std::size_t i = 0;
    for (const Selection& selection : c.selections.all()) edit.changes.push_back({selection.span(), texts[i++]});
    commit(c, std::move(edit), kind, [](const Edit& made) {
        std::vector<Selection> after;
        for (const Span& span : spans_after(made)) after.push_back(Selection::at(span.to));
        return after;
    });
}

// The spaces and tabs line `index` begins with.
std::string_view leading_space(const Buffer& buffer, std::size_t index) {
    const std::string_view line = buffer.line(index);
    std::size_t end = 0;
    while (end < line.size() && (line[end] == ' ' || line[end] == '\t')) ++end;
    return line.substr(0, end);
}

// One level of indent: a tab, or a level's worth of spaces.
std::string indent_level(const CommandContext& c) {
    const Indent indent = c.document.indent();
    return indent.tabs ? "\t" : std::string(indent.width, ' ');
}

// The spaces and tabs line `at.line` begins with, up to `at` when it is
// among them.
std::string_view indent_before(const Buffer& buffer, Position at) {
    const std::string_view line = buffer.line(at.line);
    std::size_t end = 0;
    while (end < line.size() && end < at.byte && (line[end] == ' ' || line[end] == '\t')) ++end;
    return line.substr(0, end);
}

// The closer `open` pairs with.
char closer_of(char open) {
    return open == '{' ? '}' : open == '[' ? ']' : ')';
}

// The opener the line's text before `at` ends in, past spaces; 0 when it
// ends in none, or in one in a string or comment.
char opener_before(const CommandContext& c, Position at) {
    const std::string_view line = c.document.buffer().line(at.line);
    std::size_t end = at.byte;
    while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t')) --end;
    if (end == 0) return 0;
    const char last = line[end - 1];
    if (last != '{' && last != '[' && last != '(') return 0;
    if (in_string_or_comment(c.document.syntax(), at.line, end - 1)) return 0;
    return last;
}

// Whether the line's text from `at` begins with `close`, past spaces.
bool closer_after(const Buffer& buffer, Position at, char close) {
    const std::string_view line = buffer.line(at.line);
    std::size_t first = at.byte;
    while (first < line.size() && (line[first] == ' ' || line[first] == '\t')) ++first;
    return first < line.size() && line[first] == close;
}

// Enter: a new line indented as the line it leaves, a level deeper after an
// opener; between an opener and its closer, the closer goes on a line of
// its own below.
void newline(CommandContext& c) {
    const std::string level = indent_level(c);
    Edit edit;
    std::vector<std::size_t> indents;
    for (const Selection& selection : c.selections.all()) {
        const Span span = selection.span();
        const std::string indent(indent_before(c.document.buffer(), span.from));
        const char open = opener_before(c, span.from);
        std::string text = "\n" + indent;
        if (open != 0) {
            text += level;
            if (closer_after(c.document.buffer(), span.to, closer_of(open))) text += "\n" + indent;
        }
        indents.push_back(indent.size() + (open != 0 ? level.size() : 0));
        edit.changes.push_back({.span = span, .text = std::move(text)});
    }
    commit(c, std::move(edit), EditKind::Typing, [&](const Edit& made) {
        const std::vector<Span> spans = spans_after(made);
        std::vector<Selection> after;
        for (std::size_t i = 0; i < spans.size(); ++i) {
            after.push_back(Selection::at({spans[i].from.line + 1, indents[i]}));
        }
        return after;
    });
}

// ── Pairs ───────────────────────────────────────────────────────────────────

// The token of line `line` that holds byte `byte`, or null.
const Token* token_at(const Syntax& syntax, std::size_t line, std::size_t byte) {
    const std::span<const Token> tokens = syntax.tokens(line);
    const auto after = std::upper_bound(tokens.begin(), tokens.end(), byte,
                                        [](std::size_t b, const Token& token) { return b < token.begin; });
    if (after == tokens.begin()) return nullptr;
    const Token& token = *(after - 1);
    return byte < token.end ? &token : nullptr;
}

// Whether a token of `kind` is text rather than code: a string or a comment.
bool text_kind(TokenKind kind) {
    return kind == TokenKind::String || kind == TokenKind::Comment;
}

// Whether `text`, a string's or a comment's token, closes it on its line: a
// string by the quote it opened with, unescaped; a comment by `*/` or `-->`.
bool closed(std::string_view text, TokenKind kind) {
    if (kind == TokenKind::Comment) return text.ends_with("*/") || text.ends_with("-->");
    const std::size_t quote = text.find_first_of("\"'");
    if (quote == std::string_view::npos || text.size() < quote + 2 || text.back() != text[quote]) return false;
    std::size_t escapes = 0;
    for (std::size_t i = text.size() - 1; i > quote + 1 && text[i - 1] == '\\'; --i) ++escapes;
    return escapes % 2 == 0;
}

// Whether a caret at `at` stands in a string or a comment: inside one, or at
// the end of one its line leaves open (a line comment, a string not closed
// yet).
bool in_text(const CommandContext& c, Position at) {
    const Syntax& syntax = c.document.syntax();
    if (const Token* token = token_at(syntax, at.line, at.byte); token && token->begin < at.byte && text_kind(token->kind)) {
        return true;
    }
    if (at.byte == 0) return false;
    const Token* before = token_at(syntax, at.line, at.byte - 1);
    if (before == nullptr || before->end != at.byte || !text_kind(before->kind)) return false;
    return !closed(c.document.buffer().line(at.line).substr(before->begin, before->end - before->begin), before->kind);
}

// The closer typing `text` pairs with: an opener's, or for one of the
// language's quotes, the quote; 0 for anything else.
char pair_closer(const Language& language, std::string_view text) {
    if (text.size() != 1) return 0;
    const char typed = text[0];
    if (typed == '(' || typed == '[' || typed == '{') return closer_of(typed);
    return language.quotes.find(typed) != std::string_view::npos ? typed : 0;
}

// Whether `typed`, an opener or a quote, brings its closer at `at`: before
// space, a closer, `;`, `,` or the line's end, and outside strings and
// comments. A quote doesn't just after a word (`it's`, `1'000`), unless the
// word is one of the language's string prefixes (`u8"`).
bool closes_at(const CommandContext& c, Position at, char typed) {
    const std::string_view line = c.document.buffer().line(at.line);
    if (at.byte < line.size() && std::string_view(" \t)]};,").find(line[at.byte]) == std::string_view::npos) return false;
    if (in_text(c, at)) return false;
    if (typed == '(' || typed == '[' || typed == '{') return true;
    const Language& language = c.document.language();
    std::size_t word = at.byte;
    while (word > 0 && word_byte(language, line[word - 1])) --word;
    if (word == at.byte) return true;
    return std::ranges::find(language.string_prefixes, line.substr(word, at.byte - word)) != language.string_prefixes.end();
}

// Whether `typed` at `at` steps over the same character just after it
// instead of typing another. A closer does when the brackets of its kind
// around `at` all have pairs, so another would close nothing; where one has
// none, the closer is typed and pairs it. A quote does at the end of the
// string `at` is in.
bool steps_over(const CommandContext& c, Position at, char typed) {
    const Buffer& buffer = c.document.buffer();
    const std::string_view line = buffer.line(at.line);
    if (at.byte >= line.size() || line[at.byte] != typed) return false;
    const Syntax& syntax = c.document.syntax();
    if (typed == ')' || typed == ']' || typed == '}') {
        if (in_string_or_comment(syntax, at.line, at.byte)) return false;
        const char open = typed == ')' ? '(' : typed == ']' ? '[' : '{';
        for (std::optional<Position> opener = enclosing_opener(buffer, &syntax, at, open); opener;
             opener = enclosing_opener(buffer, &syntax, *opener, open)) {
            if (!matching_bracket(buffer, syntax, *opener)) return false;
        }
        return true;
    }
    if (c.document.language().quotes.find(typed) == std::string_view::npos) return false;
    const Token* token = token_at(syntax, at.line, at.byte);
    if (token == nullptr || token->kind == TokenKind::Comment || token->end != at.byte + 1) return false;
    return line.find(typed, token->begin) < at.byte;
}

// The empty pair a caret at `at` stands in, which Backspace takes whole: an
// opener and the closer it pairs with (`(|)`), or an empty string (`"|"`).
std::optional<Span> empty_pair(const CommandContext& c, Position at) {
    const Buffer& buffer = c.document.buffer();
    const std::string_view line = buffer.line(at.line);
    if (at.byte == 0 || at.byte >= line.size()) return std::nullopt;
    const char before = line[at.byte - 1];
    const char after = line[at.byte];
    const Position open{at.line, at.byte - 1};
    const Span pair{open, {at.line, at.byte + 1}};
    if (before == '(' || before == '[' || before == '{') {
        if (after != closer_of(before)) return std::nullopt;
        const std::optional<BracketPair> found = matching_bracket(buffer, c.document.syntax(), open);
        if (!found || found->open != open || found->close != at) return std::nullopt;
        return pair;
    }
    if (before != after || c.document.language().quotes.find(before) == std::string_view::npos) return std::nullopt;
    const Token* token = token_at(c.document.syntax(), at.line, open.byte);
    if (token == nullptr || token->kind == TokenKind::Comment || token->begin != open.byte || token->end != at.byte + 1) {
        return std::nullopt;
    }
    return pair;
}

// Where a selection goes once its typing is made.
enum class Typed : uint8_t {
    /// After what was typed.
    After,
    /// Between an opener or a quote and the closer typed with it.
    Between,
    /// Over its text again, now wrapped in the pair.
    Wrapped,
    /// Past the closer or quote it stepped over, nothing typed.
    Over,
    /// After a closer moved back to its opener's indent.
    Indented,
};

struct Placed {
    Typed how{Typed::After};
    /// Where a caret that stepped over was.
    Position at{};
    /// How far along its line a closer moved back leaves the caret.
    std::size_t column{0};
    /// A wrapped selection's caret was at its start.
    bool backwards{false};
};

// Typing. With pairs closing, an opener or a quote also types its closer
// where closes_at() lets it and wraps a selection in the pair, and a closer
// or a quote steps over its like where steps_over() says. A closer typed
// where only spaces stand before it on its line goes back to the indent of
// the line its opener is on.
void type(CommandContext& c, std::string_view text) {
    const Buffer& buffer = c.document.buffer();
    const bool closer = text == "}" || text == "]" || text == ")";
    const char single = text.size() == 1 ? text[0] : '\0';
    const char pair = c.close_pairs ? pair_closer(c.document.language(), text) : '\0';
    Edit edit;
    std::vector<Placed> placed;
    for (const Selection& selection : c.selections.all()) {
        const Span span = selection.span();
        const Position at = span.from;
        if (c.close_pairs && selection.empty() && single != '\0' && steps_over(c, at, single)) {
            placed.push_back({.how = Typed::Over, .at = at});
            continue;
        }
        if (pair != '\0' && !selection.empty()) {
            edit.changes.push_back({.span = {span.from, span.from}, .text = std::string(text)});
            edit.changes.push_back({.span = {span.to, span.to}, .text = std::string(1, pair)});
            placed.push_back({.how = Typed::Wrapped, .backwards = selection.caret < selection.anchor});
            continue;
        }
        if (pair != '\0' && closes_at(c, at, single)) {
            edit.changes.push_back({.span = span, .text = std::string(text) + pair});
            placed.push_back({.how = Typed::Between});
            continue;
        }
        if (closer && selection.empty() && at.byte > 0 && indent_before(buffer, at).size() == at.byte) {
            const char open = single == '}' ? '{' : single == ']' ? '[' : '(';
            if (const std::optional<Position> opener = enclosing_opener(buffer, &c.document.syntax(), at, open)) {
                const std::string indent(leading_space(buffer, opener->line));
                edit.changes.push_back({.span = {{at.line, 0}, at}, .text = indent + std::string(text)});
                placed.push_back({.how = Typed::Indented, .column = indent.size() + 1});
                continue;
            }
        }
        edit.changes.push_back({.span = span, .text = std::string(text)});
        placed.push_back({});
    }
    // Every caret stepped over: they move on, and the typing stays in the
    // step it was in.
    if (edit.changes.empty()) {
        std::vector<Selection> after;
        for (const Placed& p : placed) after.push_back(Selection::at({p.at.line, p.at.byte + 1}));
        c.selections.set(std::move(after), c.selections.primary_index());
        return;
    }
    commit(c, std::move(edit), EditKind::Typing, [&](const Edit& made) {
        const std::vector<Span> spans = spans_after(made);
        std::vector<Selection> after;
        std::size_t change = 0;
        for (const Placed& p : placed) {
            switch (p.how) {
                case Typed::After: after.push_back(Selection::at(spans[change++].to)); break;
                case Typed::Between: {
                    const Position end = spans[change++].to;
                    after.push_back(Selection::at({end.line, end.byte - 1}));
                    break;
                }
                case Typed::Wrapped: {
                    const Position from = spans[change].to;
                    const Position to = spans[change + 1].from;
                    change += 2;
                    after.push_back(p.backwards ? Selection{.anchor = to, .caret = from} : Selection{.anchor = from, .caret = to});
                    break;
                }
                case Typed::Over: after.push_back(Selection::at(map(made, {p.at.line, p.at.byte + 1}, Bias::After))); break;
                case Typed::Indented: after.push_back(Selection::at({spans[change++].from.line, p.column})); break;
            }
        }
        return after;
    });
}

// Deletes each selection, or for a caret, what `reach` gives from it: the
// text a caret deletes, reaching from where it is (`Span(const Buffer&,
// Position)`). Spans that overlap are deleted as one, and each caret ends
// where its own span began.
template <typename Reach>
void erase(CommandContext& c, Reach reach, EditKind kind = EditKind::Deleting) {
    std::vector<Span> spans;
    for (const Selection& selection : c.selections.all()) {
        spans.push_back(selection.empty() ? reach(c.document.buffer(), selection.caret) : selection.span());
    }

    std::vector<Span> joined = spans;
    std::sort(joined.begin(), joined.end(), [](const Span& a, const Span& b) { return a.from < b.from; });
    Edit edit;
    for (const Span& span : joined) {
        if (span.empty()) continue;
        if (!edit.changes.empty() && span.from < edit.changes.back().span.to) {
            edit.changes.back().span.to = std::max(edit.changes.back().span.to, span.to);
        } else {
            edit.changes.push_back({.span = span, .text = ""});
        }
    }
    commit(c, std::move(edit), kind, [&](const Edit& made) {
        std::vector<Selection> after;
        for (const Span& span : spans) after.push_back(Selection::at(map(made, span.from, Bias::Before)));
        return after;
    });
}

Span to_char_left(const Buffer& buffer, Position at) {
    return {char_left(buffer, at), at};
}

Span to_char_right(const Buffer& buffer, Position at) {
    return {at, char_right(buffer, at)};
}

Span to_word_left(const Buffer& buffer, Position at) {
    return {word_left(buffer, at), at};
}

Span to_word_right(const Buffer& buffer, Position at) {
    return {at, word_right(buffer, at)};
}

// Backspace: the character before each caret, or with pairs closing, an
// empty pair whole (empty_pair()).
void backspace(CommandContext& c) {
    erase(c, [&](const Buffer& buffer, Position at) {
        const std::optional<Span> pair = c.close_pairs ? empty_pair(c, at) : std::nullopt;
        return pair ? *pair : to_char_left(buffer, at);
    });
}

// Lines `first` to `last`, both counted.
struct LineRange {
    std::size_t first{0};
    std::size_t last{0};
};

// The lines the selections cover, in order, those that overlap or follow on
// joined. A selection ending at the very start of a line leaves that line out.
std::vector<LineRange> covered_lines(const Selections& selections) {
    std::vector<LineRange> ranges;
    for (const Selection& selection : selections.all()) {
        const Span span = selection.span();
        LineRange range{.first = span.from.line, .last = span.to.line};
        if (!span.empty() && span.to.byte == 0 && span.to.line > span.from.line) --range.last;
        if (!ranges.empty() && range.first <= ranges.back().last + 1) {
            ranges.back().last = std::max(ranges.back().last, range.last);
        } else {
            ranges.push_back(range);
        }
    }
    return ranges;
}

// The whole of lines `range`, without the break after the last.
std::string block_text(const Buffer& buffer, LineRange range) {
    return buffer.text({{range.first, 0}, {range.last, buffer.line(range.last).size()}});
}

void delete_lines(CommandContext& c) {
    const std::vector<LineRange> ranges = covered_lines(c.selections);
    Edit edit;
    for (const LineRange& range : ranges) {
        if (range.last + 1 < c.document.buffer().line_count()) {
            edit.changes.push_back({.span = {{range.first, 0}, {range.last + 1, 0}}, .text = ""});
        } else if (range.first > 0) {
            edit.changes.push_back({.span = {{range.first - 1, c.document.buffer().line(range.first - 1).size()},
                                             {range.last, c.document.buffer().line(range.last).size()}},
                                    .text = ""});
        } else {
            edit.changes.push_back({.span = {{0, 0}, c.document.buffer().end()}, .text = ""});
        }
    }
    // Each caret goes to the line that takes its line's place, as far along
    // as that line allows.
    std::vector<Selection> carets(c.selections.all().begin(), c.selections.all().end());
    commit(c, std::move(edit), EditKind::Other, [&](const Edit& made) {
        std::vector<Selection> after;
        for (const Selection& selection : carets) {
            const Position line_start = map(made, {selection.caret.line, 0}, Bias::Before);
            after.push_back(Selection::at(c.document.buffer().clamp({line_start.line, selection.caret.byte})));
        }
        return after;
    });
}

// One level of indent put in at `at`: a tab, or spaces on to the next stop.
std::string indent_at(const CommandContext& c, Position at) {
    const Indent indent = c.document.indent();
    if (indent.tabs) return "\t";
    const std::size_t column = column_of(c.document.buffer().line(at.line), at.byte, indent.width);
    return std::string(indent.width - column % indent.width, ' ');
}

// Each selection carried through an indent or outdent: an end at a line's
// very start stays there, any other moves with the line's text.
std::vector<Selection> through_indent(const std::vector<Selection>& selections, const Edit& made) {
    auto carry = [&](Position at) { return map(made, at, at.byte == 0 ? Bias::Before : Bias::After); };
    std::vector<Selection> after;
    for (const Selection& selection : selections) {
        after.push_back({.anchor = carry(selection.anchor), .caret = carry(selection.caret)});
    }
    return after;
}

// Tab: across lines, every line that has text gains a level of indent;
// within a line, the selection or caret gives way to one.
void tab(CommandContext& c) {
    const bool lines = std::any_of(c.selections.all().begin(), c.selections.all().end(),
                                   [](const Selection& s) { return s.span().from.line != s.span().to.line; });
    if (!lines) {
        std::vector<std::string> texts;
        for (const Selection& selection : c.selections.all()) texts.push_back(indent_at(c, selection.span().from));
        replace_each(c, texts, EditKind::Typing);
        return;
    }

    const std::string level = indent_level(c);
    Edit edit;
    for (const LineRange& range : covered_lines(c.selections)) {
        for (std::size_t line = range.first; line <= range.last; ++line) {
            if (!c.document.buffer().line(line).empty()) edit.changes.push_back({.span = {{line, 0}, {line, 0}}, .text = level});
        }
    }
    const std::vector<Selection> selections(c.selections.all().begin(), c.selections.all().end());
    commit(c, std::move(edit), EditKind::Other, [&](const Edit& made) { return through_indent(selections, made); });
}

// Every line the selections touch loses a level of indent: a tab, or up to
// a level's worth of spaces.
void outdent(CommandContext& c) {
    const std::size_t width = c.document.indent().width;
    Edit edit;
    for (const LineRange& range : covered_lines(c.selections)) {
        for (std::size_t line = range.first; line <= range.last; ++line) {
            const std::string_view text = c.document.buffer().line(line);
            std::size_t end = 0;
            if (!text.empty() && text[0] == '\t') {
                end = 1;
            } else {
                while (end < text.size() && end < width && text[end] == ' ') ++end;
            }
            if (end > 0) edit.changes.push_back({.span = {{line, 0}, {line, end}}, .text = ""});
        }
    }
    const std::vector<Selection> selections(c.selections.all().begin(), c.selections.all().end());
    commit(c, std::move(edit), EditKind::Other, [&](const Edit& made) { return through_indent(selections, made); });
}

// The lines the selections cover, swapped with the line above or below them;
// nothing moves when a block is already at the top or bottom.
void move_lines(CommandContext& c, bool down) {
    const std::vector<LineRange> ranges = covered_lines(c.selections);
    if (!down && ranges.front().first == 0) return;
    if (down && ranges.back().last + 1 >= c.document.buffer().line_count()) return;

    Edit edit;
    for (const LineRange& range : ranges) {
        const std::string block = block_text(c.document.buffer(), range);
        if (down) {
            const std::size_t below = range.last + 1;
            edit.changes.push_back({.span = {{range.first, 0}, {below, c.document.buffer().line(below).size()}},
                                    .text = std::string(c.document.buffer().line(below)) + "\n" + block});
        } else {
            const std::size_t above = range.first - 1;
            edit.changes.push_back({.span = {{above, 0}, {range.last, c.document.buffer().line(range.last).size()}},
                                    .text = block + "\n" + std::string(c.document.buffer().line(above))});
        }
    }
    const std::vector<Selection> selections(c.selections.all().begin(), c.selections.all().end());
    commit(c, std::move(edit), EditKind::Other, [&](const Edit&) {
        std::vector<Selection> after;
        for (Selection selection : selections) {
            selection.anchor.line = down ? selection.anchor.line + 1 : selection.anchor.line - 1;
            selection.caret.line = down ? selection.caret.line + 1 : selection.caret.line - 1;
            selection.goal.reset();
            after.push_back(selection);
        }
        return after;
    });
}

// The lines the selections cover, copied below themselves, the selections
// going with the copy; or above, the selections staying on the upper copy.
void copy_lines(CommandContext& c, bool down) {
    const std::vector<LineRange> ranges = covered_lines(c.selections);
    Edit edit;
    for (const LineRange& range : ranges) {
        const std::string block = block_text(c.document.buffer(), range);
        if (down) {
            const Position end{range.last, c.document.buffer().line(range.last).size()};
            edit.changes.push_back({.span = {end, end}, .text = "\n" + block});
        } else {
            edit.changes.push_back({.span = {{range.first, 0}, {range.first, 0}}, .text = block + "\n"});
        }
    }
    const std::vector<Selection> selections(c.selections.all().begin(), c.selections.all().end());
    commit(c, std::move(edit), EditKind::Other, [&](const Edit&) {
        // Lines added above a selection: every block before its own, and its
        // own when the copy went below it.
        std::vector<Selection> after;
        for (Selection selection : selections) {
            std::size_t added = 0;
            for (const LineRange& range : ranges) {
                const std::size_t size = range.last - range.first + 1;
                if (range.last < selection.span().from.line) {
                    added += size;
                } else if (down && range.first <= selection.span().from.line) {
                    added += size;
                }
            }
            selection.anchor.line += added;
            selection.caret.line += added;
            selection.goal.reset();
            after.push_back(selection);
        }
        return after;
    });
}

// A new line below or above each caret's line, indented as that line is,
// the caret on it.
void insert_line(CommandContext& c, bool below) {
    std::vector<std::size_t> lines;
    for (const Selection& selection : c.selections.all()) {
        if (lines.empty() || lines.back() != selection.caret.line) lines.push_back(selection.caret.line);
    }
    Edit edit;
    for (const std::size_t line : lines) {
        const std::string indent(leading_space(c.document.buffer(), line));
        if (below) {
            const Position end{line, c.document.buffer().line(line).size()};
            edit.changes.push_back({.span = {end, end}, .text = "\n" + indent});
        } else {
            edit.changes.push_back({.span = {{line, 0}, {line, 0}}, .text = indent + "\n"});
        }
    }
    commit(c, std::move(edit), EditKind::Other, [&](const Edit& made) {
        const std::vector<Span> spans = spans_after(made);
        std::vector<Selection> after;
        for (const Selection& selection : c.selections.all()) {
            const std::size_t i = static_cast<std::size_t>(
                std::find(lines.begin(), lines.end(), selection.caret.line) - lines.begin());
            // Below, the new line ends the inserted text; above, it begins it,
            // the caret after its indent (the text less its break).
            const std::size_t indent = made.changes[i].text.size() - 1;
            after.push_back(Selection::at(below ? spans[i].to : Position{spans[i].from.line, indent}));
        }
        return after;
    });
}

// What copy and cut take: the selected text, or with nothing selected the
// carets' whole lines, each with its break.
std::string copied(const CommandContext& c) {
    const bool carets = std::all_of(c.selections.all().begin(), c.selections.all().end(),
                                    [](const Selection& s) { return s.empty(); });
    std::string text;
    if (carets) {
        std::size_t last = std::numeric_limits<std::size_t>::max();
        for (const Selection& selection : c.selections.all()) {
            if (selection.caret.line == last) continue;
            last = selection.caret.line;
            text += c.document.buffer().line(last);
            text += '\n';
        }
        return text;
    }
    bool first = true;
    for (const Selection& selection : c.selections.all()) {
        if (selection.empty()) continue;
        if (!first) text += '\n';
        text += c.document.buffer().text(selection.span());
        first = false;
    }
    return text;
}

void copy(CommandContext& c) {
    if (c.clipboard != nullptr && c.clipboard->set) c.clipboard->set(copied(c));
}

void cut(CommandContext& c) {
    if (c.clipboard == nullptr || !c.clipboard->set) return;
    copy(c);
    const bool carets = std::all_of(c.selections.all().begin(), c.selections.all().end(),
                                    [](const Selection& s) { return s.empty(); });
    if (carets) {
        delete_lines(c);
    } else {
        erase(c, [](const Buffer&, Position at) { return Span{at, at}; }, EditKind::Other);
    }
}

// The clipboard's text at every selection; with as many lines as there are
// selections, one line at each.
void paste(CommandContext& c) {
    if (c.clipboard == nullptr || !c.clipboard->get) return;
    const std::string text = c.clipboard->get();
    if (text.empty()) return;

    std::vector<std::string> lines;
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            lines.emplace_back(text.substr(begin, i - begin));
            begin = i + 1;
        }
    }
    if (!text.empty() && text.back() == '\n') lines.pop_back();
    for (std::string& line : lines) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
    }

    const bool spread = c.selections.size() > 1 && lines.size() == c.selections.size();
    replace_each(c, spread ? lines : std::vector<std::string>(c.selections.size(), text), EditKind::Other);
}

void undo(CommandContext& c) {
    if (std::optional<Selections> restored = c.document.undo()) c.selections = *restored;
}

void redo(CommandContext& c) {
    if (std::optional<Selections> restored = c.document.redo()) c.selections = *restored;
}

// ── Selecting more ──────────────────────────────────────────────────────────

void select_all(CommandContext& c) {
    c.selections = Selections({.anchor = {}, .caret = c.document.buffer().end()});
    c.document.history().close();
}

// Each selection grown to its whole lines, through the break after them, and
// on by a line each time again.
void select_line(CommandContext& c) {
    std::vector<Selection> grown;
    for (const Selection& selection : c.selections.all()) {
        const Span span = selection.span();
        const Position to = span.to.line + 1 < c.document.buffer().line_count() ? Position{span.to.line + 1, 0} : c.document.buffer().end();
        grown.push_back({.anchor = {span.from.line, 0}, .caret = to});
    }
    c.selections.set(std::move(grown), c.selections.primary_index());
    c.document.history().close();
}

bool all_carets(const Selections& selections) {
    return std::all_of(selections.all().begin(), selections.all().end(), [](const Selection& s) { return s.empty(); });
}

// The next match: carets first take the words they are in; from then on,
// the next place the primary's text occurs that isn't selected yet, after
// the primary and round from the start, is added as the primary.
void next_match(CommandContext& c) {
    if (all_carets(c.selections)) {
        std::vector<Selection> words;
        for (const Selection& selection : c.selections.all()) {
            const Span word = word_at(c.document.buffer(), selection.caret);
            words.push_back(word.empty() ? selection : Selection{.anchor = word.from, .caret = word.to});
        }
        c.selections.set(std::move(words), c.selections.primary_index());
        c.document.history().close();
        return;
    }

    const Selection primary = c.selections.primary();
    if (primary.empty()) return;
    const std::vector<Span> matches = find_all(c.document.buffer(), c.document.buffer().text(primary.span()));
    auto selected = [&](const Span& match) {
        return std::any_of(c.selections.all().begin(), c.selections.all().end(),
                           [&](const Selection& s) { return s.span() == match; });
    };
    const Span* next = nullptr;
    for (const Span& match : matches) {
        if (match.from >= primary.span().to && !selected(match)) {
            next = &match;
            break;
        }
    }
    for (const Span& match : matches) {
        if (next == nullptr && !selected(match)) next = &match;
    }
    if (next == nullptr) return;
    c.selections.add({.anchor = next->from, .caret = next->to});
    c.document.history().close();
}

// Every place the primary's text occurs, or with nothing selected, every
// whole word like the one at the primary caret.
void all_matches(CommandContext& c) {
    const Selection primary = c.selections.primary();
    const Span target = primary.empty() ? word_at(c.document.buffer(), primary.caret) : primary.span();
    if (target.empty()) return;
    const std::vector<Span> matches =
        find_all(c.document.buffer(), c.document.buffer().text(target), {.match_case = true, .whole_word = primary.empty()});
    if (matches.empty()) return;

    std::vector<Selection> selections;
    std::size_t keep = 0;
    for (std::size_t i = 0; i < matches.size(); ++i) {
        selections.push_back({.anchor = matches[i].from, .caret = matches[i].to});
        if (matches[i] == target) keep = i;
    }
    c.selections.set(std::move(selections), keep);
    c.document.history().close();
}

// A caret added on the line above the first selection, or below the last,
// as far along as that selection's caret.
void add_cursor(CommandContext& c, bool below) {
    const Selection edge = below ? c.selections.all().back() : c.selections.all().front();
    if (below ? edge.caret.line + 1 >= c.document.buffer().line_count() : edge.caret.line == 0) return;
    const float goal = edge.goal.value_or(c.layout.x_of(c.document.buffer(), edge.caret));
    const Position at = below ? lines_down(c.document.buffer(), c.layout, edge.caret, goal, 1)
                              : lines_up(c.document.buffer(), c.layout, edge.caret, goal, 1);
    c.selections.add({.anchor = at, .caret = at, .goal = goal});
    c.document.history().close();
}

// Escape: several selections give way to the primary, one to its caret.
void single(CommandContext& c) {
    if (c.selections.size() > 1) {
        c.selections.keep_primary();
    } else if (!c.selections.primary().empty()) {
        c.selections = Selections(Selection::at(c.selections.primary().caret));
    }
    c.document.history().close();
}

// ── Folding ─────────────────────────────────────────────────────────────────

// The innermost fold around `line` that is open: of those beginning on it or
// before and ending on it or after, the one beginning last.
std::optional<FoldRange> open_fold_around(const CommandContext& c, std::size_t line) {
    std::optional<FoldRange> around;
    for (const FoldRange& fold : c.document.folds()) {
        if (fold.first > line) break;
        if (fold.last >= line && !c.folds->closed_at(fold.first)) around = fold;
    }
    return around;
}

// At each caret, the innermost open fold around it closed; carets it hides
// go to its first line's end.
void close_fold(CommandContext& c) {
    if (c.folds == nullptr) return;
    for (const Selection& selection : c.selections.all()) {
        if (const std::optional<FoldRange> fold = open_fold_around(c, selection.caret.line)) {
            (void)c.folds->close(c.document, fold->first);
        }
    }
    move_out_of_folds(c.selections, *c.folds, c.document.buffer());
}

// At each caret, the folds closed on its row opened.
void open_fold(CommandContext& c) {
    if (c.folds == nullptr) return;
    for (const Selection& selection : c.selections.all()) (void)c.folds->open_row(selection.caret.line);
}

// At each caret, its row's folds opened, or where none is closed, the
// innermost fold around it closed.
void toggle_fold(CommandContext& c) {
    if (c.folds == nullptr) return;
    for (const Selection& selection : c.selections.all()) {
        if (c.folds->open_row(selection.caret.line)) continue;
        if (const std::optional<FoldRange> fold = open_fold_around(c, selection.caret.line)) {
            (void)c.folds->close(c.document, fold->first);
        }
    }
    move_out_of_folds(c.selections, *c.folds, c.document.buffer());
}

void close_all_folds(CommandContext& c) {
    if (c.folds == nullptr) return;
    c.folds->close_all(c.document);
    move_out_of_folds(c.selections, *c.folds, c.document.buffer());
}

void open_all_folds(CommandContext& c) {
    if (c.folds != nullptr) c.folds->open_all();
}

const std::array COMMANDS = {
    Command{"caret.left", "Move left", [](CommandContext& c) { step(c, false, false); }},
    Command{"caret.right", "Move right", [](CommandContext& c) { step(c, true, false); }},
    Command{"caret.up", "Move up", [](CommandContext& c) { vertical(c, false, 1, false); }},
    Command{"caret.down", "Move down", [](CommandContext& c) { vertical(c, true, 1, false); }},
    Command{"caret.word_left", "Move to the word before", [](CommandContext& c) { move(c, word_left, false); }},
    Command{"caret.word_right", "Move to the word after", [](CommandContext& c) { move(c, word_right, false); }},
    Command{"caret.line_start", "Move to the line's start", [](CommandContext& c) { move(c, line_start, false); }},
    Command{"caret.line_end", "Move to the line's end", [](CommandContext& c) { move(c, line_end, false); }},
    Command{"caret.page_up", "Move a page up",
            [](CommandContext& c) { vertical(c, false, c.layout.page_lines(), false); }},
    Command{"caret.page_down", "Move a page down",
            [](CommandContext& c) { vertical(c, true, c.layout.page_lines(), false); }},
    Command{"caret.text_start", "Move to the start of the file", [](CommandContext& c) { move(c, text_start, false); }},
    Command{"caret.text_end", "Move to the end of the file", [](CommandContext& c) { move(c, text_end, false); }},

    Command{"select.left", "Select left", [](CommandContext& c) { step(c, false, true); }},
    Command{"select.right", "Select right", [](CommandContext& c) { step(c, true, true); }},
    Command{"select.up", "Select up", [](CommandContext& c) { vertical(c, false, 1, true); }},
    Command{"select.down", "Select down", [](CommandContext& c) { vertical(c, true, 1, true); }},
    Command{"select.word_left", "Select to the word before", [](CommandContext& c) { move(c, word_left, true); }},
    Command{"select.word_right", "Select to the word after", [](CommandContext& c) { move(c, word_right, true); }},
    Command{"select.line_start", "Select to the line's start", [](CommandContext& c) { move(c, line_start, true); }},
    Command{"select.line_end", "Select to the line's end", [](CommandContext& c) { move(c, line_end, true); }},
    Command{"select.page_up", "Select a page up",
            [](CommandContext& c) { vertical(c, false, c.layout.page_lines(), true); }},
    Command{"select.page_down", "Select a page down",
            [](CommandContext& c) { vertical(c, true, c.layout.page_lines(), true); }},
    Command{"select.text_start", "Select to the start of the file",
            [](CommandContext& c) { move(c, text_start, true); }},
    Command{"select.text_end", "Select to the end of the file", [](CommandContext& c) { move(c, text_end, true); }},
    Command{"select.all", "Select all", select_all},
    Command{"select.line", "Select the line", select_line},
    Command{"select.next_match", "Select the next match", next_match},
    Command{"select.all_matches", "Select every match", all_matches},

    Command{"cursor.add_above", "Add a cursor above", [](CommandContext& c) { add_cursor(c, false); }},
    Command{"cursor.add_below", "Add a cursor below", [](CommandContext& c) { add_cursor(c, true); }},
    Command{"cursor.single", "Leave one cursor", single},

    Command{"edit.type", "Type", [](CommandContext& c) { type(c, c.text); }},
    Command{"edit.newline", "New line", newline},
    Command{"edit.tab", "Indent", tab},
    Command{"edit.outdent", "Outdent", outdent},
    Command{"edit.backspace", "Delete the character before", backspace},
    Command{"edit.delete", "Delete the character after", [](CommandContext& c) { erase(c, to_char_right); }},
    Command{"edit.delete_word_left", "Delete the word before", [](CommandContext& c) { erase(c, to_word_left); }},
    Command{"edit.delete_word_right", "Delete the word after", [](CommandContext& c) { erase(c, to_word_right); }},
    Command{"edit.delete_lines", "Delete lines", delete_lines},
    Command{"edit.move_lines_up", "Move lines up", [](CommandContext& c) { move_lines(c, false); }},
    Command{"edit.move_lines_down", "Move lines down", [](CommandContext& c) { move_lines(c, true); }},
    Command{"edit.copy_lines_up", "Copy lines up", [](CommandContext& c) { copy_lines(c, false); }},
    Command{"edit.copy_lines_down", "Copy lines down", [](CommandContext& c) { copy_lines(c, true); }},
    Command{"edit.insert_line_below", "Insert a line below", [](CommandContext& c) { insert_line(c, true); }},
    Command{"edit.insert_line_above", "Insert a line above", [](CommandContext& c) { insert_line(c, false); }},
    Command{"edit.cut", "Cut", cut},
    Command{"edit.copy", "Copy", copy},
    Command{"edit.paste", "Paste", paste},
    Command{"edit.undo", "Undo", undo},
    Command{"edit.redo", "Redo", redo},

    Command{"fold.close", "Fold", close_fold},
    Command{"fold.open", "Unfold", open_fold},
    Command{"fold.toggle", "Fold or unfold", toggle_fold},
    Command{"fold.close_all", "Fold everything", close_all_folds},
    Command{"fold.open_all", "Unfold everything", open_all_folds},
};

} // namespace

std::span<const Command> commands() {
    return COMMANDS;
}

const Command* find_command(std::string_view name) {
    const auto found = std::find_if(COMMANDS.begin(), COMMANDS.end(), [&](const Command& c) { return c.name == name; });
    return found == COMMANDS.end() ? nullptr : &*found;
}

std::vector<Span> apply_edit(CommandContext& context, Edit edit, std::optional<Position> caret) {
    if (edit.changes.empty()) return {};
    const Selections before = context.selections;
    Edit undo = context.document.apply(edit);
    if (caret) {
        context.selections = Selections(Selection::at(context.document.buffer().clamp(*caret)));
    } else {
        context.selections.map(edit);
    }
    std::vector<Span> made = spans_after(edit);
    context.document.history().close();
    context.document.history().record({.made = std::move(edit), .undo = std::move(undo), .before = before,
                                       .after = context.selections, .kind = EditKind::Other});
    context.document.history().close();
    return made;
}

std::vector<Span> replace_matches(CommandContext& context, std::span<const Span> matches, std::string_view needle,
                                  std::string_view with, FindOptions options) {
    if (matches.empty()) return {};
    const Buffer& buffer = context.document.buffer();
    Edit edit;
    for (const Span& match : matches) edit.changes.push_back({match, replacement(buffer, match, needle, with, options)});
    return apply_edit(context, std::move(edit));
}

Selections column_selection(const Buffer& buffer, const Layout& layout, std::size_t anchor_line, float anchor_x,
                             std::size_t caret_line, float caret_x) {
    const std::size_t first = std::min(anchor_line, caret_line);
    const std::size_t last = std::max(anchor_line, caret_line);
    const float left = std::min(anchor_x, caret_x);

    std::vector<Selection> selections;
    std::size_t primary = 0;
    for (std::optional<std::size_t> shown = first; shown && *shown <= last; shown = layout.line_below(buffer, *shown, 1)) {
        const std::size_t line = *shown;
        if (line != caret_line && layout.x_of(buffer, {line, buffer.line(line).size()}) < left) continue;
        if (line == caret_line) primary = selections.size();
        selections.push_back({.anchor = layout.position_at(buffer, line, anchor_x),
                              .caret = layout.position_at(buffer, line, caret_x)});
    }
    Selections out;
    out.set(std::move(selections), primary);
    return out;
}

namespace {

constexpr std::array VIEW_COMMANDS = {
    ViewCommand{"find.open", "Find"},
    ViewCommand{"find.replace", "Replace"},
    ViewCommand{"find.next", "Find next"},
    ViewCommand{"find.previous", "Find previous"},
    ViewCommand{"find.close", "Close find"},
    ViewCommand{"goto.open", "Go to line"},
    ViewCommand{"completion.open", "Complete"},
    ViewCommand{"completion.next", "Next completion"},
    ViewCommand{"completion.previous", "Previous completion"},
    ViewCommand{"completion.page_down", "Completions a page down"},
    ViewCommand{"completion.page_up", "Completions a page up"},
    ViewCommand{"completion.accept", "Accept completion"},
    ViewCommand{"completion.close", "Close completions"},
    ViewCommand{"definition.go", "Go to definition"},
    ViewCommand{"hover.show", "Show what is at the caret"},
    ViewCommand{"symbol.rename", "Rename"},
    ViewCommand{"format.document", "Format"},
};

} // namespace

std::span<const ViewCommand> view_commands() {
    return VIEW_COMMANDS;
}

bool is_view_command(std::string_view name) {
    return std::ranges::any_of(VIEW_COMMANDS, [&](const ViewCommand& command) { return command.name == name; });
}

bool run_command(std::string_view name, CommandContext& context) {
    const Command* command = find_command(name);
    if (command == nullptr) return false;
    command->run(context);
    return true;
}

} // namespace fjedit
