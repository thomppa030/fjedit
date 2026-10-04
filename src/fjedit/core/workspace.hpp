#pragma once

// The files a program has open: one document each, however many views show
// it, read from disk, written back as it came, and checked against the disk
// for changes another program made.
//
//     fjedit::Workspace workspace(root);
//     auto document = workspace.open(root / "scripts/coin.cpp");
//     if (!document) return report(document.error());
//     ...
//     if (workspace.unsaved(path)) (void)workspace.save(path);

#include "fjedit/core/document.hpp"

#include "ftk/base/result.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace fjedit {

class Workspace {
public:
    /// The files of the folder `root`, the one the program has open.
    explicit Workspace(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

    /// `path` as the workspace names files: whole, and the same however it
    /// was reached.
    [[nodiscard]] std::filesystem::path name(const std::filesystem::path& path) const;

    /// The document of the file at `path`, read from disk the first time it
    /// is asked for. An error when it can't be read (read_text_file()).
    [[nodiscard]] ftk::Result<Document*> open(const std::filesystem::path& path);

    /// The document open for `path`, or null.
    [[nodiscard]] Document* find(const std::filesystem::path& path) const;

    /// The files open, in the order they were opened.
    [[nodiscard]] std::vector<std::filesystem::path> files() const;

    /// Whether `path`'s document has changes its file doesn't.
    [[nodiscard]] bool unsaved(const std::filesystem::path& path) const;

    /// Writes `path`'s document to its file, which holds it from then on.
    [[nodiscard]] ftk::Result<> save(const std::filesystem::path& path);

    /// Lets `path`'s document go, with whatever wasn't saved.
    void close(const std::filesystem::path& path);

    /// The open files another program changed since they were last read or
    /// written here, each named once for each change.
    [[nodiscard]] std::vector<std::filesystem::path> changed_on_disk();

    /// Makes `path`'s document its file as it is on disk now, as one edit
    /// that can be undone, and saved from then on. Lines that are the same
    /// are left alone, so the carets on them stay.
    [[nodiscard]] ftk::Result<> reload(const std::filesystem::path& path);

private:
    // When a file was last written, and how long it was: what tells that
    // another program wrote it.
    struct Stamp {
        std::filesystem::file_time_type written{};
        std::uintmax_t size{0};

        bool operator==(const Stamp&) const = default;
    };

    struct OpenFile {
        std::filesystem::path path;
        std::unique_ptr<Document> document;
        // The file as it was last read or written here, and the change last
        // named by changed_on_disk().
        std::optional<Stamp> known;
        std::optional<Stamp> named;
    };

    [[nodiscard]] static std::optional<Stamp> stamp_of(const std::filesystem::path& path);
    [[nodiscard]] OpenFile* find_open(const std::filesystem::path& path);
    [[nodiscard]] const OpenFile* find_open(const std::filesystem::path& path) const;

    std::filesystem::path root_;
    std::vector<OpenFile> open_;
};

} // namespace fjedit
