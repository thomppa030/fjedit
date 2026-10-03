// GLSL's words, and FJSL's: GLSL with shader_type, render_mode and the rest
// of its own, its hints (hint_range) as meta, and the names after
// shader_type and render_mode as keywords.

#include "fjedit/core/lex/c_like.hpp"
#include "fjedit/core/lex/lexers.hpp"

#include <algorithm>
#include <array>

namespace fjedit::lex {

namespace {

constexpr auto GLSL_KEYWORDS = std::to_array<std::string_view>({
    "bool", "break", "buffer", "case", "centroid", "coherent", "const", "continue", "default", "discard", "do",
    "double", "else", "false", "flat", "float", "for", "highp", "if", "in", "inout", "int", "invariant",
    "layout", "lowp", "mediump", "noperspective", "out", "patch", "precise", "precision", "readonly",
    "restrict", "return", "sample", "shared", "smooth", "struct", "subroutine", "switch", "true", "uint",
    "uniform", "varying", "void", "volatile", "while", "writeonly",
});
static_assert(std::ranges::is_sorted(GLSL_KEYWORDS));

constexpr auto FJSL_KEYWORDS = std::to_array<std::string_view>({
    "bool", "break", "buffer", "case", "centroid", "coherent", "compute", "const", "continue", "default",
    "discard", "do", "double", "else", "false", "flat", "float", "for", "highp", "if", "image", "in", "inout",
    "instance", "int", "invariant", "layout", "lowp", "mediump", "noperspective", "out", "pass", "patch",
    "precise", "precision", "readonly", "render_mode", "restrict", "return", "sample", "shader_type", "shared",
    "smooth", "struct", "subroutine", "switch", "true", "uint", "uniform", "use", "varying", "void", "volatile",
    "while", "writeonly",
});
static_assert(std::ranges::is_sorted(FJSL_KEYWORDS));

constexpr auto GLSL_TYPES = std::to_array<std::string_view>({
    "accelerationStructureEXT", "atomic_uint", "bvec2", "bvec3", "bvec4", "dmat2", "dmat3", "dmat4", "dvec2",
    "dvec3", "dvec4", "iimage2D", "iimage3D", "image1D", "image2D", "image2DArray", "image3D", "imageCube",
    "isampler2D", "isampler2DArray", "isampler3D", "isamplerCube", "ivec2", "ivec3", "ivec4", "mat2", "mat2x2",
    "mat2x3", "mat2x4", "mat3", "mat3x2", "mat3x3", "mat3x4", "mat4", "mat4x2", "mat4x3", "mat4x4",
    "rayQueryEXT", "sampler1D", "sampler2D", "sampler2DArray", "sampler2DArrayShadow", "sampler2DMS",
    "sampler2DShadow", "sampler3D", "samplerCube", "samplerCubeArray", "samplerCubeShadow", "uimage2D",
    "uimage3D", "usampler2D", "usampler2DArray", "usampler3D", "usamplerCube", "uvec2", "uvec3", "uvec4",
    "vec2", "vec3", "vec4",
});
static_assert(std::ranges::is_sorted(GLSL_TYPES));

// shader_type spatial;
constexpr auto KEYWORD_NEXT = std::to_array<std::string_view>({"shader_type"});
// render_mode unlit, cutout;
constexpr auto KEYWORDS_TO_SEMICOLON = std::to_array<std::string_view>({"render_mode"});

} // namespace

const Lexer& glsl() {
    static const CLikeLexer lexer({.keywords = GLSL_KEYWORDS, .types = GLSL_TYPES});
    return lexer;
}

const Lexer& fjsl() {
    static const CLikeLexer lexer({.keywords = FJSL_KEYWORDS,
                                   .types = GLSL_TYPES,
                                   .meta_prefix = "hint_",
                                   .keyword_next = KEYWORD_NEXT,
                                   .keywords_to_semicolon = KEYWORDS_TO_SEMICOLON});
    return lexer;
}

} // namespace fjedit::lex
