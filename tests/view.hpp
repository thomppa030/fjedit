#pragma once

// A document in one view, laid out in monospace cells, for running commands
// as someone typing would. Text is written with its selections marked in it:
// `|` is a caret, `[` and `]` the ends of a selection (made forwards), and
// the last marked is the primary.
//
//     TestView view("a|b\nc|d");
//     view.run("caret.right");
//     REQUIRE(view.shown() == "ab|\ncd|");

#include "fjedit/core/commands.hpp"

#include "print.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct TestView {
    fjedit::Buffer buffer;
    fjedit::History history;
    fjedit::Selections selections;
    fjedit::MonospaceLayout layout{4, 3};

    explicit TestView(std::string_view marked) {
        std::string text;
        std::vector<fjedit::Selection> marks;
        fjedit::Position at;
        std::optional<fjedit::Position> open;
        for (const char c : marked) {
            if (c == '|') {
                marks.push_back(fjedit::Selection::at(at));
            } else if (c == '[') {
                open = at;
            } else if (c == ']') {
                marks.push_back({.anchor = *open, .caret = at});
                open.reset();
            } else {
                text += c;
                at = c == '\n' ? fjedit::Position{at.line + 1, 0} : fjedit::Position{at.line, at.byte + 1};
            }
        }
        buffer = fjedit::Buffer(text);
        if (!marks.empty()) selections.set(marks, marks.size() - 1);
    }

    void run(std::string_view name) {
        fjedit::CommandContext context{.buffer = buffer, .history = history, .selections = selections,
                                       .layout = layout};
        REQUIRE(fjedit::run_command(name, context));
    }

    [[nodiscard]] std::vector<fjedit::Position> carets() const {
        std::vector<fjedit::Position> out;
        for (const fjedit::Selection& s : selections.all()) out.push_back(s.caret);
        return out;
    }

    /// The text with the selections marked in it, as the view was made from.
    [[nodiscard]] std::string shown() const {
        // At one place, a selection's end comes before a caret, and a caret
        // before a selection's start.
        std::vector<std::pair<fjedit::Position, char>> marks;
        for (const fjedit::Selection& s : selections.all()) {
            if (s.empty()) {
                marks.emplace_back(s.caret, '|');
            } else {
                marks.emplace_back(s.span().from, '[');
                marks.emplace_back(s.span().to, ']');
            }
        }
        auto rank = [](char c) { return c == ']' ? 0 : c == '|' ? 1 : 2; };
        std::stable_sort(marks.begin(), marks.end(), [&](const auto& a, const auto& b) {
            return a.first != b.first ? a.first < b.first : rank(a.second) < rank(b.second);
        });

        std::string out;
        std::size_t next = 0;
        for (std::size_t line = 0; line < buffer.line_count(); ++line) {
            if (line > 0) out += '\n';
            const std::string_view text = buffer.line(line);
            for (std::size_t byte = 0; byte <= text.size(); ++byte) {
                while (next < marks.size() && marks[next].first == fjedit::Position{line, byte}) out += marks[next++].second;
                if (byte < text.size()) out += text[byte];
            }
        }
        return out;
    }
};
