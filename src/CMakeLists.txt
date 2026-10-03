# fjedit-core is the editor without a window: the text and how it is edited.
# It needs no UI, so its tests run headless.
add_library(fjedit-core STATIC
    fjedit/core/buffer.cpp
    fjedit/core/commands.cpp
    fjedit/core/find.cpp
    fjedit/core/history.cpp
    fjedit/core/keymap.cpp
    fjedit/core/language.cpp
    fjedit/core/layout.cpp
    fjedit/core/lex/c_like.cpp
    fjedit/core/lex/cpp.cpp
    fjedit/core/motion.cpp
    fjedit/core/selections.cpp
    fjedit/core/syntax.cpp)
add_library(fjedit::core ALIAS fjedit-core)
ftk_target_defaults(fjedit-core)
target_include_directories(fjedit-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# ftk-ui-core for its key vocabulary, which keymaps are written in; the core
# draws nothing.
target_link_libraries(fjedit-core PUBLIC ftk::base ftk::ui-core)
