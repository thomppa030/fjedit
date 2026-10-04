#include "fjedit/core/git_ignore.hpp"

#include <cstdlib>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace fjedit {

namespace {

namespace fs = std::filesystem;

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

char upper(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

bool same(char a, char b, bool fold) {
    return fold ? lower(a) == lower(b) : a == b;
}

std::string_view trimmed(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

std::string lowered(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = lower(c);
    return out;
}

// Whether `c` is of the named class of a bracket expression ("alpha").
bool of_class(std::string_view name, char c) {
    const bool is_upper = c >= 'A' && c <= 'Z';
    const bool is_lower = c >= 'a' && c <= 'z';
    const bool is_digit = c >= '0' && c <= '9';
    if (name == "alpha") return is_upper || is_lower;
    if (name == "digit") return is_digit;
    if (name == "alnum") return is_upper || is_lower || is_digit;
    if (name == "upper") return is_upper;
    if (name == "lower") return is_lower;
    if (name == "space") return c == ' ' || (c >= '\t' && c <= '\r');
    if (name == "xdigit") return is_digit || (lower(c) >= 'a' && lower(c) <= 'f');
    if (name == "punct") return c > ' ' && c < 0x7F && !is_upper && !is_lower && !is_digit;
    return false;
}

// Whether the bracket expression whose text begins at `at`, just past its
// [, takes `c`, and `at` moved past its ]. Nothing when no ] closes it, and
// the [ stands for itself.
std::optional<bool> in_brackets(std::string_view pattern, std::size_t& at, char c, bool fold) {
    std::size_t i = at;
    const bool negated = i < pattern.size() && (pattern[i] == '!' || pattern[i] == '^');
    if (negated) ++i;
    bool taken = false;
    for (bool first = true; i < pattern.size(); first = false) {
        char low = pattern[i];
        if (low == ']' && !first) {
            at = i + 1;
            return taken != negated;
        }
        if (low == '[' && i + 1 < pattern.size() && pattern[i + 1] == ':') {
            const std::size_t end = pattern.find(":]", i + 2);
            if (end != std::string_view::npos) {
                taken = taken || of_class(pattern.substr(i + 2, end - i - 2), c);
                i = end + 2;
                continue;
            }
        }
        if (low == '\\' && i + 1 < pattern.size()) low = pattern[++i];
        if (i + 2 < pattern.size() && pattern[i + 1] == '-' && pattern[i + 2] != ']') {
            std::size_t high_at = i + 2;
            if (pattern[high_at] == '\\' && high_at + 1 < pattern.size()) ++high_at;
            const char high = pattern[high_at];
            const auto within = [&](char x) { return low <= x && x <= high; };
            taken = taken || within(c) || (fold && (within(lower(c)) || within(upper(c))));
            i = high_at + 1;
            continue;
        }
        taken = taken || same(low, c, fold);
        ++i;
    }
    return std::nullopt;
}

// Whether `pattern` matches the whole of `text`, a path whose / only a /
// matches: * is any run within a folder's name, ? one character of it,
// [...] one of a set, \ takes the next character as itself, and ** as a
// whole part of the path is any number of folders.
bool matches(std::string_view pattern, std::string_view text, bool fold) {
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < pattern.size()) {
        const char c = pattern[i];
        if (c == '*') {
            std::size_t after = i;
            while (after < pattern.size() && pattern[after] == '*') ++after;
            const bool whole = after - i >= 2 && (i == 0 || pattern[i - 1] == '/') &&
                               (after == pattern.size() || pattern[after] == '/');
            if (whole) {
                // At the end, everything under; otherwise none or more
                // folders, then the rest.
                if (after == pattern.size()) return true;
                const std::string_view rest = pattern.substr(after + 1);
                for (std::size_t from = j;;) {
                    if (matches(rest, text.substr(from), fold)) return true;
                    const std::size_t slash = text.find('/', from);
                    if (slash == std::string_view::npos) return false;
                    from = slash + 1;
                }
            }
            const std::string_view rest = pattern.substr(after);
            for (std::size_t from = j;; ++from) {
                if (matches(rest, text.substr(from), fold)) return true;
                if (from == text.size() || text[from] == '/') return false;
            }
        }
        if (j == text.size()) return false;
        if (c == '?') {
            if (text[j] == '/') return false;
            ++i;
            ++j;
            continue;
        }
        if (c == '[') {
            std::size_t at = i + 1;
            if (const std::optional<bool> taken = in_brackets(pattern, at, text[j], fold)) {
                if (!*taken || text[j] == '/') return false;
                i = at;
                ++j;
                continue;
            }
        }
        char literal = c;
        if (c == '\\' && i + 1 < pattern.size()) literal = pattern[++i];
        if (!same(literal, text[j], fold)) return false;
        ++i;
        ++j;
    }
    return j == text.size();
}

// `path` from the root of its file system down, with no . or .. and no
// slash at its end.
fs::path normal(const fs::path& path) {
    std::error_code error;
    fs::path out = fs::absolute(path, error).lexically_normal();
    if (!out.has_filename() && out != out.root_path()) out = out.parent_path();
    return out;
}

// The first line of the file at `path`, without its line break.
std::optional<std::string> first_line(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string line;
    if (!in || !std::getline(in, line)) return std::nullopt;
    return std::string(trimmed(line));
}

// The repository's own folder: .git itself, or the folder a .git file
// names, as a worktree's and a submodule's do.
fs::path git_folder(const fs::path& dot_git) {
    std::error_code error;
    if (fs::is_directory(dot_git, error)) return dot_git;
    const std::optional<std::string> line = first_line(dot_git);
    if (!line || !line->starts_with("gitdir:")) return {};
    const fs::path named(std::string(trimmed(std::string_view(*line).substr(7))));
    return normal(named.is_relative() ? dot_git.parent_path() / named : named);
}

// Where what a repository's worktrees share is kept: its config and its
// info/exclude. A worktree's folder names it in commondir.
fs::path common_folder(const fs::path& git) {
    const std::optional<std::string> named = first_line(git / "commondir");
    if (!named) return git;
    const fs::path common(*named);
    return normal(common.is_relative() ? git / common : common);
}

// What a git config file says about ignoring, over what came before it.
struct Settings {
    std::optional<fs::path> excludes_file;
    bool ignore_case{false};
};

// A value as written: up to a comment, without its quotes.
std::string_view value_of(std::string_view text) {
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '"') quoted = !quoted;
        if (!quoted && (text[i] == '#' || text[i] == ';')) {
            text = text.substr(0, i);
            break;
        }
    }
    text = trimmed(text);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') text = text.substr(1, text.size() - 2);
    return text;
}

// Reads core.excludesFile and core.ignoreCase from the config file `file`;
// ~ in a path is `home`.
void read_config(const fs::path& file, const fs::path& home, Settings& settings) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return;
    bool core = false;
    for (std::string read; std::getline(in, read);) {
        std::string_view line = trimmed(read);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            if (close == std::string_view::npos) continue;
            core = lowered(trimmed(line.substr(1, close - 1))) == "core";
            // A setting may follow its section's name on the same line.
            line = trimmed(line.substr(close + 1));
            if (line.empty()) continue;
        }
        if (!core) continue;
        const std::size_t equals = line.find('=');
        const std::string key = lowered(trimmed(line.substr(0, equals)));
        // A name with no value is true.
        const std::string_view value = equals == std::string_view::npos ? "true" : value_of(line.substr(equals + 1));
        if (key == "excludesfile") {
            if (value == "~") {
                settings.excludes_file = home;
            } else if (value.starts_with("~/")) {
                settings.excludes_file = home / value.substr(2);
            } else {
                settings.excludes_file = fs::path(value);
            }
        } else if (key == "ignorecase") {
            const std::string said = lowered(value);
            settings.ignore_case = said == "true" || said == "yes" || said == "on" || said == "1";
        }
    }
}

} // namespace

GitPlaces GitPlaces::of_user() {
    GitPlaces places;
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') home = std::getenv("USERPROFILE");
    if (home != nullptr && home[0] != '\0') places.home = home;
    if (const char* config = std::getenv("XDG_CONFIG_HOME"); config != nullptr && config[0] != '\0') {
        places.config_home = config;
    } else if (!places.home.empty()) {
        places.config_home = places.home / ".config";
    }
    return places;
}

GitIgnore::GitIgnore(const fs::path& folder, const GitPlaces& places) {
    const fs::path start = normal(folder);
    // The top of the working tree: the nearest folder holding .git, from
    // this one up.
    fs::path git;
    std::error_code error;
    for (fs::path at = start;; at = at.parent_path()) {
        if (fs::exists(at / ".git", error)) {
            top_ = at;
            git = git_folder(at / ".git");
            break;
        }
        if (at == at.root_path() || !at.has_parent_path()) return;
    }

    // The user's config, then the repository's, which has the last word.
    const fs::path common = git.empty() ? fs::path{} : common_folder(git);
    Settings settings;
    if (!places.config_home.empty()) read_config(places.config_home / "git" / "config", places.home, settings);
    if (!places.home.empty()) read_config(places.home / ".gitconfig", places.home, settings);
    if (!common.empty()) read_config(common / "config", places.home, settings);
    ignore_case_ = settings.ignore_case;

    if (settings.excludes_file) {
        read_rules(*settings.excludes_file, "");
    } else if (!places.config_home.empty()) {
        read_rules(places.config_home / "git" / "ignore", "");
    }
    if (!common.empty()) read_rules(common / "info" / "exclude", "");

    // The .gitignore of each folder above `folder`, from the top down.
    if (start != top_) {
        fs::path at = top_;
        for (const fs::path& part : start.lexically_relative(top_)) {
            enter(at);
            at /= part;
        }
    }
}

void GitIgnore::enter(const fs::path& folder) {
    if (!in_repository()) return;
    std::string base = normal(folder).lexically_relative(top_).generic_string();
    if (base == ".") base.clear();
    read_rules(folder / ".gitignore", std::move(base));
}

void GitIgnore::leave() {
    if (!in_repository() || rules_.empty()) return;
    rules_.pop_back();
}

void GitIgnore::read_rules(const fs::path& file, std::string base) {
    Rules& set = rules_.emplace_back(Rules{.base = std::move(base), .rules = {}});
    std::ifstream in(file, std::ios::binary);
    bool first = true;
    for (std::string read; in && std::getline(in, read); first = false) {
        std::string_view line = read;
        if (first && line.starts_with("\xEF\xBB\xBF")) line.remove_prefix(3);
        if (line.ends_with('\r')) line.remove_suffix(1);
        // Spaces at the end go, unless a backslash keeps the last of them.
        while (line.ends_with(' ') && !(line.size() >= 2 && line[line.size() - 2] == '\\')) line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        Rule rule;
        if (line.front() == '!') {
            rule.negated = true;
            line.remove_prefix(1);
        }
        if (line.ends_with('/')) {
            rule.folders_only = true;
            line.remove_suffix(1);
        }
        // A / anywhere but at the end ties the pattern to this folder.
        rule.anchored = line.find('/') != std::string_view::npos;
        if (line.starts_with('/')) line.remove_prefix(1);
        if (line.empty()) continue;
        rule.pattern = line;
        set.rules.push_back(std::move(rule));
    }
}

bool GitIgnore::ignored(const fs::path& path, bool folder) const {
    if (!in_repository()) return false;
    const std::string from_top = (path.is_absolute() ? path : normal(path)).lexically_relative(top_).generic_string();
    if (from_top.empty() || from_top == "." || from_top.starts_with("../")) return false;
    const std::string_view name = std::string_view(from_top).substr(from_top.rfind('/') + 1);
    for (auto set = rules_.rbegin(); set != rules_.rend(); ++set) {
        // The path from the folder the set's file is in.
        std::string_view relative = from_top;
        if (!set->base.empty()) {
            if (!relative.starts_with(set->base) || relative.size() <= set->base.size() ||
                relative[set->base.size()] != '/') {
                continue;
            }
            relative.remove_prefix(set->base.size() + 1);
        }
        for (auto rule = set->rules.rbegin(); rule != set->rules.rend(); ++rule) {
            if (rule->folders_only && !folder) continue;
            if (matches(rule->pattern, rule->anchored ? relative : name, ignore_case_)) return !rule->negated;
        }
    }
    return false;
}

} // namespace fjedit
