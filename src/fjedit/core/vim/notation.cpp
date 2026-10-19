#include "fjedit/core/vim/notation.hpp"

#include "ftk/base/utf8.hpp"

#include <cstddef>

namespace fjedit::vim {

namespace {

using ftk::ui::Key;

struct Named {
    Key key;
    std::string_view name;
};

// The keys that type nothing, by the names Vim gives them in brackets;
// notation() writes the first name of each.
constexpr Named UNTYPED[] = {
    {Key::Escape, "Esc"},       {Key::Enter, "CR"},         {Key::Enter, "Enter"},     {Key::Enter, "Return"},
    {Key::Tab, "Tab"},          {Key::Backspace, "BS"},     {Key::Backspace, "BackSpace"},
    {Key::Delete, "Del"},       {Key::Delete, "Delete"},    {Key::Insert, "Insert"},   {Key::Home, "Home"},
    {Key::End, "End"},          {Key::PageUp, "PageUp"},    {Key::PageDown, "PageDown"},
    {Key::Left, "Left"},        {Key::Right, "Right"},      {Key::Up, "Up"},           {Key::Down, "Down"},
    {Key::F1, "F1"},            {Key::F2, "F2"},            {Key::F3, "F3"},           {Key::F4, "F4"},
    {Key::F5, "F5"},            {Key::F6, "F6"},            {Key::F7, "F7"},           {Key::F8, "F8"},
    {Key::F9, "F9"},            {Key::F10, "F10"},          {Key::F11, "F11"},         {Key::F12, "F12"},
};

// The keys that type a character, named in brackets only with a modifier
// (`<C-r>`, `<A-j>`, `<C-Space>`): Space by its name, Backslash as Vim's
// Bslash, the rest by the character on a US keyboard.
constexpr Named TYPING[] = {
    {Key::Space, "Space"}, {Key::Minus, "-"},       {Key::Equal, "="},      {Key::LeftBracket, "["},
    {Key::RightBracket, "]"}, {Key::Backslash, "Bslash"}, {Key::Semicolon, ";"}, {Key::Apostrophe, "'"},
    {Key::Comma, ","},     {Key::Period, "."},      {Key::Slash, "/"},      {Key::GraveAccent, "`"},
};

// Characters that have names of their own in brackets, unmodified.
struct NamedText {
    std::string_view text;
    std::string_view name;
};
constexpr NamedText NAMED_TEXT[] = {{" ", "Space"}, {"<", "lt"}, {"|", "Bar"}, {"\\", "Bslash"}};

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool same_name(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

std::string name_of(Key key) {
    const auto index = static_cast<int>(key);
    if (key >= Key::A && key <= Key::Z) return std::string(1, static_cast<char>('a' + index - static_cast<int>(Key::A)));
    if (key >= Key::Digit0 && key <= Key::Digit9) {
        return std::string(1, static_cast<char>('0' + index - static_cast<int>(Key::Digit0)));
    }
    for (const Named& named : UNTYPED) {
        if (named.key == key) return std::string(named.name);
    }
    for (const Named& named : TYPING) {
        if (named.key == key) return std::string(named.name);
    }
    return {};
}

// The key `name` names in brackets: one that types nothing, or with
// `modified`, one that types a character too.
std::optional<Key> key_named(std::string_view name, bool modified) {
    for (const Named& named : UNTYPED) {
        if (same_name(named.name, name)) return named.key;
    }
    if (!modified) return std::nullopt;
    if (name.size() == 1) {
        const char c = lower(name[0]);
        if (c >= 'a' && c <= 'z') return static_cast<Key>(static_cast<int>(Key::A) + (c - 'a'));
        if (c >= '0' && c <= '9') return static_cast<Key>(static_cast<int>(Key::Digit0) + (c - '0'));
    }
    for (const Named& named : TYPING) {
        if (same_name(named.name, name)) return named.key;
    }
    return std::nullopt;
}

} // namespace

std::string notation(const Chord& chord) {
    if (!chord.text.empty()) {
        for (const NamedText& named : NAMED_TEXT) {
            if (named.text == chord.text && named.text != "|" && named.text != "\\") {
                return "<" + std::string(named.name) + ">";
            }
        }
        return chord.text;
    }
    std::string out = "<";
    if (chord.modifiers.ctrl) out += "C-";
    if (chord.modifiers.shift) out += "S-";
    if (chord.modifiers.alt) out += "A-";
    if (chord.modifiers.super) out += "D-";
    out += name_of(chord.key);
    out += ">";
    return out;
}

std::optional<std::vector<Chord>> parse_notation(std::string_view text) {
    std::vector<Chord> chords;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t close = text[at] == '<' ? text.find('>', at + 1) : std::string_view::npos;
        if (close != std::string_view::npos && close > at + 1) {
            std::string_view inside = text.substr(at + 1, close - at - 1);
            ftk::ui::Modifiers modifiers;
            bool modified = false;
            while (inside.size() > 2 && inside[1] == '-') {
                const char modifier = lower(inside[0]);
                bool* held = modifier == 'c'                     ? &modifiers.ctrl
                             : modifier == 's'                   ? &modifiers.shift
                             : modifier == 'a' || modifier == 'm' ? &modifiers.alt
                             : modifier == 'd'                   ? &modifiers.super
                                                                 : nullptr;
                if (held == nullptr) return std::nullopt;
                *held = true;
                modified = true;
                inside.remove_prefix(2);
            }
            const NamedText* named_text = nullptr;
            for (const NamedText& named : NAMED_TEXT) {
                if (same_name(named.name, inside)) named_text = &named;
            }
            if (!modified && named_text != nullptr) {
                chords.push_back(Chord::typed(named_text->text));
                at = close + 1;
                continue;
            }
            if (const std::optional<Key> key = key_named(inside, modified)) {
                chords.push_back({.key = *key, .modifiers = modifiers});
                at = close + 1;
                continue;
            }
            if (modified) return std::nullopt;
        }
        // A character, whole.
        std::size_t end = at;
        (void)ftk::utf8::next(text, end);
        chords.push_back(Chord::typed(text.substr(at, end - at)));
        at = end;
    }
    return chords;
}

} // namespace fjedit::vim
