#include "fjedit/core/language.hpp"

#include "fjedit/core/lex/lexers.hpp"

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

// Each extension fjedit knows, lower case, and its language.
struct Known {
    std::string_view extension;
    const Language& (*language)();
};

constexpr Known BY_EXTENSION[] = {
    {"c", cpp},   {"cc", cpp},  {"cpp", cpp}, {"cxx", cpp}, {"h", cpp},
    {"hh", cpp},  {"hpp", cpp}, {"hxx", cpp}, {"inl", cpp}, {"ipp", cpp},
};

} // namespace

const Language& plain_text() {
    static const PlainLexer lexer;
    static const Language language{.name = "Plain text", .lexer = lexer, .line_comment = ""};
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

} // namespace fjedit
