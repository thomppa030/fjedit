#pragma once

// The text of a file as the editor holds it: one string per line, without
// its line break. The file's line break and byte order mark are kept apart
// and given back when the text is written out, so a file saved unchanged
// comes back byte for byte. A file whose lines end in mixed breaks comes
// back with the one it had most of.
//
//     fjedit::Buffer buffer(file_text);
//     for (std::size_t i = 0; i < buffer.line_count(); ++i) draw(buffer.line(i));
//     write(path, buffer.text());

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fjedit {

/// A place in the text: a line, and a byte of that line's UTF-8, both
/// counted from 0. A position sits between two characters; byte 0 is before
/// the line's first and the line's length after its last.
struct Position {
    std::size_t line{0};
    std::size_t byte{0};

    auto operator<=>(const Position&) const = default;
};

/// The text from one position up to another; `from` is never after `to`.
struct Span {
    Position from;
    Position to;

    [[nodiscard]] bool empty() const { return from == to; }
    bool operator==(const Span&) const = default;
};

/// The bytes of a UTF-8 byte order mark, which a Buffer keeps apart from
/// its first line.
inline constexpr std::string_view BYTE_ORDER_MARK = "\xEF\xBB\xBF";

/// How a file breaks its lines.
enum class LineEnding : uint8_t {
    Lf,
    CrLf,
};

/// One change of an edit: the text in `span` replaced by `text`. Lines in
/// `text` break at \n, \r\n or a lone \r, all taken as one break.
struct Change {
    Span span;
    std::string text;

    bool operator==(const Change&) const = default;
};

/// Changes made together, as one step: typing at several cursors, a
/// replace-all. Their spans are in the text as it was before the edit, in
/// order, and never overlap, though one may end where the next begins.
struct Edit {
    std::vector<Change> changes;

    bool operator==(const Edit&) const = default;
};

/// Where each change's text lies once `edit` is made: for a change that
/// inserts, its new text; for one that only deletes, an empty span where
/// the text was.
[[nodiscard]] std::vector<Span> spans_after(const Edit& edit);

/// Which side of new text a position at the very place it went in ends up.
enum class Bias : uint8_t {
    /// Before it: an insertion there pushes nothing.
    Before,
    /// After it: the caret of the one typing.
    After,
};

/// Where `at`, a position in the text before `edit`, is once the edit is
/// made. A position at the start of a replaced span stays before the new
/// text, one at its end follows it, and one where text is inserted or
/// inside a replaced span goes to the side `bias` says.
[[nodiscard]] Position map(const Edit& edit, Position at, Bias bias);

class Buffer {
public:
    /// An empty text: one line with nothing on it.
    Buffer();

    /// `text` as a file holds it, split into lines at its breaks: \n, \r\n,
    /// or a lone \r. A text that ends in a break has an empty last line.
    explicit Buffer(std::string_view text);

    [[nodiscard]] std::size_t line_count() const { return lines_.size(); }

    /// The text of line `index`, without its break. `index` must be less than
    /// line_count().
    [[nodiscard]] std::string_view line(std::size_t index) const { return lines_[index]; }

    /// The position after the last character of the last line.
    [[nodiscard]] Position end() const;

    /// The text between `span`'s ends, its lines joined with \n. Both ends
    /// must be in the text (see clamp()).
    [[nodiscard]] std::string text(Span span) const;

    /// The whole text as the file holds it: its byte order mark, when it
    /// had one, then the lines joined with its line break.
    [[nodiscard]] std::string text() const;

    /// The break the lines are joined with in text().
    [[nodiscard]] LineEnding line_ending() const { return line_ending_; }
    void set_line_ending(LineEnding ending) { line_ending_ = ending; }

    /// Whether the text began with a UTF-8 byte order mark, which text()
    /// gives back and line(0) leaves out.
    [[nodiscard]] bool byte_order_mark() const { return byte_order_mark_; }
    void set_byte_order_mark(bool mark) { byte_order_mark_ = mark; }

    /// `at` moved into the text: onto the last line when past it, onto the
    /// end of its line when past that, and back to the start of a character
    /// whose bytes it falls between.
    [[nodiscard]] Position clamp(Position at) const;

    /// Makes `edit` and returns the edit that undoes it, whose spans are in
    /// the text as it is now. Every span must be in the text (see clamp())
    /// and the changes in order without overlapping.
    [[nodiscard]] Edit apply(const Edit& edit);

    /// Counts the edits made: it moves with every apply(), so whoever holds
    /// a copy of the text (a language server) can tell it is out of date.
    [[nodiscard]] uint64_t version() const { return version_; }

private:
    // Puts `text` where `span` was.
    void replace(Span span, std::string_view text);

    std::vector<std::string> lines_;
    LineEnding line_ending_{LineEnding::Lf};
    bool byte_order_mark_{false};
    uint64_t version_{0};
};

} // namespace fjedit
