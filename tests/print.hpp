#pragma once

// How a failed check prints the core's values: "3:14" for a position,
// "3:14-3:20" for a span, "3:14>3:20" for a selection from its anchor to
// its caret.

#include "fjedit/core/selections.hpp"

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
