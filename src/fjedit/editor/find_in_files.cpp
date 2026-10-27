#include "fjedit/editor/find_in_files.hpp"

#include "fjedit/core/commands.hpp"
#include "fjedit/core/language.hpp"
#include "fjedit/core/text_file.hpp"
#include "fjedit/widget/file_kind.hpp"
#include "fjedit/widget/find.hpp"

#include "ftk/ui/core/context.hpp"
#include "ftk/ui/kit/button.hpp"
#include "ftk/ui/kit/choice.hpp"
#include "ftk/ui/kit/icons.hpp"
#include "ftk/ui/kit/layout.hpp"
#include "ftk/ui/kit/plate.hpp"
#include "ftk/ui/kit/region.hpp"
#include "ftk/ui/kit/text.hpp"
#include "ftk/ui/kit/text_field.hpp"
#include "ftk/ui/kit/tree.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <iterator>
#include <map>
#include <mutex>
#include <utility>

namespace fjedit {

namespace {

namespace ui = ftk::ui;
namespace theme = ftk::theme;
using Scope = FindInFiles::Scope;

// Sheet 39's measures: the window, the scope's choice and the filter, how
// many matches the list shows at once, and how many lines the preview.
constexpr float WINDOW_WIDTH = 700.0f;
constexpr float SCOPE_WIDTH = 260.0f;
constexpr float INCLUDE_WIDTH = 200.0f;
constexpr float RESULT_ROWS = 8.0f;
constexpr float PREVIEW_LINES = 9.0f;
// How often a search under way is looked at.
constexpr double POLL = 0.05;
// The most matches a search lists.
constexpr std::size_t LIMIT = 10'000;

constexpr ui::Choice<Scope> SCOPES[] = {
    {Scope::Project, "Project"},
    {Scope::Folder, "Folder"},
    {Scope::OpenFiles, "Open files"},
};

std::string_view trimmed_front(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t");
    return first == std::string_view::npos ? std::string_view{} : text.substr(first);
}

} // namespace

void FindInFiles::open(std::string_view seed) {
    open_ = true;
    focus_ = true;
    if (!seed.empty()) query_ = seed;
}

// What a search's threads hand on, kept until the window takes it.
struct FindInFiles::Found {
    std::mutex mutex;
    std::vector<FileHit> hits;
};

void FindInFiles::search(const Asked& asked) {
    searched_ = asked;
    // The search before goes on until it sees it is stopped, unwaited for.
    // Its matches stay listed until this one's first come.
    stop_.request_stop();
    stop_ = {};
    found_.reset();
    ended_ = {};
    searching_ = false;
    if (asked.query.empty()) {
        clear_hits();
        return;
    }
    stale_ = true;

    // Open files as they stand now, unsaved changes and all.
    std::map<std::filesystem::path, std::string> open_texts;
    const std::vector<std::filesystem::path> open_files = workspace_->files();
    for (const std::filesystem::path& path : open_files) open_texts[path] = workspace_->find(path)->buffer().text();

    searching_ = true;
    found_ = std::make_shared<Found>();
    const FindOptions options = find_options(asked.toggles);
    const std::filesystem::path root = workspace_->root();
    ended_ = pool_->submit([pool = pool_, found = found_, asked, options, root, open_texts = std::move(open_texts),
                            open_files, stop = stop_.get_token()] {
        std::vector<std::filesystem::path> files;
        if (asked.scope == Scope::OpenFiles) {
            for (const std::filesystem::path& path : open_files) {
                if (included(path.lexically_relative(root), asked.include)) files.push_back(path);
            }
        } else {
            files = files_to_search(asked.scope == Scope::Folder ? asked.folder : root, asked.include, stop);
        }
        const FoundHits hand_on = [&found](std::vector<FileHit>&& hits) {
            const std::lock_guard lock(found->mutex);
            found->hits.insert(found->hits.end(), std::make_move_iterator(hits.begin()),
                               std::make_move_iterator(hits.end()));
        };
        search_files(*pool, files, asked.query, options, hand_on, open_texts, stop, LIMIT);
    });
}

void FindInFiles::take_found() {
    if (!searching_) return;
    // Whether it has ended first: what it found before ending is then all
    // there to take.
    const bool ended = ended_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    std::vector<FileHit> taken;
    {
        const std::lock_guard lock(found_->mutex);
        taken.swap(found_->hits);
    }
    // A search's first matches, or its end with none, take the place of
    // the search before's.
    if (stale_ && (!taken.empty() || ended)) clear_hits();
    const bool first = hits_.empty();
    // The matches come file by file.
    for (FileHit& hit : taken) {
        if (hits_.empty() || hits_.back().path != hit.path) ++files_found_;
        hits_.push_back(std::move(hit));
    }
    if (first && !hits_.empty()) preview(hits_.front());
    if (ended) {
        ended_.get();
        searching_ = false;
    }
}

void FindInFiles::clear_hits() {
    hits_.clear();
    files_found_ = 0;
    selected_ = 0;
    stale_ = false;
    preview_.reset();
    previewed_.clear();
}

void FindInFiles::preview(const FileHit& hit) {
    if (previewed_ != hit.path || !preview_) {
        const bool was_open = workspace_->find(hit.path) != nullptr;
        const ftk::Result<Document*> document = workspace_->open(hit.path);
        if (!document) return;
        if (!was_open) shown_.insert(workspace_->name(hit.path));
        preview_ = std::make_unique<EditorView>(**document);
        preview_->minimap = false;
        previewed_ = hit.path;
    }
    const Buffer& buffer = workspace_->find(hit.path)->buffer();
    preview_->selections = Selections({.anchor = buffer.clamp(hit.span.from), .caret = buffer.clamp(hit.span.to)});
    preview_->reveal = true;
}

void FindInFiles::replace_all() {
    // The list is whole, and of what it looks for now, only once the
    // search has ended.
    if (!searched_ || searching_) return;
    const FindOptions options = find_options(searched_->toggles);
    std::vector<std::filesystem::path> files;
    for (const FileHit& hit : hits_) {
        if (files.empty() || files.back() != hit.path) files.push_back(hit.path);
    }
    // A file open is changed in place, unsaved; any other is written. Each
    // is searched again as it is now, so nothing changed since is missed.
    for (const std::filesystem::path& path : files) {
        Selections selections;
        const MonospaceLayout layout;
        if (Document* document = workspace_->find(path)) {
            CommandContext context{.document = *document, .selections = selections, .layout = layout};
            const std::vector<Span> found = find_all(document->buffer(), searched_->query, options);
            (void)replace_matches(context, found, searched_->query, replacement_, options);
            continue;
        }
        const ftk::Result<std::string> bytes = read_text_file(path);
        if (!bytes) continue;
        Document document(*bytes, language_for(path.filename().string()));
        CommandContext context{.document = document, .selections = selections, .layout = layout};
        const std::vector<Span> found = find_all(document.buffer(), searched_->query, options);
        (void)replace_matches(context, found, searched_->query, replacement_, options);
        (void)write_text_file(path, document.buffer().text());
    }
    // What it looks for may still be there: look again.
    searched_.reset();
}

void FindInFiles::close(FindInFilesAsk& ask) {
    open_ = false;
    stop_.request_stop();
    found_.reset();
    ended_ = {};
    searching_ = false;
    searched_.reset();
    clear_hits();
    ask.shown.assign(shown_.begin(), shown_.end());
    shown_.clear();
}

FindInFilesAsk FindInFiles::draw(ftk::ui::core::Rect over, const std::filesystem::path& folder, const Keymap& keys,
                                 float text_size, bool vim, bool over_windows) {
    FindInFilesAsk ask;
    if (!open_) return ask;
    ui::core::Context& context = ui::core::current();
    take_found();
    const Asked asked{.query = query_,
                      .toggles = toggles_,
                      .include = include_,
                      .scope = scope_,
                      .folder = scope_ == Scope::Folder ? folder : std::filesystem::path{}};
    if (!searched_ || *searched_ != asked) search(asked);
    if (searching_) context.request_frame_at(context.time() + POLL);

    bool go = false;
    bool closing = false;
    bool preview_focused = false;
    const float width = std::min(WINDOW_WIDTH, over.width() - 2.0f * ui::PLATE_INSET);
    if (auto plate = ui::Plate("##find_in_files", over.min, over.max, ui::PlatePlace::TopCenter, width, over_windows)) {
        const float inner = plate.inner_width();
        const float gap = ui::item_spacing().x;

        // Its name and what it found, then where it looks.
        const glm::vec2 top = ui::cursor();
        {
            const ui::FontScope bold(ui::Font::Bold, 0.0f);
            ui::align_text_to_frame();
            ui::text("Find in files");
        }
        ui::same_line();
        ui::align_text_to_frame();
        const std::string count =
            query_.empty()   ? std::string()
            : searching_     ? std::string("Searching\xE2\x80\xA6")
            : hits_.empty()  ? std::string("No results")
                             : std::format("{}{} results in {} files", hits_.size(), hits_.size() >= LIMIT ? "+" : "",
                                           files_found_);
        ui::text(count, theme::text_secondary());
        ui::same_line();
        ui::set_cursor({top.x + inner - SCOPE_WIDTH, top.y});
        ui::next_width(SCOPE_WIDTH);
        (void)ui::segmented("##scope", scope_, SCOPES);

        // The search, then the replacement and which files.
        ui::next_width(inner);
        const ui::FindField field =
            ui::find_field("##query", query_, toggles_, {.hint = "Find in files", .focus = focus_});
        focus_ = false;
        go = field.next;
        closing = field.closed;
        const float button = ui::frame_height();
        ui::next_width(inner - button - INCLUDE_WIDTH - 2.0f * gap);
        closing |= ui::replace_field("##replacement", replacement_).closed;
        ui::same_line();
        if (ui::icon_button("##replace_all", ui::icon::replace, "Replace in every file")) replace_all();
        ui::same_line();
        ui::next_width(INCLUDE_WIDTH);
        (void)ui::text_entry("##include", include_, {.hint = "Files to include"});

        // The matches, one row each, those in view drawn; the one picked by
        // the keys brought into view.
        if (auto list = ui::Region("##hits", {inner, RESULT_ROWS * theme::TREE_ROW})) {
            if (auto tree = ui::Tree("##hits")) {
                const ui::RowsInView rows(hits_.size(),
                                          std::exchange(reveal_, false) ? std::optional(selected_) : std::nullopt);
                for (std::size_t i = rows.first(); i < rows.end(); ++i) {
                    const FileHit& hit = hits_[i];
                    const std::string id = std::to_string(i);
                    const std::string_view line = trimmed_front(hit.line);
                    const std::string_view matched =
                        std::string_view(hit.line).substr(hit.span.from.byte, hit.span.to.byte - hit.span.from.byte);
                    const std::string where = std::format("{}:{}", hit.path.filename().string(), hit.span.from.line + 1);
                    const ui::AssetKind kind = file_kind(hit.path);
                    const ui::TreeRowResult row = ui::tree_row({
                        .id = id.c_str(),
                        .name = line,
                        .icon = kind.icon,
                        .hue = kind.hue,
                        .tag = where,
                        .selection = i == selected_ ? ui::RowSelection::Primary : ui::RowSelection::None,
                        .query = matched,
                    });
                    if (row.clicked && i != selected_) {
                        selected_ = i;
                        preview(hit);
                    }
                    if (row.double_clicked) go = true;
                }
            }
        }

        // The match picked, in its file, editable.
        if (selected_ < hits_.size() && preview_) {
            const FileHit& hit = hits_[selected_];
            const ui::AssetKind kind = file_kind(hit.path);
            ui::align_text_to_frame();
            ui::text(kind.icon, ui::kind_colour(kind));
            ui::same_line();
            ui::align_text_to_frame();
            ui::text(hit.path.lexically_relative(workspace_->root()).generic_string(), theme::text_secondary());
            ui::same_line();
            ui::align_text_to_frame();
            {
                const ui::FontScope mono(theme::mono_font(), theme::SMALL_TEXT);
                ui::text(std::format("{}:{}", hit.span.from.line + 1, hit.span.from.byte + 1), theme::text_disabled());
            }
            if (Document* document = workspace_->find(hit.path)) {
                preview_->text_size = text_size;
                const glm::vec2 at = ui::cursor();
                const glm::vec2 size{inner, PREVIEW_LINES * code_line(text_size)};
                preview_focused = code_view("Preview", *document, *preview_, keys, {.min = at, .max = at + size}).focused;
                ui::reserve(size);
            }
        }

        // What the keys do here.
        ui::text(vim ? "Enter opens at the match   Up Down, Ctrl J Ctrl K move through them   Esc closes"
                     : "Enter opens at the match   Up Down move through them   Esc closes",
                 theme::text_secondary());
    }

    // Up and Down go through the matches while the search has the keyboard;
    // with Vim's keys, Ctrl-J and Ctrl-K too.
    if (!preview_focused && !hits_.empty()) {
        const std::size_t before = selected_;
        const bool ctrl = vim && context.modifiers().ctrl;
        const bool down = context.key_repeated(ui::Key::Down) || (ctrl && context.key_repeated(ui::Key::J));
        const bool up = context.key_repeated(ui::Key::Up) || (ctrl && context.key_repeated(ui::Key::K));
        if (down && selected_ + 1 < hits_.size()) ++selected_;
        if (up && selected_ > 0) --selected_;
        if (selected_ != before) {
            preview(hits_[selected_]);
            reveal_ = true;
            context.request_frame();
        }
    }
    if (go && selected_ < hits_.size()) {
        ask.go = hits_[selected_];
        close(ask);
    } else if (closing) {
        close(ask);
    }
    return ask;
}

} // namespace fjedit
