#pragma once

// Find in files (sheet 39, W3): a window floating at the top of the code,
// where you are already looking, with the search and its toggles, where to
// look and which files, the matches as one flat list, and the code around
// the match picked, editable, under them. Ctrl Shift F, or the search on
// the files, opens it; Enter opens the file at the match, Up and Down go
// through the matches, Escape closes it. The search runs on a thread of its
// own, again whenever what it looks for changes.

#include "fjedit/core/file_search.hpp"
#include "fjedit/core/keymap.hpp"
#include "fjedit/core/workspace.hpp"
#include "fjedit/widget/code_view.hpp"

#include "ftk/ui/core/rect.hpp"
#include "ftk/ui/kit/find.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fjedit {

/// What the window asks of the program in a frame.
struct FindInFilesAsk {
    /// Open this file at this match, with the keyboard: the window closed.
    std::optional<FileHit> go;
    /// The files the window opened only to show them, now it has closed:
    /// to be let go, or shown in tabs when the preview changed them.
    std::vector<std::filesystem::path> shown;
};

class FindInFiles {
public:
    /// Where it looks.
    enum class Scope : int { Project, Folder, OpenFiles };

    explicit FindInFiles(Workspace& workspace) : workspace_(&workspace) {}

    // The search's thread holds what it is given; the window stays put.
    FindInFiles(const FindInFiles&) = delete;
    FindInFiles& operator=(const FindInFiles&) = delete;
    FindInFiles(FindInFiles&&) = delete;
    FindInFiles& operator=(FindInFiles&&) = delete;
    ~FindInFiles() = default;

    /// Opens it with the keyboard in the search, looking for `seed` when it
    /// isn't empty.
    void open(std::string_view seed);
    [[nodiscard]] bool is_open() const { return open_; }

    /// Draws it over the top of `over`, the code, and takes what is done
    /// there. `folder` is where Folder looks: the folder of the file in
    /// front.
    [[nodiscard]] FindInFilesAsk draw(ftk::ui::core::Rect over, const std::filesystem::path& folder, const Keymap& keys);

    /// The matches found for what it looks for now; empty while it searches.
    [[nodiscard]] const std::vector<FileHit>& hits() const { return hits_; }

private:
    // What a search looks for; a change starts it again.
    struct Asked {
        std::string query;
        ftk::ui::FindToggles toggles;
        std::string include;
        Scope scope{Scope::Project};
        std::filesystem::path folder;
        bool operator==(const Asked&) const = default;
    };

    // What the search's thread hands back.
    struct Found {
        std::mutex lock;
        std::vector<FileHit> hits;
        bool done{false};
    };

    void search(const Asked& asked);
    void take_found();
    void replace_all();
    void close(FindInFilesAsk& ask);
    // Shows `hit`'s file in the preview, at the match.
    void preview(const FileHit& hit);

    Workspace* workspace_;
    bool open_{false};
    bool focus_{false};
    std::string query_;
    std::string replacement_;
    std::string include_;
    ftk::ui::FindToggles toggles_;
    Scope scope_{Scope::Project};

    std::optional<Asked> searched_;
    std::shared_ptr<Found> found_;
    std::jthread worker_;
    std::vector<FileHit> hits_;
    bool searching_{false};
    std::size_t selected_{0};

    // The file in the preview and its view; the files opened only to be
    // shown there.
    std::filesystem::path previewed_;
    std::unique_ptr<EditorView> preview_;
    std::set<std::filesystem::path> shown_;
};

} // namespace fjedit
