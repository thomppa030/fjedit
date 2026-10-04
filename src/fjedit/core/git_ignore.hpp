#pragma once

// What git ignores among a folder's files, read as git reads it: the
// .gitignore in each folder, a deeper one over those above it; the
// repository's info/exclude; and the user's own excludes file. Patterns
// follow gitignore(5): ! takes a file back, a trailing / names folders only,
// a / before the end ties a pattern to its file's folder, ** spans folders,
// and the repository's core.ignoreCase holds. A folder in no repository
// ignores nothing, as git would.
//
// A walk enters each folder before asking about what is in it, and never
// goes into a folder git ignores: nothing in one can be taken back.
//
//     GitIgnore ignore(root);
//     ignore.enter(root);
//     if (!ignore.ignored(root / "build", true)) ...
//     ignore.leave();

#include <filesystem>
#include <string>
#include <vector>

namespace fjedit {

/// Where the user's git config is: their home, and the folder of their
/// settings (XDG_CONFIG_HOME), which holds git/config and git/ignore.
struct GitPlaces {
    std::filesystem::path home;
    std::filesystem::path config_home;

    /// This user's, from the environment: HOME (USERPROFILE on Windows),
    /// and XDG_CONFIG_HOME or the .config folder in the home.
    [[nodiscard]] static GitPlaces of_user();
};

class GitIgnore {
public:
    /// The rules for a walk of `folder`: its repository's, the user's
    /// excludes file found through `places`, and the .gitignore of each
    /// folder above it in the repository. The walk enters `folder` itself.
    explicit GitIgnore(const std::filesystem::path& folder, const GitPlaces& places = GitPlaces::of_user());

    /// Whether `folder` is in a repository, and so has rules at all.
    [[nodiscard]] bool in_repository() const { return !top_.empty(); }

    /// Takes in the .gitignore of `folder`, which the walk goes into: its
    /// rules hold for what is under it until leave().
    void enter(const std::filesystem::path& folder);
    /// Lets go of the rules of the folder entered last.
    void leave();

    /// Whether git ignores `path`, a file or a folder (`folder`) in a
    /// folder entered: the last pattern that matches it decides, a deeper
    /// .gitignore's after those above it.
    [[nodiscard]] bool ignored(const std::filesystem::path& path, bool folder) const;

private:
    // One pattern, as a line of an ignore file sets it out.
    struct Rule {
        std::string pattern;
        bool negated{false};
        bool folders_only{false};
        // Tied to the folder of its file, matched against the path from
        // there; otherwise against the name alone, at any depth.
        bool anchored{false};
    };

    // The rules of one file, and the folder their paths begin at, as a
    // path from the top of the repository ("" at the top).
    struct Rules {
        std::string base;
        std::vector<Rule> rules;
    };

    // Takes in the rules of the ignore file `file`, whose paths begin at
    // `base`; a file that isn't there sets none.
    void read_rules(const std::filesystem::path& file, std::string base);

    // The top of the working tree; empty out of a repository.
    std::filesystem::path top_;
    bool ignore_case_{false};
    // In order of weight, the least first: the user's file, info/exclude,
    // then each folder's .gitignore from the top down. A folder entered
    // with no .gitignore leaves an empty set, so leave() stays in step.
    std::vector<Rules> rules_;
};

} // namespace fjedit
