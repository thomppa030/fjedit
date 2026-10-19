#include "fjedit/core/vim/operators.hpp"

#include "fjedit/core/layout.hpp"
#include "fjedit/core/structure.hpp"

#include <algorithm>
#include <charconv>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fjedit::vim {

namespace {

bool blank(char c) {
    return c == ' ' || c == '\t';
}

// Where `line`'s indent ends.
std::size_t indent_end(std::string_view line) {
    std::size_t end = 0;
    while (end < line.size() && blank(line[end])) ++end;
    return end;
}

Range lines(Position from, Position to) {
    return {.span = {from, to}, .linewise = true};
}

// An exclusive span as an operator takes it (Vim's do_pending_operator()):
// ending at a line's start over more than one line, it ends at the end of
// the line before, and takes whole lines when it began in its first line's
// indent.
Range exclusive(const Buffer& buffer, Span span) {
    if (span.to.byte != 0 || span.to.line <= span.from.line) return {.span = span, .linewise = false};
    const std::size_t end_line = span.to.line - 1;
    if (span.from.byte <= indent_end(buffer.line(span.from.line))) {
        return lines(span.from, {.line = end_line, .byte = 0});
    }
    return {.span = {span.from, {end_line, buffer.line(end_line).size()}}, .linewise = false};
}

// The whole of the lines `range` acts on.
Span line_span(const Buffer& buffer, const Range& range) {
    return {{range.span.from.line, 0}, {range.span.to.line, buffer.line(range.span.to.line).size()}};
}

// What deleting `range` takes away: its text, or for whole lines, the lines
// with a break, the one before them when they end the text.
Span deleted_span(const Buffer& buffer, const Range& range) {
    if (!range.linewise) return range.span;
    const std::size_t first = range.span.from.line;
    const std::size_t last = range.span.to.line;
    if (last + 1 < buffer.line_count()) return {{first, 0}, {last + 1, 0}};
    if (first > 0) return {{first - 1, buffer.line(first - 1).size()}, {last, buffer.line(last).size()}};
    return {{0, 0}, buffer.end()};
}

std::string text_of(const Buffer& buffer, const Range& range) {
    return buffer.text(range.linewise ? line_span(buffer, range) : range.span);
}

// `changes` in order, those that overlap made one: the first's text over
// them all.
Edit ordered(std::vector<Change> changes) {
    std::stable_sort(changes.begin(), changes.end(), [](const Change& a, const Change& b) { return a.span.from < b.span.from; });
    Edit edit;
    for (Change& change : changes) {
        if (!edit.changes.empty() && change.span.from < edit.changes.back().span.to) {
            edit.changes.back().span.to = std::max(edit.changes.back().span.to, change.span.to);
        } else if (!edit.changes.empty() && change.span == edit.changes.back().span && change.span.empty()) {
            continue;
        } else {
            edit.changes.push_back(std::move(change));
        }
    }
    return edit;
}

// Makes `edit` as one step of the history (or on with one held), the
// selections then those `after` gives for what it made; with no changes,
// only the selections move.
template <typename After>
void commit(CommandContext& c, Edit edit, After&& after) {
    const Selections before = c.selections;
    const std::size_t primary = before.primary_index();
    if (edit.changes.empty()) {
        c.selections.set(after(edit), primary);
        return;
    }
    Edit undo = c.document.apply(edit);
    c.selections.set(after(edit), primary);
    c.document.history().record({.made = std::move(edit), .undo = std::move(undo), .before = before,
                                 .after = c.selections, .kind = EditKind::Other});
}

std::vector<Selection> selections_of(const CommandContext& c) {
    return {c.selections.all().begin(), c.selections.all().end()};
}

// The leading blanks that indent `columns` cells, as the document indents.
std::string indent_string(const CommandContext& c, std::size_t columns) {
    const Indent indent = c.document.indent();
    const std::size_t width = std::max<std::size_t>(indent.width, 1);
    if (!indent.tabs) return std::string(columns, ' ');
    return std::string(columns / width, '\t') + std::string(columns % width, ' ');
}

std::size_t indent_columns(const CommandContext& c, std::size_t line) {
    const std::string_view text = c.document.buffer().line(line);
    return column_of(text, indent_end(text), std::max<std::size_t>(c.document.indent().width, 1));
}

// The lines `ranges` act on, each once, in order.
std::set<std::size_t> lines_of(std::span<const std::optional<Range>> ranges) {
    std::set<std::size_t> out;
    for (const std::optional<Range>& range : ranges) {
        if (!range) continue;
        for (std::size_t line = range->span.from.line; line <= range->span.to.line; ++line) out.insert(line);
    }
    return out;
}

// Carets after an edit of whole lines: each range's on its first line's
// text, the others carried through.
std::vector<Selection> on_first_lines(const CommandContext& c, std::span<const std::optional<Range>> ranges,
                                      const std::vector<Selection>& was, const Edit& made) {
    std::vector<Selection> after;
    for (std::size_t i = 0; i < was.size(); ++i) {
        if (!ranges[i]) {
            after.push_back(Selection::at(map(made, was[i].caret, Bias::Before)));
            continue;
        }
        const Position start = map(made, {ranges[i]->span.from.line, 0}, Bias::Before);
        after.push_back(Selection::at(first_non_blank(c.document.buffer(), start.line)));
    }
    return after;
}

void delete_ranges(std::span<const std::optional<Range>> ranges, CommandContext& c) {
    const Buffer& buffer = c.document.buffer();
    const std::vector<Selection> was = selections_of(c);
    std::vector<Change> changes;
    std::vector<Span> spans(was.size());
    for (std::size_t i = 0; i < was.size(); ++i) {
        if (!ranges[i]) continue;
        spans[i] = deleted_span(buffer, *ranges[i]);
        changes.push_back({.span = spans[i], .text = ""});
    }
    commit(c, ordered(std::move(changes)), [&](const Edit& made) {
        std::vector<Selection> after;
        for (std::size_t i = 0; i < was.size(); ++i) {
            if (!ranges[i]) {
                after.push_back(Selection::at(map(made, was[i].caret, Bias::Before)));
            } else if (ranges[i]->linewise) {
                const Position start = map(made, spans[i].from, Bias::Before);
                after.push_back(Selection::at(first_non_blank(c.document.buffer(), start.line)));
            } else {
                after.push_back(Selection::at(map(made, spans[i].from, Bias::Before)));
            }
        }
        return after;
    });
}

void change_ranges(std::span<const std::optional<Range>> ranges, CommandContext& c) {
    const Buffer& buffer = c.document.buffer();
    const std::vector<Selection> was = selections_of(c);
    std::vector<Change> changes;
    std::vector<std::size_t> indents(was.size(), 0);
    for (std::size_t i = 0; i < was.size(); ++i) {
        if (!ranges[i]) continue;
        if (ranges[i]->linewise) {
            // Whole lines leave one line, indented as the first was.
            const std::string_view first = buffer.line(ranges[i]->span.from.line);
            const std::string indent(first.substr(0, indent_end(first)));
            indents[i] = indent.size();
            changes.push_back({.span = line_span(buffer, *ranges[i]), .text = indent});
        } else {
            changes.push_back({.span = ranges[i]->span, .text = ""});
        }
    }
    commit(c, ordered(std::move(changes)), [&](const Edit& made) {
        std::vector<Selection> after;
        for (std::size_t i = 0; i < was.size(); ++i) {
            if (!ranges[i]) {
                after.push_back(Selection::at(map(made, was[i].caret, Bias::Before)));
                continue;
            }
            const Position from = ranges[i]->linewise ? Position{ranges[i]->span.from.line, 0} : ranges[i]->span.from;
            const Position start = map(made, from, Bias::Before);
            after.push_back(Selection::at({start.line, start.byte + indents[i]}));
        }
        return after;
    });
}

void yank_ranges(std::span<const std::optional<Range>> ranges, CommandContext& c) {
    const std::vector<Selection> was = selections_of(c);
    commit(c, Edit{}, [&](const Edit&) {
        std::vector<Selection> after;
        for (std::size_t i = 0; i < was.size(); ++i) after.push_back(ranges[i] ? Selection::at(ranges[i]->span.from) : was[i]);
        return after;
    });
}

void shift_lines(std::span<const std::optional<Range>> ranges, bool right, std::size_t shifts, CommandContext& c) {
    const Buffer& buffer = c.document.buffer();
    const std::size_t step = std::max<std::size_t>(c.document.indent().width, 1) * shifts;
    std::vector<Change> changes;
    for (const std::size_t line : lines_of(ranges)) {
        const std::string_view text = buffer.line(line);
        // Empty lines aren't shifted.
        if (text.empty()) continue;
        const std::size_t columns = indent_columns(c, line);
        const std::size_t wanted = right ? columns + step : (columns > step ? columns - step : 0);
        const std::string indent = indent_string(c, wanted);
        const std::size_t end = indent_end(text);
        if (indent != text.substr(0, end)) changes.push_back({.span = {{line, 0}, {line, end}}, .text = indent});
    }
    const std::vector<Selection> was = selections_of(c);
    commit(c, ordered(std::move(changes)), [&](const Edit& made) { return on_first_lines(c, ranges, was, made); });
}

// Whether `line` ends, its code's last character, in an opener.
bool opens_block(const Document& document, std::size_t line) {
    const std::string_view text = document.buffer().line(line);
    for (std::size_t byte = text.size(); byte-- > 0;) {
        if (blank(text[byte]) || in_string_or_comment(document.syntax(), line, byte)) continue;
        return text[byte] == '{' || text[byte] == '(' || text[byte] == '[';
    }
    return false;
}

void indent_lines(std::span<const std::optional<Range>> ranges, CommandContext& c) {
    const Buffer& buffer = c.document.buffer();
    const std::size_t level = std::max<std::size_t>(c.document.indent().width, 1);
    // The indents given so far, in cells, which the lines after go by.
    std::map<std::size_t, std::size_t> given;
    const auto columns = [&](std::size_t line) {
        const auto found = given.find(line);
        return found != given.end() ? found->second : indent_columns(c, line);
    };
    std::vector<Change> changes;
    for (const std::size_t line : lines_of(ranges)) {
        const std::string_view text = buffer.line(line);
        const std::size_t end = indent_end(text);
        std::size_t wanted = 0;
        if (end < text.size()) {
            const char first = text[end];
            const bool closes = (first == '}' || first == ')' || first == ']') &&
                                !in_string_or_comment(c.document.syntax(), line, end);
            if (closes) {
                // A closer lines up with its opener's line.
                const char open = first == '}' ? '{' : first == ')' ? '(' : '[';
                if (const std::optional<Position> opener = enclosing_opener(buffer, &c.document.syntax(), {line, end}, open)) {
                    wanted = columns(opener->line);
                }
            } else {
                // As the line with text before it, a level deeper after an opener.
                std::size_t above = line;
                while (above > 0 && indent_end(buffer.line(above - 1)) == buffer.line(above - 1).size()) --above;
                if (above > 0) wanted = columns(above - 1) + (opens_block(c.document, above - 1) ? level : 0);
            }
        }
        given[line] = wanted;
        const std::string indent = end < text.size() ? indent_string(c, wanted) : std::string();
        if (indent != text.substr(0, end)) changes.push_back({.span = {{line, 0}, {line, end}}, .text = indent});
    }
    const std::vector<Selection> was = selections_of(c);
    commit(c, ordered(std::move(changes)), [&](const Edit& made) { return on_first_lines(c, ranges, was, made); });
}

char case_changed(char ch, Operator op) {
    const bool lower = ch >= 'a' && ch <= 'z';
    const bool upper = ch >= 'A' && ch <= 'Z';
    if (lower && (op == Operator::Upper || op == Operator::ToggleCase)) return static_cast<char>(ch - 'a' + 'A');
    if (upper && (op == Operator::Lower || op == Operator::ToggleCase)) return static_cast<char>(ch - 'A' + 'a');
    return ch;
}

void change_case(Operator op, std::span<const std::optional<Range>> ranges, CommandContext& c) {
    const Buffer& buffer = c.document.buffer();
    const std::vector<Selection> was = selections_of(c);
    // The ranges' spans, those that overlap made one.
    std::vector<Change> spans;
    for (const std::optional<Range>& range : ranges) {
        if (range) spans.push_back({.span = range->linewise ? line_span(buffer, *range) : range->span, .text = ""});
    }
    const Edit merged = ordered(std::move(spans));
    std::vector<Change> changes;
    for (const Change& change : merged.changes) {
        std::string text = buffer.text(change.span);
        const std::string before = text;
        for (char& ch : text) ch = case_changed(ch, op);
        if (text != before) changes.push_back({.span = change.span, .text = std::move(text)});
    }
    commit(c, ordered(std::move(changes)), [&](const Edit& made) {
        std::vector<Selection> after;
        for (std::size_t i = 0; i < was.size(); ++i) {
            after.push_back(Selection::at(map(made, ranges[i] ? ranges[i]->span.from : was[i].caret, Bias::Before)));
        }
        return after;
    });
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> out;
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            out.emplace_back(text.substr(begin, i - begin));
            begin = i + 1;
        }
    }
    return out;
}

} // namespace

Range motion_range(const Buffer& buffer, Position from, Position to, MotionKind kind) {
    const Position first = std::min(from, to);
    const Position last = std::max(from, to);
    if (kind == MotionKind::Linewise) return lines(first, last);
    if (kind == MotionKind::Inclusive) return {.span = {first, right_in_line(buffer, last, 1)}, .linewise = false};
    return exclusive(buffer, {first, last});
}

Range object_range(const Buffer& buffer, const Taken& taken) {
    if (taken.linewise) return lines(taken.span.from, taken.span.to);
    return exclusive(buffer, taken.span);
}

void operate(Operator op, std::span<const std::optional<Range>> ranges, char name, CommandContext& context,
             Registers& registers, std::size_t shifts, bool numbered) {
    const Buffer& buffer = context.document.buffer();
    Yanked yanked;
    for (const std::optional<Range>& range : ranges) {
        if (!range) continue;
        yanked.pieces.push_back(text_of(buffer, *range));
        yanked.linewise = range->linewise;
    }
    if (yanked.pieces.empty()) return;
    switch (op) {
        case Operator::Delete:
            registers.deleted(name, std::move(yanked), context.clipboard, numbered);
            delete_ranges(ranges, context);
            break;
        case Operator::Change:
            registers.deleted(name, std::move(yanked), context.clipboard, numbered);
            // What insert mode types after is one step with the change.
            context.document.history().hold();
            change_ranges(ranges, context);
            break;
        case Operator::Yank:
            registers.yank(name, std::move(yanked), context.clipboard);
            yank_ranges(ranges, context);
            break;
        case Operator::ShiftRight:
        case Operator::ShiftLeft: shift_lines(ranges, op == Operator::ShiftRight, shifts, context); break;
        case Operator::Indent: indent_lines(ranges, context); break;
        case Operator::ToggleCase:
        case Operator::Lower:
        case Operator::Upper: change_case(op, ranges, context); break;
    }
}

void put(const Yanked& yanked, bool after, std::size_t count, CommandContext& context) {
    if (yanked.pieces.empty() || count == 0) return;
    const Buffer& buffer = context.document.buffer();
    const std::vector<Selection> was = selections_of(context);
    const std::size_t carets = was.size();
    // One piece with a line for each caret spreads its lines over them.
    std::vector<std::string> spread;
    if (yanked.pieces.size() == 1 && !yanked.linewise && carets > 1) {
        spread = split_lines(yanked.pieces.front());
        if (spread.size() != carets) spread.clear();
    }
    std::string whole;
    for (std::size_t i = 0; i < yanked.pieces.size(); ++i) whole += (i > 0 ? "\n" : "") + yanked.pieces[i];

    std::vector<Change> changes;
    // The change each caret's put made, by its index among the changes.
    std::vector<std::size_t> made_by(carets, 0);
    std::vector<bool> multiline(carets, false);
    std::optional<std::size_t> last_line;
    for (std::size_t i = 0; i < carets; ++i) {
        const std::string& piece = yanked.pieces.size() == carets ? yanked.pieces[i] : !spread.empty() ? spread[i] : whole;
        std::string block = piece;
        for (std::size_t n = 1; n < count; ++n) block += (yanked.linewise ? "\n" : "") + piece;
        const Position caret = was[i].caret;
        if (yanked.linewise) {
            // Whole lines go once to a line, however many carets are on it.
            if (last_line == caret.line) {
                made_by[i] = changes.size() - 1;
                continue;
            }
            last_line = caret.line;
            made_by[i] = changes.size();
            const std::size_t end = buffer.line(caret.line).size();
            changes.push_back(after ? Change{.span = {{caret.line, end}, {caret.line, end}}, .text = "\n" + block}
                                    : Change{.span = {{caret.line, 0}, {caret.line, 0}}, .text = block + "\n"});
            continue;
        }
        const Position at = after && !buffer.line(caret.line).empty() ? right_in_line(buffer, caret, 1) : caret;
        made_by[i] = changes.size();
        multiline[i] = block.find('\n') != std::string::npos;
        changes.push_back({.span = {at, at}, .text = std::move(block)});
    }
    Edit edit;
    edit.changes = std::move(changes);
    commit(context, std::move(edit), [&](const Edit& made) {
        const std::vector<Span> spans = spans_after(made);
        std::vector<Selection> next;
        for (std::size_t i = 0; i < carets; ++i) {
            const Span& span = spans[made_by[i]];
            if (yanked.linewise) {
                next.push_back(Selection::at(first_non_blank(context.document.buffer(), after ? span.from.line + 1 : span.from.line)));
            } else if (multiline[i] || span.empty()) {
                next.push_back(Selection::at(span.from));
            } else {
                next.push_back(Selection::at(left_in_line(context.document.buffer(), span.to, 1)));
            }
        }
        return next;
    });
}

void join_lines(std::size_t count, CommandContext& context) {
    const Buffer& buffer = context.document.buffer();
    const std::vector<Selection> was = selections_of(context);
    const std::size_t joins = std::max<std::size_t>(count, 2) - 1;
    std::vector<Change> changes;
    std::vector<std::optional<std::size_t>> made_by(was.size());
    std::vector<std::size_t> points(was.size(), 0);
    std::optional<std::size_t> joined_to;
    for (std::size_t i = 0; i < was.size(); ++i) {
        const std::size_t first = was[i].caret.line;
        if (joined_to && first <= *joined_to) continue;
        const std::size_t last = std::min(first + joins, buffer.line_count() - 1);
        if (last == first) continue;
        std::string text(buffer.line(first));
        std::size_t point = 0;
        for (std::size_t line = first + 1; line <= last; ++line) {
            const std::string_view next = buffer.line(line);
            const std::string_view trimmed = next.substr(indent_end(next));
            const bool space = !text.empty() && !blank(text.back()) && !trimmed.empty() && trimmed.front() != ')';
            point = text.size();
            if (space) text += ' ';
            text += trimmed;
        }
        joined_to = last;
        made_by[i] = changes.size();
        points[i] = point;
        changes.push_back({.span = {{first, 0}, {last, buffer.line(last).size()}}, .text = std::move(text)});
    }
    Edit edit;
    edit.changes = std::move(changes);
    commit(context, std::move(edit), [&](const Edit& made) {
        const std::vector<Span> spans = spans_after(made);
        std::vector<Selection> next;
        for (std::size_t i = 0; i < was.size(); ++i) {
            if (made_by[i]) {
                next.push_back(Selection::at({spans[*made_by[i]].from.line, points[i]}));
            } else {
                next.push_back(Selection::at(map(made, was[i].caret, Bias::Before)));
            }
        }
        return next;
    });
}

bool replace_characters(std::string_view with, std::size_t count, CommandContext& context) {
    const Buffer& buffer = context.document.buffer();
    const std::vector<Selection> was = selections_of(context);
    const bool line_break = with == "\n" || with == "\r";
    std::vector<Change> changes;
    std::vector<std::optional<std::size_t>> made_by(was.size());
    for (std::size_t i = 0; i < was.size(); ++i) {
        const Position from = was[i].caret;
        Position to = from;
        bool enough = true;
        for (std::size_t n = 0; n < count && enough; ++n) {
            enough = to.byte < buffer.line(to.line).size();
            if (enough) to = right_in_line(buffer, to, 1);
        }
        if (!enough || count == 0) continue;
        std::string text;
        if (line_break) {
            text = "\n";
        } else {
            for (std::size_t n = 0; n < count; ++n) text += with;
        }
        made_by[i] = changes.size();
        changes.push_back({.span = {from, to}, .text = std::move(text)});
    }
    if (changes.empty()) return false;
    Edit edit = ordered(std::move(changes));
    commit(context, std::move(edit), [&](const Edit& made) {
        const std::vector<Span> spans = spans_after(made);
        std::vector<Selection> next;
        for (std::size_t i = 0; i < was.size(); ++i) {
            if (!made_by[i]) {
                next.push_back(Selection::at(map(made, was[i].caret, Bias::Before)));
            } else if (line_break) {
                next.push_back(Selection::at(spans[*made_by[i]].to));
            } else {
                next.push_back(Selection::at(left_in_line(context.document.buffer(), spans[*made_by[i]].to, 1)));
            }
        }
        return next;
    });
    return true;
}

void toggle_case_characters(std::size_t count, CommandContext& context) {
    const Buffer& buffer = context.document.buffer();
    const std::vector<Selection> was = selections_of(context);
    std::vector<Change> changes;
    std::vector<Position> ends;
    for (const Selection& selection : was) {
        const Position to = right_in_line(buffer, selection.caret, count);
        ends.push_back(to);
        std::string text = buffer.text({selection.caret, to});
        const std::string before = text;
        for (char& ch : text) ch = case_changed(ch, Operator::ToggleCase);
        if (text != before) changes.push_back({.span = {selection.caret, to}, .text = std::move(text)});
    }
    commit(context, ordered(std::move(changes)), [&](const Edit& made) {
        std::vector<Selection> next;
        for (const Position& end : ends) next.push_back(Selection::at(map(made, end, Bias::After)));
        return next;
    });
}

bool add_to_number(long long amount, CommandContext& context) {
    const Buffer& buffer = context.document.buffer();
    const std::vector<Selection> was = selections_of(context);
    std::vector<Change> changes;
    std::vector<std::optional<std::size_t>> made_by(was.size());
    const auto digit = [](char ch) { return ch >= '0' && ch <= '9'; };
    const auto hex_digit = [&](char ch) { return digit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'); };
    for (std::size_t i = 0; i < was.size(); ++i) {
        const std::size_t line = was[i].caret.line;
        const std::string_view text = buffer.line(line);
        const std::size_t at = was[i].caret.byte;
        // The hexadecimal number the caret is in, after its 0x.
        std::size_t start = at;
        while (start > 0 && start <= text.size() && hex_digit(text[start - 1])) --start;
        bool hex = start >= 2 && (text[start - 1] == 'x' || text[start - 1] == 'X') && text[start - 2] == '0' &&
                   at < text.size() && hex_digit(text[at]);
        if (!hex) {
            // Else the decimal number it is in, or the first after it.
            start = at;
            if (start < text.size() && digit(text[start])) {
                while (start > 0 && digit(text[start - 1])) --start;
            } else {
                while (start < text.size() && !digit(text[start])) ++start;
                if (start == text.size()) continue;
            }
            // On the 0x of a hexadecimal number: the number after it.
            hex = start >= 2 && (text[start - 1] == 'x' || text[start - 1] == 'X') && text[start - 2] == '0';
            if (!hex && text[start] == '0' && start + 2 < text.size() && (text[start + 1] == 'x' || text[start + 1] == 'X') &&
                hex_digit(text[start + 2])) {
                hex = true;
                start += 2;
            }
        }
        std::size_t end = start;
        while (end < text.size() && (hex ? hex_digit(text[end]) : digit(text[end]))) ++end;
        const bool negative = !hex && start > 0 && text[start - 1] == '-';
        const std::size_t from = negative ? start - 1 : start;

        std::string replaced;
        if (hex) {
            unsigned long long value = 0;
            if (std::from_chars(text.data() + start, text.data() + end, value, 16).ec != std::errc{}) continue;
            value += static_cast<unsigned long long>(amount);
            const bool upper = std::any_of(text.begin() + static_cast<std::ptrdiff_t>(start), text.begin() + static_cast<std::ptrdiff_t>(end),
                                           [](char ch) { return ch >= 'A' && ch <= 'F'; });
            char digits[32];
            const auto made = std::to_chars(std::begin(digits), std::end(digits), value, 16);
            std::string written(digits, made.ptr);
            if (upper) {
                for (char& ch : written) ch = case_changed(ch, Operator::Upper);
            }
            // Leading zeros keep the number's width.
            if (written.size() < end - start && text[start] == '0') written.insert(0, end - start - written.size(), '0');
            replaced = written;
        } else {
            long long value = 0;
            if (std::from_chars(text.data() + start, text.data() + end, value).ec != std::errc{}) continue;
            replaced = std::to_string((negative ? -value : value) + amount);
        }
        made_by[i] = changes.size();
        changes.push_back({.span = {{line, from}, {line, end}}, .text = std::move(replaced)});
    }
    if (changes.empty()) return false;
    commit(context, ordered(std::move(changes)), [&](const Edit& made) {
        const std::vector<Span> spans = spans_after(made);
        std::vector<Selection> next;
        for (std::size_t i = 0; i < was.size(); ++i) {
            if (made_by[i]) {
                next.push_back(Selection::at(left_in_line(context.document.buffer(), spans[*made_by[i]].to, 1)));
            } else {
                next.push_back(Selection::at(map(made, was[i].caret, Bias::Before)));
            }
        }
        return next;
    });
    return true;
}

} // namespace fjedit::vim
