#include "fjedit/editor/code_files.hpp"

#include "fjedit/core/commands.hpp"
#include "fjedit/core/language.hpp"
#include "fjedit/core/layout.hpp"
#include "fjedit/core/text_file.hpp"
#include "fjedit/lsp/client.hpp"

#include <format>
#include <memory>
#include <string_view>
#include <utility>

namespace fjedit {

namespace {

// How often the disk is looked at for files changed elsewhere.
constexpr double DISK_CHECK = 2.0;

} // namespace

CodeFiles::CodeFiles(std::filesystem::path root, Preferences& preferences, CodeFilesOptions options)
    : workspace_(std::move(root)), preferences_(preferences), wake_(std::move(options.wake)),
      own_fjls_(std::move(options.fjls)),
      services_(noting_paths(options.services ? std::move(*options.services)
                                              : std::vector<ServiceMaker>{clangd(), fjls()}),
                workspace_.root()) {
    if (!options.recovery.empty()) recovery_.emplace(std::move(options.recovery));
}

ServiceMaker CodeFiles::clangd() {
    static const std::vector<std::string> EXTENSIONS{"c", "cc", "cpp", "cxx", "h", "hh", "hpp", "hxx", "inl", "ipp"};
    return {.name = "clangd",
            .extensions = EXTENSIONS,
            .make = [this](const std::filesystem::path& root) -> std::unique_ptr<LanguageService> {
                return std::make_unique<lsp::Client>(lsp::ClientOptions{
                    // No mark before a completion's name for whether it
                    // brings an #include: the name is the name.
                    .command = {preferences_.clangd, "--log=error", "--header-insertion-decorators=0"},
                    .root = root,
                    .extensions = EXTENSIONS,
                    .language_id = [](const std::filesystem::path& path) {
                        return path.extension() == ".c" ? std::string("c") : std::string("cpp");
                    },
                    .arrived = wake_,
                    .if_missing = "set its path in Preferences",
                });
            }};
}

ServiceMaker CodeFiles::fjls() {
    // A game's script headers, beside clangd; fjell's own files and GLSL
    // alone.
    static const Language& GLSL = language_for("shader.glsl");
    static const std::vector<std::string> EXTENSIONS = [] {
        std::vector<std::string> extensions{"hpp", "fjss", "fjwidget", "fjfont", "fjsl"};
        for (const std::string_view extension : extensions_of(GLSL)) extensions.emplace_back(extension);
        return extensions;
    }();
    return {.name = "fjls",
            .extensions = EXTENSIONS,
            .make = [this](const std::filesystem::path& root) -> std::unique_ptr<LanguageService> {
                return std::make_unique<lsp::Client>(lsp::ClientOptions{
                    .command = {fjls_path()},
                    .root = root,
                    .extensions = EXTENSIONS,
                    .language_id = [](const std::filesystem::path& path) {
                        const std::string extension = path.extension().string();
                        if (extension == ".hpp") return std::string("cpp");
                        // A stage's file and an include alike.
                        if (&language_for(path.filename().string()) == &GLSL) return std::string("glsl");
                        return extension.substr(1);
                    },
                    .arrived = wake_,
                    .if_missing = own_fjls_.empty() ? "set its path in Preferences" : "it comes with the program",
                });
            }};
}

std::vector<ServiceMaker> CodeFiles::noting_paths(std::vector<ServiceMaker> makers) {
    for (ServiceMaker& maker : makers) {
        const bool clangd = maker.name == "clangd";
        if ((!clangd && maker.name != "fjls") || !maker.make) continue;
        std::string* started = clangd ? &clangd_started_ : &fjls_started_;
        const std::string* wanted = clangd ? &preferences_.clangd : &fjls_path();
        maker.make = [started, wanted, make = std::move(maker.make)](const std::filesystem::path& root) {
            *started = *wanted;
            return make(root);
        };
    }
    return makers;
}

bool CodeFiles::follow_server_paths() {
    bool restarted = false;
    const auto follow = [&](const char* name, const std::string& started, const std::string& wanted) {
        if (!services_.status_of(name) || started == wanted) return;
        services_.restart(name);
        restarted = true;
    };
    follow("clangd", clangd_started_, preferences_.clangd);
    follow("fjls", fjls_started_, fjls_path());
    return restarted;
}

ftk::Result<Document*> CodeFiles::open(const std::filesystem::path& path) {
    const ftk::Result<Document*> document = workspace_.open(path);
    if (!document) {
        note_ = document.error();
        return document;
    }
    note_.clear();
    // The files' marks it had when it was let go are its again.
    const std::filesystem::path named = workspace_.name(path);
    for (auto kept = closed_marks_.begin(); kept != closed_marks_.end();) {
        if (kept->second.path != named) {
            ++kept;
            continue;
        }
        (*document)->set_mark(kept->first, kept->second.at);
        kept = closed_marks_.erase(kept);
    }
    return document;
}

ftk::Result<> CodeFiles::save(const std::filesystem::path& path) {
    if (ftk::Result<> saved = workspace_.save(path); !saved) {
        note_ = saved.error();
        return saved;
    }
    const std::filesystem::path named = workspace_.name(path);
    changed_.erase(named);
    forget_kept(named);
    services_.saved(named);
    note_.clear();
    return {};
}

void CodeFiles::let_go(const std::filesystem::path& path) {
    // Its files' marks wait for it to open again.
    if (const Document* document = workspace_.find(path)) {
        for (const auto& [name, at] : document->marks()) {
            if (name >= 'A' && name <= 'Z') closed_marks_[name] = {.path = workspace_.name(path), .at = at};
        }
    }
    services_.close(path);
    workspace_.close(path);
}

std::optional<std::filesystem::path> CodeFiles::first_unsaved() const {
    for (const std::filesystem::path& path : workspace_.files()) {
        if (workspace_.unsaved(path)) return path;
    }
    return std::nullopt;
}

std::vector<std::filesystem::path> CodeFiles::unsaved_files() const {
    std::vector<std::filesystem::path> unsaved;
    for (const std::filesystem::path& path : workspace_.files()) {
        if (workspace_.unsaved(path)) unsaved.push_back(path);
    }
    return unsaved;
}

CodeFiles::Update CodeFiles::update(double now) {
    Update update{.looked = false, .restarted = follow_server_paths()};
    // What the language services found since, before anything shows it.
    services_.update();
    if (now < next_check_) return update;
    next_check_ = now + DISK_CHECK;
    for (const std::filesystem::path& path : workspace_.changed_on_disk()) {
        if (workspace_.unsaved(path)) {
            changed_.insert(path);
        } else if (const ftk::Result<> reloaded = workspace_.reload(path); !reloaded) {
            note_ = reloaded.error();
        }
    }
    keep_unsaved();
    update.looked = true;
    return update;
}

ftk::Result<> CodeFiles::reload(const std::filesystem::path& path) {
    if (ftk::Result<> reloaded = workspace_.reload(path); !reloaded) {
        note_ = reloaded.error();
        return reloaded;
    }
    changed_.erase(workspace_.name(path));
    return {};
}

std::vector<Recovery::Kept> CodeFiles::kept_from_before() const {
    if (!recovery_) return {};
    return recovery_->kept();
}

void CodeFiles::keep_unsaved() {
    if (!recovery_) return;
    for (const std::filesystem::path& path : workspace_.files()) {
        if (!workspace_.unsaved(path)) {
            forget_kept(path);
            continue;
        }
        const Document& document = *workspace_.find(path);
        const auto kept = kept_.find(path);
        if (kept != kept_.end() && kept->second == document.version()) continue;
        if (const ftk::Result<> written = recovery_->keep(path, document.buffer().text()); !written) {
            note_ = written.error();
            continue;
        }
        kept_[path] = document.version();
    }
}

void CodeFiles::forget_kept(const std::filesystem::path& path) {
    if (kept_.erase(path) > 0 && recovery_) recovery_->forget(path);
}

void CodeFiles::discard_kept(const std::filesystem::path& path) {
    if (recovery_) recovery_->forget(path);
}

ftk::Result<> CodeFiles::put_back(const std::filesystem::path& path, std::string_view bytes) {
    ftk::Result<> put = workspace_.set_text(path, bytes);
    if (!put) note_ = put.error();
    return put;
}

void CodeFiles::keep_file_mark(char name, const std::filesystem::path& in) {
    const Document* own = workspace_.find(in);
    closed_marks_.erase(name);
    for (const std::filesystem::path& path : workspace_.files()) {
        Document* document = workspace_.find(path);
        if (document != nullptr && document != own) document->forget_mark(name);
    }
}

std::optional<std::filesystem::path> CodeFiles::file_with_mark(char name) const {
    for (const std::filesystem::path& path : workspace_.files()) {
        const Document* document = workspace_.find(path);
        if (document != nullptr && document->mark(name)) return path;
    }
    if (const auto kept = closed_marks_.find(name); kept != closed_marks_.end()) return kept->second.path;
    return std::nullopt;
}

void CodeFiles::make_edits(const WorkspaceEdit& edits) {
    for (const auto& [path, file] : edits) {
        if (Document* document = workspace_.find(path)) {
            if (file.version && *file.version != document->version()) {
                note_ = std::format("{} changed while it was being renamed in", path.filename().string());
                continue;
            }
            // Its views catch up through the edit, as with any made elsewhere.
            Selections selections;
            const MonospaceLayout layout;
            CommandContext context{.document = *document, .selections = selections, .layout = layout};
            (void)apply_edit(context, file.edit);
            continue;
        }
        const ftk::Result<std::string> bytes = read_text_file(path);
        if (!bytes) {
            note_ = bytes.error();
            continue;
        }
        Document document(*bytes, language_for(path.filename().string()));
        Selections selections;
        const MonospaceLayout layout;
        CommandContext context{.document = document, .selections = selections, .layout = layout};
        (void)apply_edit(context, file.edit);
        if (const ftk::Result<> written = write_text_file(path, document.buffer().text()); !written) note_ = written.error();
    }
}

} // namespace fjedit
