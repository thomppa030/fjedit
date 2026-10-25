#include "fjedit/core/language.hpp"

#include "fjedit/core/lex/lexers.hpp"
#include "fjedit/core/lex/scan.hpp"

#include <string>

namespace fjedit {

namespace {

class PlainLexer final : public Lexer {
public:
    [[nodiscard]] LineState lex(std::string_view, const LineState&, std::vector<Token>&) const override { return {}; }
};

const Language& cpp() {
    static const Language language{.name = "C++", .lexer = lex::cpp(), .line_comment = "//"};
    return language;
}

const Language& glsl() {
    static const Language language{.name = "GLSL", .lexer = lex::glsl(), .line_comment = "//"};
    return language;
}

const Language& fjsl() {
    static const Language language{.name = "FJSL", .lexer = lex::fjsl(), .line_comment = "//"};
    return language;
}

const Language& fjss() {
    static const Language language{.name = "FJSS", .lexer = lex::fjss(), .line_comment = "", .word_also = "-"};
    return language;
}

const Language& widget() {
    // A widget's tags, attributes and properties run through hyphens and
    // colons: health-bar, on:click, class:low, style---fill.
    static const Language language{.name = "Widget", .lexer = lex::markup(), .line_comment = "",
                                   .folding = Folding::Indentation, .word_also = "-:"};
    return language;
}

const Language& json() {
    static const Language language{.name = "JSON", .lexer = lex::json(), .line_comment = "//"};
    return language;
}

// Each extension fjedit knows, lower case, and its language.
struct Known {
    std::string_view extension;
    const Language& (*language)();
};

constexpr Known BY_EXTENSION[] = {
    {"c", cpp},   {"cc", cpp},  {"cpp", cpp}, {"cxx", cpp}, {"h", cpp},
    {"hh", cpp},  {"hpp", cpp}, {"hxx", cpp}, {"inl", cpp}, {"ipp", cpp},
    {"comp", glsl}, {"frag", glsl}, {"geom", glsl}, {"glsl", glsl}, {"mesh", glsl}, {"rahit", glsl},
    {"rcall", glsl}, {"rchit", glsl}, {"rgen", glsl}, {"rint", glsl}, {"rmiss", glsl}, {"task", glsl},
    {"tesc", glsl}, {"tese", glsl}, {"vert", glsl},
    {"fjsl", fjsl}, {"fjss", fjss}, {"fjui", widget}, {"fjwidget", widget},
    {"fjanimset", json}, {"fjday", json}, {"fjell", json}, {"fjfont", json}, {"fjgraph", json}, {"fjinput", json},
    {"fjlut", json}, {"fjmat", json}, {"fjp", json}, {"fjsurface", json}, {"fjvfx", json}, {"fjwater", json},
    {"fjweather", json}, {"json", json},
};

} // namespace

bool word_byte(const Language& language, char c) {
    return lex::name_char(c) || (c != '\0' && language.word_also.find(c) != std::string_view::npos);
}

const Language& plain_text() {
    static const PlainLexer lexer;
    static const Language language{.name = "Plain text", .lexer = lexer, .line_comment = "",
                                   .folding = Folding::Indentation};
    return language;
}

const Language& language_for(std::string_view file_name) {
    const std::size_t slash = file_name.find_last_of("/\\");
    const std::string_view name = slash == std::string_view::npos ? file_name : file_name.substr(slash + 1);
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos) return plain_text();

    std::string extension(name.substr(dot + 1));
    for (char& c : extension) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    for (const Known& known : BY_EXTENSION) {
        if (known.extension == extension) return known.language();
    }
    return plain_text();
}

std::vector<std::string_view> extensions_of(const Language& language) {
    std::vector<std::string_view> out;
    for (const Known& known : BY_EXTENSION) {
        if (&known.language() == &language) out.push_back(known.extension);
    }
    return out;
}

} // namespace fjedit
