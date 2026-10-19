#include "fjedit/widget/completion.hpp"

#include "fjedit/core/document.hpp"
#include "fjedit/widget/code_view.hpp"
#include "fjedit/widget/icons.hpp"
#include "fjedit/widget/markdown.hpp"

#include "ftk/ui/core/context.hpp"
#include "ftk/ui/kit/layout.hpp"
#include "ftk/ui/kit/plate.hpp"
#include "ftk/ui/kit/region.hpp"
#include "ftk/ui/kit/text.hpp"
#include "ftk/ui/kit/tree.hpp"
#include "ftk/ui/theme.hpp"

#include <algorithm>
#include <utility>

namespace fjedit {

namespace {

namespace ui = ftk::ui;
namespace core = ftk::ui::core;
namespace theme = ftk::theme;

// P1's measures beside code at 13: the names at 12.5; the list 250 wide;
// the documentation 350 wide, padded 12 by 10.
constexpr float NAME_TEXT = 12.5f;
constexpr float LIST_WIDTH = 250.0f;
constexpr float DOCS_WIDTH = 350.0f;
constexpr glm::vec2 DOCS_PAD{12.0f, 10.0f};

bool digit(char c) {
    return c >= '0' && c <= '9';
}

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// Whether `typed`'s characters come in `filter` in order, case aside.
bool matches(std::string_view filter, std::string_view typed) {
    std::size_t at = 0;
    for (const char c : filter) {
        if (at < typed.size() && lower(c) == lower(typed[at])) ++at;
    }
    return at == typed.size();
}

bool begins_with(std::string_view filter, std::string_view typed) {
    if (typed.size() > filter.size()) return false;
    for (std::size_t i = 0; i < typed.size(); ++i) {
        if (lower(filter[i]) != lower(typed[i])) return false;
    }
    return true;
}

// Where the word the caret is in or after begins on `line`.
std::size_t word_start(const Language& language, std::string_view line, std::size_t caret) {
    std::size_t byte = std::min(caret, line.size());
    while (byte > 0 && word_byte(language, line[byte - 1])) --byte;
    return byte;
}

// An entry's icon, in the hue of what it names as code is coloured.
struct KindLook {
    const char* icon;
    std::optional<theme::Hue> hue;
};

KindLook look_of(CompletionKind kind) {
    switch (kind) {
        case CompletionKind::Method:
        case CompletionKind::Function:
        case CompletionKind::Constructor:
            return {icon::function, theme::Hue::blue};
        case CompletionKind::Field:
        case CompletionKind::Property:
            return {icon::variable, theme::Hue::periwinkle};
        case CompletionKind::Variable:
            return {icon::variable, std::nullopt};
        case CompletionKind::Class:
        case CompletionKind::Interface:
        case CompletionKind::Struct:
            return {icon::type, theme::Hue::teal};
        case CompletionKind::TypeParameter:
            return {icon::type_parameter, theme::Hue::teal};
        case CompletionKind::Module:
            return {icon::module, theme::Hue::teal};
        case CompletionKind::Enum:
            return {icon::enumeration, theme::Hue::teal};
        case CompletionKind::EnumMember:
            return {icon::enum_member, theme::Hue::orange};
        case CompletionKind::Constant:
        case CompletionKind::Value:
        case CompletionKind::Unit:
            return {icon::constant, theme::Hue::orange};
        case CompletionKind::Keyword:
            return {icon::keyword, theme::Hue::lilac};
        case CompletionKind::Operator:
            return {icon::operation, std::nullopt};
        case CompletionKind::File:
            return {icon::file, std::nullopt};
        case CompletionKind::Folder:
            return {icon::folder, std::nullopt};
        case CompletionKind::Reference:
        case CompletionKind::Color:
        case CompletionKind::Event:
            return {icon::reference, std::nullopt};
        case CompletionKind::Text:
        case CompletionKind::Snippet:
            break;
    }
    return {icon::text, std::nullopt};
}

// The entries that match `typed`, best first: those it begins, then the
// rest, each in the order the service prefers.
std::vector<std::size_t> matching(const Completions& completions, std::string_view typed) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < completions.items.size(); ++i) {
        if (matches(completions.items[i].filter, typed)) out.push_back(i);
    }
    std::ranges::stable_sort(out, [&](std::size_t a, std::size_t b) {
        const Completion& first = completions.items[a];
        const Completion& second = completions.items[b];
        const bool first_begins = begins_with(first.filter, typed);
        if (first_begins != begins_with(second.filter, typed)) return first_begins;
        return first.sort < second.sort;
    });
    return out;
}

} // namespace

std::optional<CompletionTrigger> asks_completion(const Language& language, std::span<const std::string> triggers,
                                                 std::string_view line, std::size_t caret) {
    if (caret == 0 || caret > line.size()) return std::nullopt;
    if (word_byte(language, line[caret - 1])) {
        // A word, not a number: in a language whose words take `-`, not a
        // negative one either.
        std::size_t first = word_start(language, line, caret);
        while (first < caret && line[first] == '-') ++first;
        if (first < caret && digit(line[first])) return std::nullopt;
        return CompletionTrigger{};
    }
    const std::string_view before = line.substr(0, caret);
    for (const std::string& trigger : triggers) {
        if (!before.ends_with(trigger)) continue;
        // A number's point is no member access.
        if (trigger == "." && caret >= 2 && digit(line[caret - 2])) return std::nullopt;
        return CompletionTrigger{.kind = CompletionTrigger::Kind::Character, .character = trigger};
    }
    return std::nullopt;
}

void open_completion(CompletionState& state, LanguageService& service, const std::filesystem::path& path,
                     const Document& document, Position caret, const CompletionTrigger& trigger) {
    const Buffer& buffer = document.buffer();
    caret = buffer.clamp(caret);
    const std::string_view line = buffer.line(caret.line);
    const Position start{caret.line, word_start(document.language(), line, caret.byte)};
    std::string typed(line.substr(start.byte, caret.byte - start.byte));
    state = CompletionState{.open = true,
                            .start = start,
                            .asked = service.complete(path, caret, trigger),
                            .asked_for = typed,
                            .typed = typed};
}

void follow_completion(CompletionState& state, LanguageService& service, const std::filesystem::path& path,
                       const Document& document, Position caret) {
    if (!state.open) return;
    const Buffer& buffer = document.buffer();
    caret = buffer.clamp(caret);
    if (caret.line != state.start.line || caret.byte < state.start.byte) {
        state = {};
        return;
    }
    const std::string typed(buffer.line(caret.line).substr(state.start.byte, caret.byte - state.start.byte));
    if (!std::ranges::all_of(typed, [&](char c) { return word_byte(document.language(), c); })) {
        state = {};
        return;
    }
    if (!state.asked->answered) {
        state.typed = typed;
        return;
    }
    const Completions& answer = state.asked->answer;
    if (typed != state.asked_for && answer.incomplete) {
        state.asked = service.complete(path, caret, {.kind = CompletionTrigger::Kind::Incomplete, .character = {}});
        state.asked_for = typed;
        state.typed = typed;
        state.shown.clear();
        return;
    }
    if (typed != state.typed || state.shown.empty()) {
        state.shown = matching(answer, typed);
        state.selected = 0;
        state.typed = typed;
    }
    // Nothing completes what is typed.
    if (state.shown.empty()) state = {};
}

void move_completion(CompletionState& state, std::ptrdiff_t by) {
    if (state.shown.empty()) return;
    const auto last = static_cast<std::ptrdiff_t>(state.shown.size()) - 1;
    state.selected = static_cast<std::size_t>(std::clamp(static_cast<std::ptrdiff_t>(state.selected) + by, std::ptrdiff_t{0}, last));
    state.reveal = true;
}

void accept_completion(CompletionState& state, CommandContext& context, std::optional<std::size_t> index) {
    const std::size_t chosen = index.value_or(state.selected);
    if (!state.open || !state.asked || !state.asked->answered || chosen >= state.shown.size()) {
        state = {};
        return;
    }
    const Completions& answer = state.asked->answer;
    const Completion& item = answer.items[state.shown[chosen]];
    const Document& document = context.document;
    const Position caret = document.buffer().clamp(context.selections.primary().caret);

    // Its text for the word, from where its own edit begins up to the caret,
    // wherever more typing has taken that.
    const Position from = item.change.span.from.line == caret.line && item.change.span.from <= caret
                              ? item.change.span.from
                              : state.start;
    const Span word{.from = from, .to = caret};
    Edit edit;
    edit.changes.push_back({.span = word, .text = item.change.text});
    // What it brings along, carried through the edits made since it was
    // asked for; dropped when they can't be followed, or when it meets the
    // word.
    if (const auto since = document.edits_since(answer.version)) {
        for (Change more : item.more) {
            for (const Edit& made : *since) {
                more.span = {.from = map(made, more.span.from, Bias::Before), .to = map(made, more.span.to, Bias::After)};
            }
            if (more.span.to <= word.from || more.span.from >= word.to) edit.changes.push_back(std::move(more));
        }
    }
    std::ranges::stable_sort(edit.changes, {}, [](const Change& change) { return change.span.from; });
    const auto own = std::ranges::find(edit.changes, word, &Change::span);
    const Position after = spans_after(edit)[static_cast<std::size_t>(own - edit.changes.begin())].to;
    (void)apply_edit(context, std::move(edit), after);
    state = {};
}

std::optional<std::size_t> completion_card(CompletionState& state, glm::vec2 view_min, glm::vec2 view_max,
                                           glm::vec2 anchor_min, glm::vec2 anchor_max, float text_size,
                                           std::string_view extension) {
    if (!state.open || !state.asked || !state.asked->answered || state.shown.empty()) return std::nullopt;
    core::Context& context = core::current();
    const Completions& answer = state.asked->answer;
    const Completion& selected = answer.items[state.shown[state.selected]];
    const float scale = text_size / CODE_TEXT;

    // Beside the list: the selected entry's signature in the file's
    // language, then what its documentation says.
    std::vector<MarkdownBlock> docs;
    if (!selected.parameters.empty() || !selected.detail.empty()) {
        std::string signature = selected.detail.empty() ? std::string() : selected.detail + " ";
        signature += selected.label + selected.parameters;
        docs.push_back({.kind = MarkdownBlock::Kind::Code, .language = std::string(extension), .lines = {signature}});
    }
    std::vector<MarkdownBlock> written = read_markdown(selected.documentation);
    if (!written.empty()) {
        if (!docs.empty()) docs.push_back({.kind = MarkdownBlock::Kind::Rule});
        docs.insert(docs.end(), std::make_move_iterator(written.begin()), std::make_move_iterator(written.end()));
    }

    const float list_width = LIST_WIDTH * scale;
    const float docs_width = docs.empty() ? 0.0f : DOCS_WIDTH * scale;
    const auto rows = static_cast<float>(std::min(state.shown.size(), COMPLETION_ROWS));
    std::optional<std::size_t> clicked;
    const ui::Card card("##completion", {.view_min = view_min,
                                         .view_max = view_max,
                                         .anchor_min = anchor_min,
                                         .anchor_max = anchor_max,
                                         .width = list_width + docs_width,
                                         .padding = {0.0f, 0.0f}});
    {
        // The names in the mono face, what is typed of them in amber.
        const ui::FontScope mono(theme::mono_font(), NAME_TEXT * scale);
        if (auto list = ui::Region("##list", {list_width, rows * theme::TREE_ROW})) {
            if (auto tree = ui::Tree("##entries")) {
                const ui::RowsInView in_view(state.shown.size(), std::exchange(state.reveal, false)
                                                                     ? std::optional(state.selected)
                                                                     : std::nullopt);
                for (std::size_t i = in_view.first(); i < in_view.end(); ++i) {
                    const Completion& item = answer.items[state.shown[i]];
                    const KindLook look = look_of(item.kind);
                    const std::string id = std::to_string(i);
                    const ui::TreeRowResult row = ui::tree_row({
                        .id = id.c_str(),
                        .name = item.label,
                        .icon = look.icon,
                        .hue = look.hue,
                        .tag = item.detail,
                        .selection = i == state.selected ? ui::RowSelection::Primary : ui::RowSelection::None,
                        .query = state.typed,
                    });
                    if (row.clicked) clicked = i;
                }
            }
        }
    }
    if (!docs.empty()) {
        // Right of the list, a rule between them.
        ui::same_line(0.0f);
        const glm::vec2 at = ui::cursor();
        const glm::vec2 pad = DOCS_PAD * scale;
        const float height = draw_markdown(context, docs, at + pad, docs_width - 2.0f * pad.x, card_look(scale)) + 2.0f * pad.y;
        context.draw_list().rect_filled(at, {at.x + 1.0f, at.y + std::max(height, rows * theme::TREE_ROW)},
                                        theme::surface_raised());
        ui::reserve({docs_width, height});
    }
    return clicked;
}

} // namespace fjedit
