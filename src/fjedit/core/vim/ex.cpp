#include "fjedit/core/vim/ex.hpp"

#include "fjedit/core/vim/registers.hpp"

#include <algorithm>
#include <charconv>
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
// Each command, with what it does in a few words, for a list of them.
constexpr ExCommandName COMMANDS[] = {
    {"bnext", "Next tab"},
    {"bprevious", "Previous tab"},
    {"close", "Close"},
    {"copy", "Put a copy of lines below a line"},
    {"delete", "Delete lines"},
    {"edit", "Open a file, or read this one again"},
    {"global", "Run a command on the lines with a pattern"},
    {"move", "Move lines below a line"},
    {"nohlsearch", "Hide the search's matches"},
    {"normal", "Type keys in normal mode"},
    {"qall", "Quit"},
    {"quit", "Close"},
    {"set", "Change a setting"},
    {"sort", "Sort lines"},
    {"split", "Split"},
    {"substitute", "Replace a pattern"},
    {"t", "Put a copy of lines below a line"},
    {"vglobal", "Run a command on the lines without a pattern"},
    {"vsplit", "Split"},
    {"wall", "Save all"},
    {"wincmd", "Go to another group"},
    {"wq", "Save and close"},
    {"write", "Save"},
    {"xit", "Save if it changed, and close"},
    {"yank", "Copy lines"},
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

// The range a line begins with, after any `:` and blanks, `at` left at
// what follows it and its blanks.
ftk::Result<ExRange> read_range(std::string_view line, std::size_t& at) {
    ExRange range;
    while (at < line.size() && (line[at] == ':' || line[at] == ' ' || line[at] == '\t')) ++at;
    if (at < line.size() && line[at] == '%') {
        range.whole = true;
        ++at;
    } else {
        ftk::Result<std::optional<ExAddress>> first = address(line, at);
        if (!first) return std::unexpected(first.error());
        range.from = std::move(*first);
        skip_blanks(line, at);
        if (at < line.size() && (line[at] == ',' || line[at] == ';')) {
            range.from_first = line[at] == ';';
            ++at;
            if (!range.from) range.from = ExAddress{};
            ftk::Result<std::optional<ExAddress>> second = address(line, at);
            if (!second) return std::unexpected(second.error());
            range.to = second->value_or(ExAddress{});
        }
    }
    skip_blanks(line, at);
    return range;
}

} // namespace

ftk::Result<ExCommand> parse_ex(std::string_view line) {
    ExCommand out;
    std::size_t at = 0;
    ftk::Result<ExRange> range = read_range(line, at);
    if (!range) return std::unexpected(range.error());
    out.range = std::move(*range);

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

std::span<const ExCommandName> ex_commands() {
    return COMMANDS;
}

std::optional<NameTyped> name_typed(std::string_view line) {
    std::size_t at = 0;
    if (!read_range(line, at)) return std::nullopt;
    const std::size_t begin = at;
    while (at < line.size() && letter(line[at])) ++at;
    if (at == begin || at != line.size()) return std::nullopt;
    NameTyped out{.at = begin, .typed = line.substr(begin), .names = {}};
    for (const ExCommandName& command : COMMANDS) {
        if (command.name.starts_with(out.typed)) out.names.push_back(command);
    }
    if (out.names.empty()) return std::nullopt;
    return out;
}

std::optional<PathTyped> path_typed(std::string_view line) {
    std::size_t at = 0;
    if (!read_range(line, at)) return std::nullopt;
    const std::size_t begin = at;
    while (at < line.size() && letter(line[at])) ++at;
    // :e to :edit, as parse_ex() reads them.
    const std::string_view name = line.substr(begin, at - begin);
    if (name.empty() || !std::string_view("edit").starts_with(name)) return std::nullopt;
    if (at < line.size() && line[at] == '!') ++at;
    if (at == line.size() || line[at] != ' ') return std::nullopt;
    skip_blanks(line, at);
    const std::string_view path = line.substr(at);
    const std::size_t slash = path.rfind('/');
    const std::size_t split = slash == std::string_view::npos ? 0 : slash + 1;
    return PathTyped{.at = at, .folder = path.substr(0, split), .name = path.substr(split)};
}

Delimited split_pattern(std::string_view text, char delimiter) {
    std::size_t at = 0;
    while (at < text.size()) {
        const char c = text[at];
        if (c == '\\') {
            at += 2;
        } else if (c == '[') {
            // A collection, its ] first taken as itself; without one, the [
            // is itself.
            std::size_t end = at + 1;
            if (end < text.size() && text[end] == '^') ++end;
            if (end < text.size() && text[end] == ']') ++end;
            while (end < text.size() && text[end] != ']') end += text[end] == '\\' ? 2 : 1;
            at = end < text.size() ? end + 1 : at + 1;
        } else if (c == delimiter) {
            return {.pattern = text.substr(0, at), .rest = text.substr(at + 1), .ended = true};
        } else {
            ++at;
        }
    }
    return {.pattern = text, .rest = {}, .ended = false};
}

bool pattern_delimiter(char c) {
    return !(letter(c) || digit(c) || c == ' ' || c == '\\' || c == '"' || c == '|' || c == '&');
}

ftk::Result<std::optional<std::size_t>> read_count(std::string_view argument) {
    while (!argument.empty() && argument.front() == ' ') argument.remove_prefix(1);
    if (argument.empty()) return std::optional<std::size_t>{};
    if (!std::ranges::all_of(argument, digit)) return ftk::make_error(std::format("E488: Trailing characters: {}", argument));
    std::size_t at = 0;
    const std::size_t n = number(argument, at);
    if (n == 0) return ftk::make_error("E939: Positive count required");
    return std::optional<std::size_t>{n};
}

ftk::Result<RegisterCount> read_register_count(std::string_view argument) {
    RegisterCount out;
    if (!argument.empty() && !digit(argument.front()) && Registers::valid(argument.front())) {
        out.reg = argument.front();
        argument.remove_prefix(1);
    }
    ftk::Result<std::optional<std::size_t>> count = read_count(argument);
    if (!count) return std::unexpected(count.error());
    out.count = *count;
    return out;
}

ftk::Result<Shift> read_shift(char shift, std::string_view argument) {
    Shift out;
    while (!argument.empty() && argument.front() == shift) {
        ++out.levels;
        argument.remove_prefix(1);
    }
    ftk::Result<std::optional<std::size_t>> count = read_count(argument);
    if (!count) return std::unexpected(count.error());
    out.count = *count;
    return out;
}

ftk::Result<ExAddress> read_target(std::string_view argument) {
    ftk::Result<ExCommand> target = parse_ex(argument);
    if (!target || !target->range.from || target->range.to || target->range.whole || !target->name.empty()) {
        return ftk::make_error("E14: Invalid address");
    }
    return std::move(*target->range.from);
}

ftk::Result<SubstituteArgument> read_substitute(std::string_view argument) {
    SubstituteArgument out;
    std::string_view flags = argument;
    if (!argument.empty() && pattern_delimiter(argument.front())) {
        const char delimiter = argument.front();
        const Delimited written = split_pattern(argument.substr(1), delimiter);
        // The replacement, to the next delimiter not escaped.
        const std::string_view rest = written.rest;
        std::size_t end = 0;
        while (end < rest.size() && rest[end] != delimiter) end += rest[end] == '\\' ? 2 : 1;
        end = std::min(end, rest.size());
        out.written = SubstituteArgument::Written{.pattern = std::string(written.pattern),
                                                  .with = std::string(rest.substr(0, end))};
        flags = end < rest.size() ? rest.substr(end + 1) : std::string_view{};
    }
    while (!flags.empty() && flags.front() != ' ' && !digit(flags.front())) {
        switch (flags.front()) {
            case '&': out.every = SubstituteArgument::Every::AsLast; break;
            case 'g': out.every = SubstituteArgument::Every::Yes; break;
            case 'i': out.match_case = false; break;
            case 'I': out.match_case = true; break;
            case 'e': out.quiet = true; break;
            default: return ftk::make_error(std::format("E488: Trailing characters: {}", flags));
        }
        flags.remove_prefix(1);
    }
    ftk::Result<std::optional<std::size_t>> count = read_count(flags);
    if (!count) return std::unexpected(count.error());
    out.count = *count;
    return out;
}

ftk::Result<GlobalArgument> read_global(std::string_view argument) {
    if (argument.empty() || !pattern_delimiter(argument.front())) return ftk::make_error("E476: Invalid command");
    const Delimited written = split_pattern(argument.substr(1), argument.front());
    return GlobalArgument{.pattern = std::string(written.pattern), .command = std::string(written.rest)};
}

ftk::Result<SetItem> read_set_item(std::string_view item) {
    using Kind = SetItem::Kind;
    struct Named {
        std::string_view whole;
        std::string_view short_name;
        SetOption option;
        Kind kind;
    };
    static constexpr Named OPTIONS[] = {
        {"number", "nu", SetOption::Number, Kind::OnOff},
        {"relativenumber", "rnu", SetOption::RelativeNumber, Kind::OnOff},
        {"ignorecase", "ic", SetOption::IgnoreCase, Kind::OnOff},
        {"smartcase", "scs", SetOption::SmartCase, Kind::OnOff},
        {"hlsearch", "hls", SetOption::HlSearch, Kind::OnOff},
        {"incsearch", "is", SetOption::IncSearch, Kind::OnOff},
        {"expandtab", "et", SetOption::ExpandTab, Kind::OnOff},
        {"shiftwidth", "sw", SetOption::ShiftWidth, Kind::Number},
        {"tabstop", "ts", SetOption::TabStop, Kind::Number},
        {"scrolloff", "so", SetOption::ScrollOff, Kind::Number},
        {"timeoutlen", "tm", SetOption::TimeoutLen, Kind::Number},
        {"clipboard", "cb", SetOption::Clipboard, Kind::Text},
    };
    std::string_view name = item;
    std::optional<SetItem::Action> action;
    std::optional<std::string_view> value;
    if (const std::size_t equals = name.find('='); equals != std::string_view::npos) {
        value = name.substr(equals + 1);
        name = name.substr(0, equals);
    } else if (name.ends_with('?')) {
        action = SetItem::Action::Ask;
        name.remove_suffix(1);
    } else if (name.ends_with('!')) {
        action = SetItem::Action::Toggle;
        name.remove_suffix(1);
    }
    const auto find = [&](std::string_view written) {
        return std::ranges::find_if(OPTIONS, [&](const Named& o) { return o.whole == written || o.short_name == written; });
    };
    auto found = find(name);
    if (found == std::end(OPTIONS) && !value && !action) {
        if (name.starts_with("no") && find(name.substr(2)) != std::end(OPTIONS)) {
            found = find(name.substr(2));
            action = SetItem::Action::Off;
        } else if (name.starts_with("inv") && find(name.substr(3)) != std::end(OPTIONS)) {
            found = find(name.substr(3));
            action = SetItem::Action::Toggle;
        }
    }
    if (found == std::end(OPTIONS)) return ftk::make_error(std::format("E518: Unknown option: {}", item));
    SetItem out{.option = found->option, .name = found->whole, .kind = found->kind, .action = SetItem::Action::On,
                .value = 0, .text = {}};
    if (found->kind == Kind::OnOff) {
        if (value) return ftk::make_error(std::format("E474: Invalid argument: {}", item));
        out.action = action.value_or(SetItem::Action::On);
        return out;
    }
    // A number or text: shown when asked for or named alone, set after =.
    if (action == SetItem::Action::Off || action == SetItem::Action::Toggle) {
        return ftk::make_error(std::format("E474: Invalid argument: {}", item));
    }
    if (!value) {
        out.action = SetItem::Action::Ask;
        return out;
    }
    if (found->kind == Kind::Text) {
        // The clipboard's: the system's for the unnamed register, or not.
        constexpr std::string_view CLIPBOARDS[] = {"", "unnamed", "unnamedplus", "unnamed,unnamedplus",
                                                   "unnamedplus,unnamed"};
        if (!std::ranges::contains(CLIPBOARDS, *value)) {
            return ftk::make_error(std::format("E474: Invalid argument: {}", item));
        }
        out.action = SetItem::Action::Value;
        out.text = *value;
        return out;
    }
    if (value->empty() || !std::ranges::all_of(*value, digit) ||
        std::from_chars(value->data(), value->data() + value->size(), out.value).ec != std::errc{}) {
        return ftk::make_error(std::format("E521: Number required after =: {}", item));
    }
    if (out.value == 0 && out.option != SetOption::ScrollOff && out.option != SetOption::TimeoutLen) {
        return ftk::make_error(std::format("E487: Argument must be positive: {}", item));
    }
    out.action = SetItem::Action::Value;
    return out;
}

ftk::Result<SortArgument> read_sort(std::string_view argument) {
    SortArgument out;
    for (const char flag : argument) {
        if (flag == ' ') continue;
        if (flag == 'i') {
            out.fold_case = true;
        } else if (flag == 'n') {
            out.by_number = true;
        } else if (flag == 'u') {
            out.unique = true;
        } else if (flag == '/') {
            return ftk::make_error("Not supported yet: :sort with a pattern");
        } else {
            return ftk::make_error(std::format("E474: Invalid argument: {}", argument));
        }
    }
    return out;
}

} // namespace fjedit::vim
