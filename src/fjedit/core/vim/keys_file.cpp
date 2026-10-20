#include "fjedit/core/vim/keys_file.hpp"

#include "fjedit/core/vim/notation.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <optional>

namespace fjedit::vim {

namespace {

constexpr MapModes NVO = MapModes::Normal | MapModes::Visual | MapModes::OperatorPending;
constexpr MapModes IC = MapModes::Insert | MapModes::Command;

// A map command: its whole name, the least of it Vim takes, the modes it
// maps in, and whether its map plays its keys through the maps again.
struct MapCommand {
    std::string_view whole;
    std::string_view shortest;
    MapModes modes;
    bool recursive;
};
constexpr MapCommand MAP_COMMANDS[] = {
    {"map", "map", NVO, true},
    {"nmap", "nm", MapModes::Normal, true},
    {"vmap", "vm", MapModes::Visual, true},
    {"xmap", "xm", MapModes::Visual, true},
    {"omap", "om", MapModes::OperatorPending, true},
    {"imap", "im", MapModes::Insert, true},
    {"cmap", "cm", MapModes::Command, true},
    {"noremap", "no", NVO, false},
    {"nnoremap", "nn", MapModes::Normal, false},
    {"vnoremap", "vn", MapModes::Visual, false},
    {"xnoremap", "xn", MapModes::Visual, false},
    {"onoremap", "ono", MapModes::OperatorPending, false},
    {"inoremap", "ino", MapModes::Insert, false},
    {"cnoremap", "cno", MapModes::Command, false},
};

// The keymap modes a bind line may name.
constexpr std::array<std::string_view, 7> BIND_MODES = {"normal", "visual",  "insert", "command",
                                                        "completing", "finding", "program"};

bool blank(char c) {
    return c == ' ' || c == '\t';
}

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && blank(text.front())) text.remove_prefix(1);
    while (!text.empty() && (blank(text.back()) || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// Whether `text` at `at` begins with `word`, letters in any case.
bool begins_with(std::string_view text, std::size_t at, std::string_view word) {
    if (text.size() - std::min(at, text.size()) < word.size()) return false;
    for (std::size_t i = 0; i < word.size(); ++i) {
        if (lower(text[at + i]) != lower(word[i])) return false;
    }
    return true;
}

// Where `word` begins in `text` from `at` on, letters in any case.
std::size_t find_word(std::string_view text, std::string_view word, std::size_t at = 0) {
    for (; at + word.size() <= text.size(); ++at) {
        if (begins_with(text, at, word)) return at;
    }
    return std::string_view::npos;
}

// `text` with each <leader> and <localleader> the leaders in force.
std::string with_leaders(std::string_view text, std::string_view leader, std::string_view local_leader) {
    std::string out;
    for (std::size_t at = 0; at < text.size();) {
        if (begins_with(text, at, "<leader>")) {
            out += leader;
            at += 8;
        } else if (begins_with(text, at, "<localleader>")) {
            out += local_leader;
            at += 13;
        } else {
            out += text[at++];
        }
    }
    return out;
}

// What `let` gives a leader: a string in single quotes as written, or in
// double quotes with \<…> a key's name and \\ a backslash; as notation.
std::optional<std::string> leader_value(std::string_view value) {
    if (value.size() < 2 || value.front() != value.back() || (value.front() != '"' && value.front() != '\'')) {
        return std::nullopt;
    }
    const bool escapes = value.front() == '"';
    const std::string_view inside = value.substr(1, value.size() - 2);
    std::string out;
    for (std::size_t at = 0; at < inside.size(); ++at) {
        const char c = inside[at];
        if (escapes && c == '\\' && at + 1 < inside.size()) {
            if (inside[at + 1] == '<') {
                // \<Space>: the key the name names, as notation.
                const std::size_t close = inside.find('>', at);
                if (close == std::string_view::npos) return std::nullopt;
                out += inside.substr(at + 1, close - at);
                at = close;
                continue;
            }
            out += inside[at + 1] == '\\' ? "<Bslash>" : std::string(1, inside[at + 1]);
            ++at;
            continue;
        }
        out += c == '<' ? std::string("<lt>") : std::string(1, c);
    }
    if (out.empty()) return std::nullopt;
    return out;
}

// What a map does, `text` its right side: keys, and `:` lines between
// <Cmd> and <CR>; <Nop> nothing. An error for what doesn't read.
std::optional<std::vector<MapStep>> map_steps(std::string_view text, std::string& error) {
    std::vector<MapStep> steps;
    if (text.size() == 5 && begins_with(text, 0, "<Nop>")) return steps;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t command = find_word(text, "<Cmd>", at);
        const std::string_view keys = text.substr(at, (command == std::string_view::npos ? text.size() : command) - at);
        if (!keys.empty()) {
            std::optional<std::vector<Chord>> chords = parse_notation(keys);
            if (!chords) {
                error = std::format("Can't read the keys: {}", keys);
                return std::nullopt;
            }
            steps.push_back({.keys = std::move(*chords), .command = {}, .is_command = false});
        }
        if (command == std::string_view::npos) break;
        const std::size_t begin = command + 5;
        const std::size_t end = find_word(text, "<CR>", begin);
        if (end == std::string_view::npos) {
            error = "<Cmd> needs a <CR> to end its command";
            return std::nullopt;
        }
        steps.push_back({.keys = {}, .command = std::string(text.substr(begin, end - begin)), .is_command = true});
        at = end + 4;
    }
    return steps;
}

} // namespace

KeysFile read_keys_file(std::string_view text) {
    KeysFile out;
    std::string leader = "<Space>";
    std::string local_leader = "<Bslash>";
    std::size_t number = 0;
    for (std::size_t from = 0; from <= text.size();) {
        const std::size_t end = std::min(text.find('\n', from), text.size());
        std::string_view line = trimmed(text.substr(from, end - from));
        from = end + 1;
        ++number;
        const auto fail = [&](std::string message) { out.errors.push_back({.line = number, .message = std::move(message)}); };
        while (!line.empty() && (line.front() == ':' || blank(line.front()))) line.remove_prefix(1);
        if (line.empty() || line.front() == '"') continue;

        std::size_t at = 0;
        while (at < line.size() && ((line[at] >= 'a' && line[at] <= 'z') || (line[at] >= 'A' && line[at] <= 'Z'))) ++at;
        const std::string_view word = line.substr(0, at);
        const bool bang = at < line.size() && line[at] == '!';
        std::string_view rest = trimmed(line.substr(at + (bang ? 1 : 0)));

        if (word == "let") {
            const std::size_t equals = rest.find('=');
            std::string_view name = trimmed(rest.substr(0, std::min(equals, rest.size())));
            if (name.starts_with("g:")) name.remove_prefix(2);
            if (name != "mapleader" && name != "maplocalleader") {
                fail(std::format("Not supported yet: let {}", name));
                continue;
            }
            const std::optional<std::string> value =
                equals == std::string_view::npos ? std::nullopt : leader_value(trimmed(rest.substr(equals + 1)));
            if (!value) {
                fail(std::format("A leader is a string in quotes: let {} = \" \"", name));
                continue;
            }
            (name == "mapleader" ? leader : local_leader) = *value;
            continue;
        }
        if (word == "set" || word == "se") {
            if (rest.empty()) {
                fail("Nothing to set");
                continue;
            }
            out.settings.push_back({.argument = std::string(rest), .line = number});
            continue;
        }
        if (word == "bind") {
            // Three words: the mode, the keys, the command.
            std::vector<std::string_view> words;
            for (std::size_t i = 0; i < rest.size();) {
                while (i < rest.size() && blank(rest[i])) ++i;
                const std::size_t start = i;
                while (i < rest.size() && !blank(rest[i])) ++i;
                if (i > start) words.push_back(rest.substr(start, i - start));
            }
            if (words.size() != 3) {
                fail("A bind line is: bind <mode> <keys> <command>");
                continue;
            }
            if (std::ranges::find(BIND_MODES, words[0]) == BIND_MODES.end()) {
                fail(std::format("No mode called {} to bind in", words[0]));
                continue;
            }
            std::optional<std::vector<Chord>> keys = parse_notation(with_leaders(words[1], leader, local_leader));
            if (!keys || keys->empty()) {
                fail(std::format("Can't read the keys: {}", words[1]));
                continue;
            }
            out.binds.push_back({.mode = std::string(words[0]), .keys = std::move(*keys),
                                 .command = std::string(words[2]), .line = number});
            continue;
        }

        // A map command, as short as Vim takes it; map! and noremap! map in
        // insert mode and on the command line.
        const auto command = std::ranges::find_if(MAP_COMMANDS, [&](const MapCommand& c) {
            return word.starts_with(c.shortest) && c.whole.starts_with(word);
        });
        if (command == std::end(MAP_COMMANDS) || (bang && command->modes != NVO)) {
            fail(std::format("Not supported here: {}{}", word, bang ? "!" : ""));
            continue;
        }
        Mapping map{.modes = bang ? IC : command->modes, .keys = {}, .does = {}, .recursive = command->recursive,
                    .waits = true, .line = number};
        // Its options, each as <name>.
        bool refused = false;
        while (rest.starts_with('<')) {
            const std::size_t close = rest.find('>');
            if (close == std::string_view::npos) break;
            const std::string_view option = rest.substr(1, close - 1);
            const auto is = [&](std::string_view name) { return option.size() == name.size() && begins_with(option, 0, name); };
            // <silent> and <unique> change nothing here.
            if (is("nowait")) {
                map.waits = false;
            } else if (is("expr") || is("buffer") || is("script")) {
                fail(std::format("Not supported yet: <{}>", option));
                refused = true;
                break;
            } else if (!is("silent") && !is("unique")) {
                break;
            }
            rest = trimmed(rest.substr(close + 1));
        }
        if (refused) continue;
        // The keys to the first blank, then what they do.
        std::size_t keys_end = 0;
        while (keys_end < rest.size() && !blank(rest[keys_end])) ++keys_end;
        const std::string_view keys_text = rest.substr(0, keys_end);
        const std::string_view does_text = trimmed(rest.substr(keys_end));
        if (keys_text.empty() || does_text.empty()) {
            fail("A map is: the keys, then what they do");
            continue;
        }
        if (does_text.find('|') != std::string_view::npos || keys_text.find('|') != std::string_view::npos) {
            fail("Write | as <Bar> in a map");
            continue;
        }
        std::optional<std::vector<Chord>> keys = parse_notation(with_leaders(keys_text, leader, local_leader));
        if (!keys || keys->empty()) {
            fail(std::format("Can't read the keys: {}", keys_text));
            continue;
        }
        std::string error;
        std::optional<std::vector<MapStep>> steps = map_steps(with_leaders(does_text, leader, local_leader), error);
        if (!steps) {
            fail(std::move(error));
            continue;
        }
        map.keys = std::move(*keys);
        map.does = std::move(*steps);
        out.maps.push_back(std::move(map));
    }
    return out;
}

} // namespace fjedit::vim
