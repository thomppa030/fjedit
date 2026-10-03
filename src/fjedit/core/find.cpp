#include "fjedit/core/find.hpp"

#include "fjedit/core/motion.hpp"

#include "ftk/base/utf8.hpp"

#include <algorithm>

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

// Whether `needle`, cut into `parts`, occurs from `from`.
bool matches_at(const Buffer& buffer, const std::vector<std::string_view>& parts, Position from, bool match_case) {
    const std::size_t last = from.line + parts.size() - 1;
    if (last >= buffer.line_count()) return false;
    const std::string_view first = buffer.line(from.line).substr(from.byte);
    if (parts.size() == 1) return first.size() >= parts[0].size() && same(first.substr(0, parts[0].size()), parts[0], match_case);

    if (!same(first, parts.front(), match_case)) return false;
    for (std::size_t i = 1; i + 1 < parts.size(); ++i) {
        if (!same(buffer.line(from.line + i), parts[i], match_case)) return false;
    }
    const std::string_view end = buffer.line(last);
    return end.size() >= parts.back().size() && same(end.substr(0, parts.back().size()), parts.back(), match_case);
}

// Where a match from `from` of `parts` ends.
Position match_end(const std::vector<std::string_view>& parts, Position from) {
    if (parts.size() == 1) return {from.line, from.byte + parts[0].size()};
    return {from.line + parts.size() - 1, parts.back().size()};
}

} // namespace

std::vector<Span> find_all(const Buffer& buffer, std::string_view needle, FindOptions options) {
    return find_in_lines(buffer, needle, 0, buffer.line_count(), options);
}

std::vector<Span> find_in_lines(const Buffer& buffer, std::string_view needle, std::size_t first_line,
                                std::size_t end_line, FindOptions options) {
    std::vector<Span> found;
    end_line = std::min(end_line, buffer.line_count());
    if (needle.empty()) return found;
    const std::vector<std::string_view> parts = pieces(needle);
    const bool word_first = word_after(needle, 0);
    const bool word_last = word_before(needle, needle.size());

    // A match found at a whole word's place: a word character it begins or
    // ends with doesn't carry on a word of the text.
    auto apart = [&](Position from, Position to) {
        if (!options.whole_word) return true;
        if (word_first && word_before(buffer.line(from.line), from.byte)) return false;
        return !(word_last && word_after(buffer.line(to.line), to.byte));
    };

    // Every character boundary in turn; after a match, from its end. A match
    // is never empty, so the walk always moves on.
    Position at{first_line, 0};
    while (at.line < end_line) {
        const std::string_view line = buffer.line(at.line);
        if (at.byte > line.size()) {
            at = {at.line + 1, 0};
            continue;
        }
        const bool boundary = at.byte == line.size() || !continuation(line[at.byte]);
        if (boundary && matches_at(buffer, parts, at, options.match_case)) {
            const Position end = match_end(parts, at);
            if (apart(at, end)) {
                found.push_back({at, end});
                at = end;
                continue;
            }
        }
        ++at.byte;
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
