#include "fjedit/core/vim/registers.hpp"

#include <cstddef>
#include <utility>

namespace fjedit::vim {

namespace {

bool lower(char name) {
    return name >= 'a' && name <= 'z';
}

bool upper(char name) {
    return name >= 'A' && name <= 'Z';
}

bool digit(char name) {
    return name >= '0' && name <= '9';
}

bool clipboard_name(char name) {
    return name == '+' || name == '*';
}

std::string joined(const std::vector<std::string>& pieces) {
    std::string out;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        if (i > 0) out += '\n';
        out += pieces[i];
    }
    return out;
}

// `added` after what `held` holds, as `"A` adds: piece to piece when they
// have as many, whole otherwise; whole lines when either is, a line break
// between.
Yanked appended(const Yanked& held, const Yanked& added) {
    Yanked out;
    out.linewise = held.linewise || added.linewise;
    const std::string between = out.linewise ? "\n" : "";
    if (held.pieces.size() == added.pieces.size()) {
        for (std::size_t i = 0; i < held.pieces.size(); ++i) out.pieces.push_back(held.pieces[i] + between + added.pieces[i]);
    } else {
        out.pieces.push_back(joined(held.pieces) + between + joined(added.pieces));
    }
    return out;
}

} // namespace

std::string Yanked::text() const {
    std::string out = joined(pieces);
    if (linewise) out += '\n';
    return out;
}

bool Registers::valid(char name) {
    return name == '"' || lower(name) || upper(name) || digit(name) || name == '-' || name == '_' ||
           clipboard_name(name);
}

std::optional<Yanked> Registers::get(char name, const Clipboard* clipboard) const {
    if (upper(name)) name = static_cast<char>(name - 'A' + 'a');
    if (name == '"') return unnamed_;
    if (digit(name)) return numbered_[static_cast<std::size_t>(name - '0')];
    if (lower(name)) return named_[static_cast<std::size_t>(name - 'a')];
    if (name == '-') return small_;
    if (clipboard_name(name) && clipboard != nullptr && clipboard->get) {
        std::string text = clipboard->get();
        if (text.empty()) return std::nullopt;
        const bool linewise = text.back() == '\n';
        if (linewise) text.pop_back();
        return Yanked{.pieces = {std::move(text)}, .linewise = linewise};
    }
    return std::nullopt;
}

void Registers::put_in(char name, const Yanked& yanked, Clipboard* clipboard) {
    if (upper(name)) {
        std::optional<Yanked>& held = named_[static_cast<std::size_t>(name - 'A')];
        held = held ? appended(*held, yanked) : yanked;
    } else if (lower(name)) {
        named_[static_cast<std::size_t>(name - 'a')] = yanked;
    } else if (digit(name)) {
        numbered_[static_cast<std::size_t>(name - '0')] = yanked;
    } else if (clipboard_name(name) && clipboard != nullptr && clipboard->set) {
        clipboard->set(yanked.text());
    }
}

void Registers::yank(char name, Yanked yanked, Clipboard* clipboard) {
    if (name == '_') return;
    if (name == '"') {
        numbered_[0] = yanked;
    } else {
        put_in(name, yanked, clipboard);
    }
    // The unnamed register holds what the one written holds, appended to.
    unnamed_ = upper(name) ? get(name, clipboard) : std::optional<Yanked>(std::move(yanked));
}

void Registers::deleted(char name, Yanked yanked, Clipboard* clipboard, bool numbered) {
    if (name == '_') return;
    bool reaches_over_lines = yanked.linewise;
    for (const std::string& piece : yanked.pieces) reaches_over_lines = reaches_over_lines || piece.find('\n') != std::string::npos;
    if (name != '"') put_in(name, yanked, clipboard);
    // Whole lines and more go to 1, even with a register named; a little
    // with none named goes to -.
    if (reaches_over_lines || numbered) {
        for (std::size_t i = numbered_.size() - 1; i > 1; --i) numbered_[i] = std::move(numbered_[i - 1]);
        numbered_[1] = yanked;
    }
    if (name == '"' && !reaches_over_lines) small_ = yanked;
    unnamed_ = upper(name) ? get(name, clipboard) : std::optional<Yanked>(std::move(yanked));
}

} // namespace fjedit::vim
