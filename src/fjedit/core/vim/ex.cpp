#include "fjedit/core/vim/ex.hpp"

#include <algorithm>
#include <format>

namespace fjedit::vim {

namespace {

// A command's whole name, and the least of it that names it.
struct Name {
    std::string_view whole;
    std::string_view shortest;
};
constexpr Name NAMES[] = {
    {"bnext", "bn"},  {"bprevious", "bp"}, {"close", "clo"},  {"copy", "co"},          {"delete", "d"},
    {"edit", "e"},    {"global", "g"},     {"move", "m"},     {"nohlsearch", "noh"},   {"normal", "norm"},
    {"qall", "qa"},   {"quit", "q"},       {"set", "se"},     {"sort", "sor"},         {"split", "sp"},
    {"substitute", "s"}, {"t", "t"},       {"vglobal", "v"},  {"vsplit", "vs"},        {"wall", "wa"},
    {"wincmd", "winc"}, {"wq", "wq"},      {"write", "w"},    {"xit", "x"},            {"yank", "y"},
};
constexpr std::string_view COMMANDS[] = {
    "bnext", "bprevious", "close", "copy", "delete", "edit", "global", "move", "nohlsearch", "normal", "qall", "quit",
    "set", "sort", "split", "substitute", "t", "vglobal", "vsplit", "wall", "wincmd", "wq", "write", "xit", "yank",
};

bool digit(char c) {
    return c >= '0' && c <= '9';
}

bool letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

void skip_blanks(std::string_view text, std::size_t& at) {
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) ++at;
}

// The number from `at` on, `at` past it.
std::size_t number(std::string_view text, std::size_t& at) {
    std::size_t value = 0;
    while (at < text.size() && digit(text[at])) {
        value = std::min<std::size_t>(value * 10 + static_cast<std::size_t>(text[at] - '0'), 999999999);
        ++at;
    }
    return value;
}

// An address from `at` on, `at` left past it: nothing when none begins
// there, an error when one begun doesn't read.
ftk::Result<std::optional<ExAddress>> address(std::string_view text, std::size_t& at) {
    skip_blanks(text, at);
    ExAddress out;
    bool any = false;
    if (at < text.size()) {
        const char c = text[at];
        if (digit(c)) {
            out.kind = ExAddress::Kind::Number;
            out.number = number(text, at);
            any = true;
        } else if (c == '.' || c == '$') {
            out.kind = c == '.' ? ExAddress::Kind::Current : ExAddress::Kind::Last;
            ++at;
            any = true;
        } else if (c == '\'') {
            if (at + 1 >= text.size()) return ftk::make_error("E78: Unknown mark");
            out.kind = ExAddress::Kind::Mark;
            out.mark = text[at + 1];
            at += 2;
            any = true;
        } else if (c == '/' || c == '?') {
            // A pattern to the next of the same character not escaped.
            std::size_t i = at + 1;
            while (i < text.size() && text[i] != c) i += text[i] == '\\' ? 2 : 1;
            out.kind = c == '/' ? ExAddress::Kind::Forward : ExAddress::Kind::Backward;
            out.pattern = std::string(text.substr(at + 1, std::min(i, text.size()) - at - 1));
            at = i < text.size() ? i + 1 : text.size();
            any = true;
        }
    }
    // + and -, each with a number or one.
    while (at < text.size() && (text[at] == '+' || text[at] == '-')) {
        const bool plus = text[at] == '+';
        ++at;
        long lines = 1;
        if (at < text.size() && digit(text[at])) lines = static_cast<long>(number(text, at));
        out.offset += plus ? lines : -lines;
        any = true;
    }
    if (!any) return std::optional<ExAddress>{};
    return std::optional<ExAddress>{std::move(out)};
}

} // namespace

ftk::Result<ExCommand> parse_ex(std::string_view line) {
    ExCommand out;
    std::size_t at = 0;
    while (at < line.size() && (line[at] == ':' || line[at] == ' ' || line[at] == '\t')) ++at;

    // The range.
    if (at < line.size() && line[at] == '%') {
        out.range.whole = true;
        ++at;
    } else {
        ftk::Result<std::optional<ExAddress>> first = address(line, at);
        if (!first) return std::unexpected(first.error());
        out.range.from = std::move(*first);
        skip_blanks(line, at);
        if (at < line.size() && (line[at] == ',' || line[at] == ';')) {
            out.range.from_first = line[at] == ';';
            ++at;
            if (!out.range.from) out.range.from = ExAddress{};
            ftk::Result<std::optional<ExAddress>> second = address(line, at);
            if (!second) return std::unexpected(second.error());
            out.range.to = second->value_or(ExAddress{});
        }
    }
    skip_blanks(line, at);

    // The name: letters, or a run of > or <, whose length is how far it
    // shifts.
    const std::size_t begin = at;
    if (at < line.size() && (line[at] == '>' || line[at] == '<')) {
        const char shift = line[at];
        while (at < line.size() && line[at] == shift) ++at;
        out.name = std::string(1, shift);
        out.argument = std::string(at - begin - 1, shift);
        skip_blanks(line, at);
        out.argument += line.substr(at);
        return out;
    }
    while (at < line.size() && letter(line[at])) ++at;
    const std::string_view typed = line.substr(begin, at - begin);
    if (!typed.empty()) {
        const Name* found = nullptr;
        for (const Name& name : NAMES) {
            if (typed.starts_with(name.shortest) && name.whole.starts_with(typed)) {
                found = &name;
                break;
            }
        }
        if (found == nullptr) return ftk::make_error(std::format("E492: Not an editor command: {}", line));
        out.name = found->whole;
        if (at < line.size() && line[at] == '!') {
            out.bang = true;
            ++at;
        }
    } else if (at < line.size()) {
        return ftk::make_error(std::format("E492: Not an editor command: {}", line));
    }
    skip_blanks(line, at);
    out.argument = std::string(line.substr(at));
    return out;
}

std::span<const std::string_view> ex_commands() {
    return COMMANDS;
}

} // namespace fjedit::vim
