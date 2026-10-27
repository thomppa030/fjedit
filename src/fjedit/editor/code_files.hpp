#pragma once

// The files an editor has open, for every view and window of it: one
// document each, the language services that answer for it, the disk looked
// at every few seconds for changes another program made, and unsaved work
// kept against a crash. It draws nothing: what it found is asked of it, and
// whoever shows the code shows that.
//
//     fjedit::CodeFiles files(folder, preferences, {.recovery = kept_in});
//     auto document = files.open(folder / "scripts/coin.cpp");
//     ...
//     const auto looked = files.update(ui::core::current().time());

#include "fjedit/core/document.hpp"
#include "fjedit/core/language_services.hpp"
#include "fjedit/core/recovery.hpp"
#include "fjedit/core/workspace.hpp"
#include "fjedit/editor/preferences.hpp"

#include "ftk/base/result.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fjedit {

/// What files start with besides their folder.
struct CodeFilesOptions {
    /// Where unsaved work is kept against a crash, and offered back from at
    /// the start; none keeps nothing.
    std::filesystem::path recovery{};
    /// Their language services, in the order a file is asked of them. Not
    /// given, clangd answers for C and C++, and fjls for a game's script
    /// headers and fjell's own files, each at the path the preferences give.
    std::optional<std::vector<ServiceMaker>> services{};
    /// The fjls the program brings, started over the one the preferences
    /// name, whatever they say; empty for theirs.
    std::string fjls{};
    /// Wakes the frame loop from another thread, when a language service's
    /// answer comes: ftk::Window::wake() for a window.
    std::function<void()> wake{};
};

class CodeFiles {
public:
    /// The files of the folder `root`, their servers at the paths
    /// `preferences` give, which outlive them.
    CodeFiles(std::filesystem::path root, Preferences& preferences, CodeFilesOptions options = {});

    // Its language services hold `this`: the files stay where they were made.
    CodeFiles(const CodeFiles&) = delete;
    CodeFiles& operator=(const CodeFiles&) = delete;
    CodeFiles(CodeFiles&&) = delete;
    CodeFiles& operator=(CodeFiles&&) = delete;

    [[nodiscard]] Workspace& workspace() { return workspace_; }
    [[nodiscard]] const Workspace& workspace() const { return workspace_; }
    [[nodiscard]] LanguageServices& services() { return services_; }
    [[nodiscard]] const LanguageServices& services() const { return services_; }
    [[nodiscard]] const std::filesystem::path& root() const { return workspace_.root(); }
    /// The fjls the program brings; empty when the preferences name it.
    [[nodiscard]] const std::string& own_fjls() const { return own_fjls_; }

    /// The document of the file at `path`, read the first time it is asked
    /// for, with the files' marks (`A`-`Z`) it had when it was let go. What
    /// went wrong, also noted, when it can't be read.
    [[nodiscard]] ftk::Result<Document*> open(const std::filesystem::path& path);

    /// Writes `path`'s document to its file, telling its services, and
    /// forgets the work kept for it. What went wrong, also noted, when it
    /// can't be written.
    ftk::Result<> save(const std::filesystem::path& path);

    /// Lets the file at `path` go, with whatever wasn't saved: its files'
    /// marks are kept until it opens again, and its services told.
    void let_go(const std::filesystem::path& path);

    /// Whether `path`'s document has changes its file doesn't.
    [[nodiscard]] bool unsaved(const std::filesystem::path& path) const { return workspace_.unsaved(path); }
    /// The first file opened that has unsaved changes, when one has.
    [[nodiscard]] std::optional<std::filesystem::path> first_unsaved() const;
    /// Every file open that has unsaved changes, in the order they were
    /// opened.
    [[nodiscard]] std::vector<std::filesystem::path> unsaved_files() const;

    /// What happened as the files were brought up to date.
    struct Update {
        /// The disk was looked at: what lists folders reads them again.
        bool looked{false};
        /// A server was started again at a new path: what was asked of the
        /// one before will never be answered.
        bool restarted{false};
    };

    /// Brings the files up to date at `now`, in seconds: a server whose path
    /// the preferences changed started again, what the services found since
    /// taken, and every few seconds the disk looked at. A file another
    /// program changed comes in as it is when it has no unsaved changes
    /// here, and is held to be asked about when it has; unsaved work is
    /// kept.
    Update update(double now);

    /// When the disk is next looked at, in seconds.
    [[nodiscard]] double next_check() const { return next_check_; }

    /// Whether the file at `path` changed on disk while it had unsaved
    /// changes here, and nobody has said yet which to keep.
    [[nodiscard]] bool changed_on_disk(const std::filesystem::path& path) const { return changed_.contains(path); }
    /// Keeps the changes here over the file changed on disk.
    void keep_mine(const std::filesystem::path& path) { changed_.erase(path); }
    /// Makes `path`'s document its file as it is on disk now, as one edit
    /// that can be undone. What went wrong, also noted, when it can't be
    /// read.
    ftk::Result<> reload(const std::filesystem::path& path);

    /// Whether unsaved work is kept against a crash.
    [[nodiscard]] bool keeps_work() const { return recovery_.has_value(); }
    /// The work kept from before, when the editor closed with unsaved
    /// changes: none when none is kept.
    [[nodiscard]] std::vector<Recovery::Kept> kept_from_before() const;
    /// Forgets the work kept for `path` this time round: saved, let go, or
    /// dropped on purpose.
    void forget_kept(const std::filesystem::path& path);
    /// Forgets the work kept from before for `path`, not taken back.
    void discard_kept(const std::filesystem::path& path);
    /// Makes the open file at `path` hold `bytes`, unsaved: work kept from
    /// before, taken back. What went wrong, also noted, when it can't.
    ftk::Result<> put_back(const std::filesystem::path& path, std::string_view bytes);

    /// A file's mark set in the file at `in`: taken from every other file,
    /// open or let go, as a file's mark is in one file at a time.
    void keep_file_mark(char name, const std::filesystem::path& in);
    /// The file open that has the file's mark `name`, or the one let go with
    /// it; none when no file has it.
    [[nodiscard]] std::optional<std::filesystem::path> file_with_mark(char name) const;

    /// Makes `edits`: in the files open, as one step each while they are as
    /// the edits were made for; in the others, on disk. What can't be made
    /// is noted.
    void make_edits(const WorkspaceEdit& edits);

    /// What went wrong last, for a status bar: empty once something works.
    [[nodiscard]] const std::string& note() const { return note_; }
    void set_note(std::string what) { note_ = std::move(what); }
    void clear_note() { note_.clear(); }

private:
    // clangd for C and C++, and fjls for a game's script headers and
    // fjell's own files, at the paths the preferences give them, fjls at
    // the program's own when it brings one.
    [[nodiscard]] ServiceMaker clangd();
    [[nodiscard]] ServiceMaker fjls();
    [[nodiscard]] const std::string& fjls_path() const { return own_fjls_.empty() ? preferences_.fjls : own_fjls_; }
    // The makers, those of clangd and fjls noting the path each starts at.
    [[nodiscard]] std::vector<ServiceMaker> noting_paths(std::vector<ServiceMaker> makers);
    // Starts a server again when the path the preferences give it changed.
    bool follow_server_paths();
    // Keeps what each unsaved file holds when it changed since it was last
    // kept, and forgets what was kept for those now saved.
    void keep_unsaved();

    Workspace workspace_;
    Preferences& preferences_;
    // What wakes the frame loop when a language service answers; the fjls
    // the program brings; the paths clangd and fjls were last started at;
    // and the services, each running once a file it answers for opens.
    std::function<void()> wake_;
    std::string own_fjls_;
    std::string clangd_started_;
    std::string fjls_started_;
    LanguageServices services_;
    // Files changed on disk while they had unsaved changes here, until
    // answered; and when the disk is next looked at.
    std::set<std::filesystem::path> changed_;
    double next_check_{0.0};
    // Unsaved work kept against a crash, and each file's version as kept.
    std::optional<Recovery> recovery_;
    std::map<std::filesystem::path, uint64_t> kept_;
    // The files' marks (`A`-`Z`) of files let go, each with its file, until
    // the file opens again.
    struct FileMark {
        std::filesystem::path path;
        Position at;
    };
    std::map<char, FileMark> closed_marks_;
    std::string note_;
};

} // namespace fjedit
