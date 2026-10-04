#pragma once

// Unsaved work kept against a crash: what each file with unsaved changes
// holds, written into a folder every few seconds while it is unsaved, and
// offered back at the next start until it is saved or let go.
//
//     fjedit::Recovery recovery(folder);
//     (void)recovery.keep(path, document.buffer().text());  // while unsaved
//     recovery.forget(path);                                 // saved, or let go
//     for (const fjedit::Recovery::Kept& kept : recovery.kept()) offer(kept);

#include "ftk/base/result.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit {

class Recovery {
public:
    /// Work kept in `folder`, made when something is first kept.
    explicit Recovery(std::filesystem::path folder) : folder_(std::move(folder)) {}

    [[nodiscard]] const std::filesystem::path& folder() const { return folder_; }

    /// A folder of its own under `base` for the work on the files of the
    /// folder `root`, the same on every run.
    [[nodiscard]] static std::filesystem::path folder_for(const std::filesystem::path& base,
                                                          const std::filesystem::path& root);

    /// Keeps `bytes` as the unsaved work on the file at `path`, in place of
    /// what was kept for it before.
    [[nodiscard]] ftk::Result<> keep(const std::filesystem::path& path, std::string_view bytes);

    /// Lets what was kept for `path` go.
    void forget(const std::filesystem::path& path);

    struct Kept {
        /// The file the work is on.
        std::filesystem::path path;
        /// What the file held, unsaved.
        std::string bytes;
        /// When it was kept.
        std::filesystem::file_time_type when;
    };

    /// Everything kept, the oldest first.
    [[nodiscard]] std::vector<Kept> kept() const;

private:
    [[nodiscard]] std::filesystem::path kept_path(const std::filesystem::path& path) const;

    std::filesystem::path folder_;
};

} // namespace fjedit
