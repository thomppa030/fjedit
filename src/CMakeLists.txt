# fjedit-core is the editor without a window: the text and how it is edited.
# It needs no UI, so its tests run headless.
add_library(fjedit-core STATIC
    fjedit/core/buffer.cpp
    fjedit/core/commands.cpp
    fjedit/core/document.cpp
    fjedit/core/file_search.cpp
    fjedit/core/find.cpp
    fjedit/core/folds.cpp
    fjedit/core/git_ignore.cpp
    fjedit/core/history.cpp
    fjedit/core/jump_list.cpp
    fjedit/core/keymap.cpp
    fjedit/core/language.cpp
    fjedit/core/language_services.cpp
    fjedit/core/layout.cpp
    fjedit/core/lex/c_like.cpp
    fjedit/core/lex/cpp.cpp
    fjedit/core/lex/fjss.cpp
    fjedit/core/lex/markup.cpp
    fjedit/core/lex/glsl.cpp
    fjedit/core/lex/json.cpp
    fjedit/core/motion.cpp
    fjedit/core/recovery.cpp
    fjedit/core/selections.cpp
    fjedit/core/structure.cpp
    fjedit/core/syntax.cpp
    fjedit/core/text_file.cpp
    fjedit/core/vim/engine.cpp
    fjedit/core/vim/ex.cpp
    fjedit/core/vim/keys_file.cpp
    fjedit/core/vim/maps.cpp
    fjedit/core/vim/motions.cpp
    fjedit/core/vim/notation.cpp
    fjedit/core/vim/operators.cpp
    fjedit/core/vim/pattern.cpp
    fjedit/core/vim/registers.cpp
    fjedit/core/vim/text_objects.cpp
    fjedit/core/vim/walk.cpp
    fjedit/core/workspace.cpp)
add_library(fjedit::core ALIAS fjedit-core)
ftk_target_defaults(fjedit-core)
target_include_directories(fjedit-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# ftk-ui-core for its key vocabulary, which keymaps are written in; the core
# draws nothing.
target_link_libraries(fjedit-core PUBLIC ftk::base ftk::ui-core)

# fjedit-widget is the code view, drawn with ftk's UI core in the theme's code
# colours: the text laid out and drawn, and the keyboard and mouse taken.
add_library(fjedit-widget STATIC
    fjedit/widget/code_view.cpp
    fjedit/widget/completion.cpp
    fjedit/widget/file_kind.cpp
    fjedit/widget/find.cpp
    fjedit/widget/go_to_line.cpp
    fjedit/widget/markdown.cpp
    fjedit/widget/minimap.cpp
    fjedit/widget/next_keys.cpp
    fjedit/widget/text_layout.cpp)
add_library(fjedit::widget ALIAS fjedit-widget)
ftk_target_defaults(fjedit-widget)
target_link_libraries(fjedit-widget PUBLIC fjedit::core ftk::app-ui)
# Its look comes from ftk's theme and kit alone, as every editor UI's does.
ftk_check_editor_ui(fjedit-widget ROOTS ${CMAKE_CURRENT_SOURCE_DIR}/fjedit/widget
                    ICONS ${CMAKE_CURRENT_SOURCE_DIR}/fjedit/widget/icons.hpp)

find_package(Threads REQUIRED)

# fjedit-editor is what a program needs to edit a folder's code, whatever
# window it lays out: the files open with their language servers, the file
# tree, find in files and how the code is shown. fjedit's program is built
# on it, and so is fjell's Code tab.
add_library(fjedit-editor STATIC
    fjedit/editor/code_area.cpp
    fjedit/editor/code_files.cpp
    fjedit/editor/code_status.cpp
    fjedit/editor/file_tree.cpp
    fjedit/editor/find_in_files.cpp
    fjedit/editor/preferences.cpp)
add_library(fjedit::editor ALIAS fjedit-editor)
ftk_target_defaults(fjedit-editor)
target_link_libraries(fjedit-editor PUBLIC fjedit::widget fjedit::lsp ftk::app ftk::app-ui ftk::platform Threads::Threads)
ftk_check_editor_ui(fjedit-editor ROOTS ${CMAKE_CURRENT_SOURCE_DIR}/fjedit/editor
                    ICONS ${CMAKE_CURRENT_SOURCE_DIR}/fjedit/editor/icons.hpp)

# fjedit-program is fjedit as a program: a folder open in a window, its files
# down the side, tabs over the code, menus and a status bar. The executable
# runs it in a window of its own; its tests run it in the kit's harness.
add_library(fjedit-program STATIC
    fjedit/program/preferences_window.cpp
    fjedit/program/program.cpp)
add_library(fjedit::program ALIAS fjedit-program)
ftk_target_defaults(fjedit-program)
target_link_libraries(fjedit-program PUBLIC fjedit::editor fjedit::lsp ftk::app ftk::app-ui ftk::platform Threads::Threads)
ftk_check_editor_ui(fjedit-program ROOTS ${CMAKE_CURRENT_SOURCE_DIR}/fjedit/program
                    ICONS ${CMAKE_CURRENT_SOURCE_DIR}/fjedit/program/icons.hpp)

# fjedit-lsp talks to language servers: each run as a child process and
# talked to through ftk::jsonrpc over its standard streams, as the language
# service the editor asks.
add_library(fjedit-lsp STATIC
    fjedit/lsp/client.cpp
    fjedit/lsp/connection.cpp
    fjedit/lsp/convert.cpp)
add_library(fjedit::lsp ALIAS fjedit-lsp)
ftk_target_defaults(fjedit-lsp)
target_link_libraries(fjedit-lsp PUBLIC fjedit::core ftk::jsonrpc ftk::platform nlohmann_json::nlohmann_json Threads::Threads)
