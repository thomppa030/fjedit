#pragma once

// How a failed check prints the core's values: "3:14" for a position,
// "3:14-3:20" for a span, "3:14>3:20" for a selection from its anchor to
// its caret, "lines 3..9" for a fold, "3:14-3:20 in "the line"" for a match
// in a text.

#include "fjedit/core/find.hpp"
#include "fjedit/core/selections.hpp"
#include "fjedit/core/structure.hpp"

#include <catch2/catch_tostring.hpp>

#include <string>

template <>
struct Catch::StringMaker<fjedit::Position> {
    static std::string convert(const fjedit::Position& at) {
        return std::to_string(at.line) + ":" + std::to_string(at.byte);
    }
};

template <>
struct Catch::StringMaker<fjedit::Span> {
    static std::string convert(const fjedit::Span& span) {
        return StringMaker<fjedit::Position>::convert(span.from) + "-" + StringMaker<fjedit::Position>::convert(span.to);
    }
};

template <>
struct Catch::StringMaker<fjedit::Selection> {
    static std::string convert(const fjedit::Selection& s) {
        return StringMaker<fjedit::Position>::convert(s.anchor) + ">" + StringMaker<fjedit::Position>::convert(s.caret);
    }
};

template <>
struct Catch::StringMaker<fjedit::FoldRange> {
    static std::string convert(const fjedit::FoldRange& fold) {
        return "lines " + std::to_string(fold.first) + ".." + std::to_string(fold.last);
    }
};

template <>
struct Catch::StringMaker<fjedit::TextMatch> {
    static std::string convert(const fjedit::TextMatch& match) {
        return StringMaker<fjedit::Span>::convert(match.span) + " in \"" + std::string(match.line) + "\"";
    }
};
