#include "fjedit/core/find.hpp"

#include "fjedit/core/motion.hpp"

#include "ftk/base/utf8.hpp"

#include <algorithm>
#include <regex>

namespace fjedit {

namespace {

char folded(char c, bool match_case) {
    if (!match_case && c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
    return c;
}

bool same(std::string_view a, std::string_view b, bool match_case) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (folded(a[i], match_case) != folded(b[i], match_case)) return false;
    }
    return true;
}

bool continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// Whether a word character of `line` touches byte `at` from the left.
bool word_before(std::string_view line, std::size_t at) {
    if (at == 0) return false;
    std::size_t start = at - 1;
    while (start > 0 && continuation(line[start])) --start;
    return char_kind(ftk::utf8::next(line, start)) == CharKind::Word;
}

// Whether a word character of `line` begins at byte `at`.
bool word_after(std::string_view line, std::size_t at) {
    return at < line.size() && char_kind(ftk::utf8::next(line, at)) == CharKind::Word;
}

// The places on a line, or in a whole text, where a match may begin: where
// the needle's first byte is, in either case when case doesn't matter. Each
// is found by a scan of its own and kept until the walk passes it, so a
// long line is scanned once whatever the walk asks.
class Starts {
public:
    Starts(std::string_view line, char first, bool match_case) : line_(line) {
        const char lower = folded(first, false);
        bytes_[0] = match_case ? first : lower;
        if (!match_case && lower >= 'a' && lower <= 'z') {
            bytes_[1] = static_cast<char>(lower - 'a' + 'A');
            count_ = 2;
        }
    }

    // The first place at or after `from`, or npos.
    std::size_t next(std::size_t from) {
        std::size_t first = std::string_view::npos;
        for (std::size_t i = 0; i < count_; ++i) {
            if (!found_[i] || (at_[i] != std::string_view::npos && at_[i] < from)) {
                at_[i] = line_.find(bytes_[i], from);
                found_[i] = true;
            }
            first = std::min(first, at_[i]);
        }
        return first;
    }

private:
    std::string_view line_;
    char bytes_[2]{};
    std::size_t at_[2]{};
    bool found_[2]{};
    std::size_t count_{1};
};

// `needle` cut at its \n.
std::vector<std::string_view> pieces(std::string_view needle) {
    std::vector<std::string_view> out;
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= needle.size(); ++i) {
        if (i == needle.size() || needle[i] == '\n') {
            out.push_back(needle.substr(begin, i - begin));
            begin = i + 1;
        }
    }
    return out;
}

// A walk down a text's lines, broken where Buffer breaks them: at \n, \r\n
// or a lone \r. Each break is looked for once, as the walk comes to it, so
// a walk to the end reads the text once.
class LineWalk {
public:
    explicit LineWalk(std::string_view text) : text_(text), lf_(text.find('\n')), cr_(text.find('\r')) {}

    // The line it is on: its index, the byte of the text it begins at, and
    // its text without its break.
    [[nodiscard]] std::size_t index() const { return index_; }
    [[nodiscard]] std::size_t begin() const { return begin_; }
    [[nodiscard]] std::string_view line() { return text_.substr(begin_, end() - begin_); }

    // On to the next line; false on the last, which no break ends.
    bool next() {
        const std::size_t at = end();
        if (at == text_.size()) return false;
        const bool crlf = text_[at] == '\r' && at + 1 < text_.size() && text_[at + 1] == '\n';
        begin_ = at + (crlf ? 2 : 1);
        ++index_;
        return true;
    }

    // On to the line byte `at` is on, from the line it is on or one before.
    void to(std::size_t at) {
        while (end() < at) {
            if (!next()) return;
        }
    }

private:
    // Where the line it is on ends: at its break, or at the text's end.
    std::size_t end() {
        if (lf_ < begin_) lf_ = text_.find('\n', begin_);
        if (cr_ < begin_) cr_ = text_.find('\r', begin_);
        return std::min({lf_, cr_, text_.size()});
    }

    std::string_view text_;
    std::size_t index_{0};
    std::size_t begin_{0};
    // The first \n and \r at or after the line's start, or npos.
    std::size_t lf_;
    std::size_t cr_;
};

// A text's lines as a Buffer holds them, viewed where they are in it.
class TextLines {
public:
    explicit TextLines(std::string_view text) {
        LineWalk walk(text);
        lines_.push_back(walk.line());
        while (walk.next()) lines_.push_back(walk.line());
    }

    [[nodiscard]] std::size_t line_count() const { return lines_.size(); }
    [[nodiscard]] std::string_view line(std::size_t index) const { return lines_[index]; }

private:
    std::vector<std::string_view> lines_;
};

// Whether `needle`, cut into `parts`, occurs from `from` in `lines`, a
// Buffer or TextLines.
template <typename Lines>
bool matches_at(const Lines& lines, const std::vector<std::string_view>& parts, Position from, bool match_case) {
    const std::size_t last = from.line + parts.size() - 1;
    if (last >= lines.line_count()) return false;
    const std::string_view first = lines.line(from.line).substr(from.byte);
    if (parts.size() == 1) return first.size() >= parts[0].size() && same(first.substr(0, parts[0].size()), parts[0], match_case);

    if (!same(first, parts.front(), match_case)) return false;
    for (std::size_t i = 1; i + 1 < parts.size(); ++i) {
        if (!same(lines.line(from.line + i), parts[i], match_case)) return false;
    }
    const std::string_view end = lines.line(last);
    return end.size() >= parts.back().size() && same(end.substr(0, parts.back().size()), parts.back(), match_case);
}

// Where a match from `from` of `parts` ends.
Position match_end(const std::vector<std::string_view>& parts, Position from) {
    if (parts.size() == 1) return {from.line, from.byte + parts[0].size()};
    return {from.line + parts.size() - 1, parts.back().size()};
}

// `pattern` as a regular expression, or nothing when it doesn't parse.
std::optional<std::regex> compiled(std::string_view pattern, bool match_case) {
    auto flags = std::regex::ECMAScript;
    if (!match_case) flags |= std::regex::icase;
    try {
        return std::regex(pattern.begin(), pattern.end(), flags);
    } catch (const std::regex_error&) {
        return std::nullopt;
    }
}

// Every match of `expression` in `line` whose group `options` names took
// part, and took text unless `options` lets matches take none, in order.
template <typename Found>
void each_match(std::string_view line, const std::regex& expression, const FindOptions& options, Found&& found) {
    using Matches = std::regex_iterator<std::string_view::const_iterator>;
    const auto group = static_cast<int>(options.group);
    for (Matches it(line.begin(), line.end(), expression), end; it != end; ++it) {
        if (options.group >= it->size() || !(*it)[group].matched) continue;
        if (it->length(group) == 0 && !options.empty) continue;
        found(*it);
    }
}

// The match of `expression` on `match`'s line whose group `options` names
// begins where `match` does, found again from the line's start so anchors
// and what is around it read as they did when it was found; `use` is given
// it.
template <typename Use>
bool rematch(const Buffer& buffer, Span match, const std::regex& expression, const FindOptions& options, Use&& use) {
    bool done = false;
    const auto group = static_cast<int>(options.group);
    each_match(buffer.line(match.from.line), expression, options, [&](const auto& found) {
        if (done || static_cast<std::size_t>(found.position(group)) != match.from.byte) return;
        use(found);
        done = true;
    });
    return done;
}

// A match from `from` to `to` on `line` that a whole word search keeps: a
// word character at either end doesn't carry on a word of the text.
bool whole_word_at(std::string_view line, std::size_t from, std::size_t to) {
    if (word_after(line, from) && word_before(line, from)) return false;
    return !(word_before(line, to) && word_after(line, to));
}

template <typename Lines>
std::vector<Span> regex_in_lines(const Lines& lines, std::string_view pattern, std::size_t first_line,
                                 std::size_t end_line, FindOptions options, std::size_t limit) {
    std::vector<Span> found;
    const std::optional<std::regex> expression = compiled(pattern, options.match_case);
    if (!expression) return found;
    for (std::size_t line = first_line; line < end_line && found.size() < limit; ++line) {
        const std::string_view text = lines.line(line);
        const auto group = static_cast<int>(options.group);
        each_match(text, *expression, options, [&](const auto& match) {
            const auto from = static_cast<std::size_t>(match.position(group));
            const std::size_t to = from + static_cast<std::size_t>(match.length(group));
            if (found.size() >= limit || (options.whole_word && !whole_word_at(text, from, to))) return;
            found.push_back({{line, from}, {line, to}});
        });
    }
    return found;
}

// The first `limit` places `needle` occurs that begin on lines `first_line`
// up to `end_line` of `lines`, a Buffer or TextLines.
template <typename Lines>
std::vector<Span> in_lines(const Lines& lines, std::string_view needle, std::size_t first_line, std::size_t end_line,
                           FindOptions options, std::size_t limit) {
    std::vector<Span> found;
    end_line = std::min(end_line, lines.line_count());
    if (needle.empty()) return found;
    if (options.regex) return regex_in_lines(lines, needle, first_line, end_line, options, limit);
    const std::vector<std::string_view> parts = pieces(needle);
    const bool word_first = word_after(needle, 0);
    const bool word_last = word_before(needle, needle.size());

    // A match found at a whole word's place: a word character it begins or
    // ends with doesn't carry on a word of the text.
    auto apart = [&](Position from, Position to) {
        if (!options.whole_word) return true;
        if (word_first && word_before(lines.line(from.line), from.byte)) return false;
        return !(word_last && word_after(lines.line(to.line), to.byte));
    };

    // A needle within a line: from each place on each line where its first
    // byte is, and after a match from its end.
    if (parts.size() == 1) {
        for (std::size_t index = first_line; index < end_line && found.size() < limit; ++index) {
            const std::string_view line = lines.line(index);
            Starts starts(line, needle.front(), options.match_case);
            std::size_t from = 0;
            for (std::size_t start = starts.next(from); start != std::string_view::npos && found.size() < limit;
                 start = starts.next(from)) {
                const Position at{index, start};
                if (!continuation(line[start]) && matches_at(lines, parts, at, options.match_case)) {
                    const Position end = match_end(parts, at);
                    if (apart(at, end)) {
                        found.push_back({at, end});
                        from = end.byte;
                        continue;
                    }
                }
                from = start + 1;
            }
        }
        return found;
    }

    // A needle across lines can begin only where its first piece ends a
    // line; after a match, the next is looked for from its end.
    Position from{first_line, 0};
    while (from.line < end_line && found.size() < limit) {
        const std::string_view line = lines.line(from.line);
        if (line.size() >= parts.front().size()) {
            const Position at{from.line, line.size() - parts.front().size()};
            const bool boundary = at.byte == line.size() || !continuation(line[at.byte]);
            if (at.byte >= from.byte && boundary && matches_at(lines, parts, at, options.match_case)) {
                const Position end = match_end(parts, at);
                if (apart(at, end)) {
                    found.push_back({at, end});
                    from = end;
                    continue;
                }
            }
        }
        from = {from.line + 1, 0};
    }
    return found;
}

// A needle within a line, looked for in the whole of `text` at once rather
// than line by line: from each place its first byte is, and after a match
// from its end. Only a match is walked to its line, where a whole word is
// judged as find_all() judges it.
std::vector<TextMatch> in_text(std::string_view text, std::string_view needle, FindOptions options, std::size_t limit) {
    std::vector<TextMatch> found;
    // No line holds a \r.
    if (needle.find('\r') != std::string_view::npos) return found;
    const bool word_first = word_after(needle, 0);
    const bool word_last = word_before(needle, needle.size());
    LineWalk lines(text);
    Starts starts(text, needle.front(), options.match_case);
    std::size_t from = 0;
    for (std::size_t start = starts.next(from); start != std::string_view::npos && found.size() < limit;
         start = starts.next(from)) {
        from = start + 1;
        if (continuation(text[start]) || text.size() - start < needle.size() ||
            !same(text.substr(start, needle.size()), needle, options.match_case)) {
            continue;
        }
        lines.to(start);
        const std::string_view line = lines.line();
        const Position at{lines.index(), start - lines.begin()};
        const Position end{at.line, at.byte + needle.size()};
        if (options.whole_word &&
            ((word_first && word_before(line, at.byte)) || (word_last && word_after(line, end.byte)))) {
            continue;
        }
        found.push_back({{at, end}, line});
        from = start + needle.size();
    }
    return found;
}

} // namespace

std::vector<Span> find_all(const Buffer& buffer, std::string_view needle, FindOptions options) {
    return find_in_lines(buffer, needle, 0, buffer.line_count(), options);
}

std::vector<Span> find_in_lines(const Buffer& buffer, std::string_view needle, std::size_t first_line,
                                std::size_t end_line, FindOptions options) {
    return in_lines(buffer, needle, first_line, end_line, options, std::numeric_limits<std::size_t>::max());
}

std::vector<TextMatch> find_in_text(std::string_view text, std::string_view needle, FindOptions options,
                                    std::size_t limit) {
    if (text.starts_with(BYTE_ORDER_MARK)) text.remove_prefix(BYTE_ORDER_MARK.size());
    if (needle.empty() || limit == 0) return {};
    if (!options.regex && needle.find('\n') == std::string_view::npos) return in_text(text, needle, options, limit);

    // A regular expression, or a needle across lines: line by line, as
    // find_all() looks, on views of the text's lines.
    const TextLines lines(text);
    std::vector<TextMatch> found;
    for (const Span& span : in_lines(lines, needle, 0, lines.line_count(), options, limit)) {
        found.push_back({span, lines.line(span.from.line)});
    }
    return found;
}

std::optional<Span> find_next(const Buffer& buffer, std::string_view needle, Position from, FindOptions options) {
    const std::vector<Span> all = find_all(buffer, needle, options);
    if (all.empty()) return std::nullopt;
    for (const Span& span : all) {
        if (span.from >= from) return span;
    }
    return all.front();
}

bool regex_valid(std::string_view pattern) {
    return compiled(pattern, true).has_value();
}

std::string replacement(const Buffer& buffer, Span match, std::string_view needle, std::string_view with,
                        FindOptions options) {
    if (!options.regex) return std::string(with);
    const std::optional<std::regex> expression = compiled(needle, options.match_case);
    if (!expression) return std::string(with);
    std::string out(with);
    (void)rematch(buffer, match, *expression, options,
                  [&](const auto& found) { out = found.format(std::string(with)); });
    return out;
}

std::optional<std::vector<std::string>> captured(const Buffer& buffer, Span match, std::string_view needle,
                                                 FindOptions options) {
    const std::optional<std::regex> expression = compiled(needle, options.match_case);
    if (!expression) return std::nullopt;
    std::vector<std::string> groups;
    const bool found = rematch(buffer, match, *expression, options, [&](const auto& matched) {
        for (std::size_t i = 0; i < matched.size(); ++i) groups.push_back(matched.str(static_cast<int>(i)));
    });
    if (!found) return std::nullopt;
    return groups;
}

Span word_at(const Buffer& buffer, Position at) {
    const std::string_view line = buffer.line(at.line);
    std::size_t from = at.byte;
    std::size_t to = at.byte;
    if (!word_after(line, at.byte) && !word_before(line, at.byte)) return {at, at};
    while (word_before(line, from)) {
        --from;
        while (from > 0 && continuation(line[from])) --from;
    }
    while (word_after(line, to)) (void)ftk::utf8::next(line, to);
    return {{at.line, from}, {at.line, to}};
}

} // namespace fjedit
