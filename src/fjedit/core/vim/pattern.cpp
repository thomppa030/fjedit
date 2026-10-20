#include "fjedit/core/vim/pattern.hpp"

#include <cstdint>
#include <format>
#include <utility>

namespace fjedit::vim {

namespace {

// How much of a pattern means something without a backslash, as \v, \m,
// \M and \V switch it: very magic, magic, nomagic, very nomagic.
enum class Magic : uint8_t { Very, On, Off, VeryOff };

// Whether `c` means something as written, or `escaped` after a backslash.
bool special(char c, bool escaped, Magic magic) {
    // These want a backslash, but in very magic mode.
    if (c != '\0' && std::string_view("()|+?={@<>%&").find(c) != std::string_view::npos) {
        return (magic == Magic::Very) != escaped;
    }
    // These want one in nomagic and very nomagic mode.
    if (c != '\0' && std::string_view(".*[~").find(c) != std::string_view::npos) {
        return (magic == Magic::Very || magic == Magic::On) != escaped;
    }
    // And these in very nomagic mode.
    if (c == '^' || c == '$') return (magic != Magic::VeryOff) != escaped;
    return false;
}

// Vim's classes of characters, as find's expressions write them; a word or
// keyword character is what fjedit's words have, ASCII's letters, digits
// and _.
struct Class {
    char name;
    std::string_view regex;
};
constexpr Class CLASSES[] = {
    {'s', "[ \\t]"},       {'S', "[^ \\t]"},       {'d', "[0-9]"},        {'D', "[^0-9]"},
    {'w', "[0-9A-Za-z_]"}, {'W', "[^0-9A-Za-z_]"}, {'a', "[A-Za-z]"},     {'A', "[^A-Za-z]"},
    {'l', "[a-z]"},        {'L', "[^a-z]"},        {'u', "[A-Z]"},        {'U', "[^A-Z]"},
    {'x', "[0-9A-Fa-f]"},  {'X', "[^0-9A-Fa-f]"},  {'o', "[0-7]"},        {'O', "[^0-7]"},
    {'h', "[A-Za-z_]"},    {'H', "[^A-Za-z_]"},    {'k', "[0-9A-Za-z_]"}, {'K', "[A-Za-z_]"},
    {'i', "[0-9A-Za-z_]"}, {'I', "[A-Za-z_]"},
};

// `c` as a character of find's expressions matches it.
std::string as_literal(char c) {
    if (c != '\0' && std::string_view("\\^$.|?*+()[]{}/").find(c) != std::string_view::npos) return std::string{'\\', c};
    return std::string(1, c);
}

// The character a collection's \d, \o, \x, \u or \U at `from` writes, its
// digits read on from `from` + 2; `end` is left past them. Nothing for a
// code past ASCII, which a collection of bytes can't hold.
std::optional<char> code_in_collection(std::string_view in, std::size_t from, std::size_t& end) {
    const char kind = in[from + 1];
    const int base = kind == 'd' ? 10 : kind == 'o' ? 8 : 16;
    const std::size_t most = kind == 'x' ? 2 : kind == 'u' ? 4 : kind == 'U' ? 8 : 3;
    unsigned value = 0;
    end = from + 2;
    while (end < in.size() && end - from - 2 < most) {
        const char d = in[end];
        const int digit = d >= '0' && d <= '9'   ? d - '0'
                          : d >= 'a' && d <= 'f' ? d - 'a' + 10
                          : d >= 'A' && d <= 'F' ? d - 'A' + 10
                                                 : 99;
        if (digit >= base) break;
        value = value * static_cast<unsigned>(base) + static_cast<unsigned>(digit);
        ++end;
    }
    if (end == from + 2 || value >= 0x80) return std::nullopt;
    return static_cast<char>(value);
}

} // namespace

ftk::Result<Pattern> read_pattern(std::string_view in, PatternSettings settings) {
    Magic magic = Magic::On;
    std::string out;
    // \c (false) or \C (true); and whether a letter A to Z is written as
    // itself, for smartcase.
    std::optional<bool> case_said;
    bool upper = false;
    // find's groups so far, and find's for each of Vim's; for each group
    // open, whether it captures.
    std::size_t made = 0;
    std::vector<std::size_t> groups{0};
    std::vector<bool> open;
    // What was read last: something a quantifier may follow, a quantifier,
    // or the start of a branch, where ^ is the line's start.
    bool atom = false;
    bool quantified = false;
    bool branch_start = true;
    // A | outside every group, the group \zs began, and \ze.
    bool alternation = false;
    std::optional<std::size_t> zs;
    bool ze = false;

    const auto emit = [&](std::string_view regex) {
        out += regex;
        atom = true;
        quantified = false;
        branch_start = false;
    };
    const auto mark = [&](std::string_view regex, bool starts_branch) {
        out += regex;
        atom = false;
        quantified = false;
        branch_start = starts_branch;
    };
    // Whether a branch ends at `i`: the pattern's end, | or ).
    const auto ends_branch = [&](std::size_t i) {
        if (i == in.size()) return true;
        if (magic == Magic::Very) return in[i] == '|' || in[i] == ')';
        return in[i] == '\\' && i + 1 < in.size() && (in[i + 1] == '|' || in[i + 1] == ')');
    };

    std::size_t at = 0;
    while (at < in.size()) {
        char c = in[at++];
        bool escaped = false;
        if (c == '\\') {
            // A backslash ending the pattern is itself.
            if (at == in.size()) {
                emit("\\\\");
                break;
            }
            escaped = true;
            c = in[at++];
        }
        // A letter past ASCII, whole, so a quantifier takes all its bytes.
        if (static_cast<unsigned char>(c) >= 0x80) {
            std::size_t end = at;
            while (end < in.size() && (static_cast<unsigned char>(in[end]) & 0xC0) == 0x80) ++end;
            emit("(?:" + std::string(in.substr(at - 1, end - at + 1)) + ")");
            at = end;
            continue;
        }
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (escaped && letter) {
            switch (c) {
                case 'v': magic = Magic::Very; continue;
                case 'm': magic = Magic::On; continue;
                case 'M': magic = Magic::Off; continue;
                case 'V': magic = Magic::VeryOff; continue;
                case 'c': case_said = false; continue;
                case 'C': case_said = true; continue;
                case 't': emit("\\t"); continue;
                case 'e': emit("\\x1b"); continue;
                case 'r': emit("\\r"); continue;
                case 'b': emit("\\x08"); continue;
                case 'n':
                case '_': return ftk::make_error(std::format("Patterns across lines aren't supported: \\{}", c));
                case 'z': {
                    const char which = at < in.size() ? in[at++] : '\0';
                    if (which != 's' && which != 'e') return ftk::make_error("E68: Invalid character after \\z");
                    if (!open.empty() || alternation || ze || (which == 's' && zs)) {
                        return ftk::make_error(std::format("\\z{} is supported once, outside groups and alternatives", which));
                    }
                    if (which == 's') {
                        // The match is a group of its own from here.
                        zs = ++made;
                        mark("(", false);
                    } else {
                        // What follows is looked ahead at, not taken.
                        if (zs) out += ")";
                        ze = true;
                        mark("(?=", false);
                    }
                    continue;
                }
                default: break;
            }
            if (c >= '1' && c <= '9') {
                const auto n = static_cast<std::size_t>(c - '0');
                if (n >= groups.size()) return ftk::make_error("E65: Illegal back reference");
                emit("\\" + std::to_string(groups[n]));
                continue;
            }
            bool known = false;
            for (const Class& kind : CLASSES) {
                if (kind.name != c) continue;
                emit(kind.regex);
                known = true;
                break;
            }
            if (!known) return ftk::make_error(std::format("\\{} isn't supported", c));
            continue;
        }

        if (!special(c, escaped, magic)) {
            if (!escaped && c >= 'A' && c <= 'Z') upper = true;
            emit(as_literal(c));
            continue;
        }
        switch (c) {
            case '(':
                open.push_back(true);
                groups.push_back(++made);
                mark("(", true);
                break;
            case '%':
                if (at < in.size() && in[at] == '(') {
                    ++at;
                    open.push_back(false);
                    mark("(?:", true);
                    break;
                }
                return ftk::make_error("Positions aren't supported: \\%");
            case ')':
                if (open.empty()) return ftk::make_error("E55: Unmatched \\)");
                open.pop_back();
                emit(")");
                break;
            case '|':
                if (open.empty()) {
                    if (zs || ze) return ftk::make_error("\\zs and \\ze are supported outside alternatives");
                    alternation = true;
                }
                mark("|", true);
                break;
            case '*':
            case '+':
            case '=':
            case '?':
            case '{': {
                // With nothing before it, the character itself.
                if (!atom) {
                    emit(as_literal(c));
                    break;
                }
                if (quantified) return ftk::make_error("E61: Nested quantifier");
                std::string quantifier = c == '*' ? "*" : c == '+' ? "+" : "?";
                if (c == '{') {
                    // \{n,m}, \{n}, \{n,}, \{,m}, \{}, and with - lazily.
                    std::size_t close = at;
                    while (close < in.size() && in[close] != '}' &&
                           !(in[close] == '\\' && close + 1 < in.size() && in[close + 1] == '}')) {
                        ++close;
                    }
                    if (close == in.size()) return ftk::make_error("E554: Syntax error in \\{...}");
                    std::string_view body = in.substr(at, close - at);
                    at = close + (in[close] == '\\' ? 2 : 1);
                    const bool lazy = body.starts_with('-');
                    if (lazy) body.remove_prefix(1);
                    const std::size_t comma = body.find(',');
                    const std::string_view from = body.substr(0, comma);
                    const std::string_view to = comma == std::string_view::npos ? std::string_view() : body.substr(comma + 1);
                    const auto digits = [](std::string_view s) { return s.find_first_not_of("0123456789") == std::string_view::npos; };
                    if (!digits(from) || !digits(to)) return ftk::make_error("E554: Syntax error in \\{...}");
                    if (comma == std::string_view::npos) {
                        quantifier = from.empty() ? "*" : std::format("{{{}}}", from);
                    } else {
                        const std::string least = from.empty() ? std::string("0") : std::string(from);
                        quantifier = to.empty() ? (least == "0" ? std::string("*") : std::format("{{{},}}", least))
                                                : std::format("{{{},{}}}", least, to);
                    }
                    if (lazy) quantifier += '?';
                }
                out += quantifier;
                quantified = true;
                break;
            }
            case '<': mark("\\b(?=\\w)", false); break;
            case '>': mark("\\b(?!\\w)", false); break;
            case '@': return ftk::make_error("Looking around isn't supported: \\@");
            case '&': return ftk::make_error("\\& isn't supported");
            case '~': return ftk::make_error("E33: No previous substitute regular expression");
            case '.': emit("."); break;
            case '^':
                if (branch_start) {
                    mark("^", false);
                } else {
                    emit("\\^");
                }
                break;
            case '$':
                if (ends_branch(at)) {
                    mark("$", false);
                } else {
                    emit("\\$");
                }
                break;
            case '[': {
                // A collection, its ] first taken as itself; without a ]
                // to close it, the [ is itself.
                std::size_t i = at;
                const bool negated = i < in.size() && in[i] == '^';
                if (negated) ++i;
                std::string items;
                bool first = true;
                bool closed = false;
                while (i < in.size()) {
                    const char k = in[i];
                    if (k == ']' && !first) {
                        closed = true;
                        ++i;
                        break;
                    }
                    first = false;
                    if (k == '[' && i + 1 < in.size() && in[i + 1] == ':') {
                        const std::size_t end = in.find(":]", i + 2);
                        if (end != std::string_view::npos) {
                            items += in.substr(i, end + 2 - i);
                            i = end + 2;
                            continue;
                        }
                    }
                    if (k == '\\' && i + 1 < in.size()) {
                        const char e = in[i + 1];
                        std::string_view written;
                        switch (e) {
                            case 'e': written = "\\x1b"; break;
                            case 't': written = "\\t"; break;
                            case 'r': written = "\\r"; break;
                            case 'b': written = "\\x08"; break;
                            case '\\': written = "\\\\"; break;
                            case ']': written = "\\]"; break;
                            case '^': written = "\\^"; break;
                            case '-': written = "\\-"; break;
                            case 'n': return ftk::make_error("Patterns across lines aren't supported: \\n");
                            default: break;
                        }
                        if (!written.empty()) {
                            items += written;
                            i += 2;
                            continue;
                        }
                        if (e == 'd' || e == 'o' || e == 'x' || e == 'u' || e == 'U') {
                            std::size_t end = i;
                            const std::optional<char> code = code_in_collection(in, i, end);
                            if (!code) return ftk::make_error("A collection holds ASCII's characters only");
                            items += std::format("\\x{:02x}", static_cast<unsigned>(*code));
                            i = end;
                            continue;
                        }
                        // Any other backslash is itself.
                        items += "\\\\";
                        ++i;
                        continue;
                    }
                    if (static_cast<unsigned char>(k) >= 0x80) {
                        return ftk::make_error("A collection holds ASCII's characters only");
                    }
                    if (k >= 'A' && k <= 'Z') upper = true;
                    if (k == '[' || k == ']' || k == '\\') items += '\\';
                    items += k;
                    ++i;
                }
                if (!closed) {
                    emit("\\[");
                    break;
                }
                at = i;
                emit("[" + std::string(negated ? "^" : "") + items + "]");
                break;
            }
            default: emit(as_literal(c)); break;
        }
    }
    if (!open.empty()) return ftk::make_error("E54: Unmatched \\(");
    if (ze || zs) out += ")";
    if (zs) groups[0] = *zs;

    Pattern pattern{.regex = std::move(out),
                    .options = {.match_case = case_said.value_or(!settings.ignore_case || (settings.smart_case && upper)),
                                .whole_word = false,
                                .regex = true,
                                .group = zs.value_or(0)},
                    .groups = std::move(groups)};
    if (!regex_valid(pattern.regex)) return ftk::make_error("The pattern doesn't read");
    return pattern;
}

std::optional<std::vector<std::string>> captured(const Buffer& buffer, Span match, const Pattern& pattern) {
    const std::optional<std::vector<std::string>> found = fjedit::captured(buffer, match, pattern.regex, pattern.options);
    if (!found) return std::nullopt;
    std::vector<std::string> taken;
    for (const std::size_t group : pattern.groups) taken.push_back(group < found->size() ? (*found)[group] : std::string());
    return taken;
}

std::string with_previous(std::string_view with, std::string_view previous) {
    std::string text;
    for (std::size_t i = 0; i < with.size(); ++i) {
        if (with[i] == '\\' && i + 1 < with.size()) {
            text += with[i];
            text += with[++i];
        } else if (with[i] == '~') {
            text += previous;
        } else {
            text += with[i];
        }
    }
    return text;
}

std::string expand_replacement(std::string_view with, std::span<const std::string> taken, std::string_view previous) {
    // A ~ first: the previous substitute's text.
    const std::string text = with_previous(with, previous);

    // The case \u or \l gives the next letter, and \U or \L every one.
    enum class Case : uint8_t { As, Upper, Lower };
    Case next = Case::As;
    Case every = Case::As;
    std::string out;
    const auto put = [&](char ch) {
        const Case use = next != Case::As ? std::exchange(next, Case::As) : every;
        if (use == Case::Upper && ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        if (use == Case::Lower && ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        out += ch;
    };
    const auto put_all = [&](std::string_view part) {
        for (const char ch : part) put(ch);
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '&') {
            if (!taken.empty()) put_all(taken[0]);
            continue;
        }
        if (ch != '\\' || i + 1 == text.size()) {
            put(ch);
            continue;
        }
        const char e = text[++i];
        if (e >= '0' && e <= '9') {
            const auto n = static_cast<std::size_t>(e - '0');
            if (n < taken.size()) put_all(taken[n]);
        } else if (e == 'r' || e == 'n') {
            out += '\n';
        } else if (e == 't') {
            out += '\t';
        } else if (e == 'u' || e == 'l') {
            next = e == 'u' ? Case::Upper : Case::Lower;
        } else if (e == 'U' || e == 'L') {
            every = e == 'U' ? Case::Upper : Case::Lower;
        } else if (e == 'E' || e == 'e') {
            every = Case::As;
        } else {
            put(e);
        }
    }
    return out;
}

} // namespace fjedit::vim
