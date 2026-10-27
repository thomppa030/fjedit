# fjedit's unit tests (fjedit-tests): everything that runs without a window.

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.0
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(Catch2)
# Whoever fetched it first, its source directory is read here.
FetchContent_GetProperties(Catch2)

add_executable(fjedit-tests
    log_for_tests.cpp
    test_buffer.cpp
    test_commands.cpp
    test_document.cpp
    test_edit.cpp
    test_editing.cpp
    test_file_search.cpp
    test_find.cpp
    test_folds.cpp
    test_git_ignore.cpp
    test_history.cpp
    test_jump_list.cpp
    test_keymap.cpp
    test_language_services.cpp
    test_lex_cpp.cpp
    test_lex_fjss.cpp
    test_lex_glsl.cpp
    test_lex_json.cpp
    test_lex_markup.cpp
    test_motion.cpp
    test_select.cpp
    test_selections.cpp
    test_structure.cpp
    test_syntax.cpp
    test_vim_engine.cpp
    test_vim_ex.cpp
    test_vim_keys_file.cpp
    test_vim_maps.cpp
    test_vim_motions.cpp
    test_vim_pattern.cpp
    test_vim_registers.cpp
    test_vim_text_objects.cpp
    test_workspace.cpp)
ftk_target_defaults(fjedit-tests)
ftk_leak_suppressions(fjedit-tests)
target_link_libraries(fjedit-tests PRIVATE fjedit::core Catch2::Catch2WithMain)

# The code view, driven as a person drives it through ftk's harness: clicks,
# keys and text into the UI core with no window.
add_executable(fjedit-widget-tests
    log_for_tests.cpp
    test_code_view.cpp
    test_file_kind.cpp
    test_markdown.cpp)
ftk_target_defaults(fjedit-widget-tests)
ftk_leak_suppressions(fjedit-widget-tests)
target_link_libraries(fjedit-widget-tests PRIVATE fjedit::widget ftk::kit-harness Catch2::Catch2WithMain)

# What a program edits a folder's code with, on folders of the tests' own,
# through the same harness.
add_executable(fjedit-editor-tests
    log_for_tests.cpp
    test_code_area.cpp
    test_code_files.cpp
    test_file_tree.cpp
    test_find_in_files.cpp
    test_preferences.cpp)
ftk_target_defaults(fjedit-editor-tests)
ftk_leak_suppressions(fjedit-editor-tests)
# The code area draws fjedit's mark through ftk's Vulkan backend, which the
# tests never call: the loader is loaded at the first call, so they run where
# there is none, a CI runner among them.
ftk_delay_load_vulkan(fjedit-editor-tests)
target_link_libraries(fjedit-editor-tests PRIVATE fjedit::editor ftk::kit-harness Catch2::Catch2WithMain)

# The program, on folders of the tests' own, through the same harness.
add_executable(fjedit-program-tests
    log_for_tests.cpp
    test_program.cpp)
ftk_target_defaults(fjedit-program-tests)
ftk_leak_suppressions(fjedit-program-tests)
# The program draws its pictures through ftk's Vulkan backend, which the
# tests never call: the loader is loaded at the first call, so they run where
# there is none, a CI runner among them.
ftk_delay_load_vulkan(fjedit-program-tests)
target_link_libraries(fjedit-program-tests PRIVATE fjedit::program ftk::kit-harness Catch2::Catch2WithMain)
# fjedit's brand files, read where they are in the source.
target_compile_definitions(fjedit-program-tests PRIVATE FJEDIT_BRAND="${CMAKE_CURRENT_SOURCE_DIR}/../brand")

# The language server connection, against a fake server of the tests' own,
# and clangd when it is installed.
add_executable(fjedit-fake-server
    fake_server.cpp)
ftk_target_defaults(fjedit-fake-server)
target_link_libraries(fjedit-fake-server PRIVATE ftk::jsonrpc ftk::platform)
add_executable(fjedit-lsp-tests
    log_for_tests.cpp
    test_lsp.cpp
    test_lsp_client.cpp
    test_lsp_convert.cpp
    test_lsp_fjls.cpp)
ftk_target_defaults(fjedit-lsp-tests)
ftk_leak_suppressions(fjedit-lsp-tests)
target_link_libraries(fjedit-lsp-tests PRIVATE fjedit::lsp Catch2::Catch2WithMain)
add_dependencies(fjedit-lsp-tests fjedit-fake-server)
target_compile_definitions(fjedit-lsp-tests PRIVATE FJEDIT_FAKE_SERVER="$<TARGET_FILE:fjedit-fake-server>")

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(fjedit-tests)
catch_discover_tests(fjedit-widget-tests TEST_PREFIX "view: ")
catch_discover_tests(fjedit-editor-tests TEST_PREFIX "editor: ")
catch_discover_tests(fjedit-program-tests TEST_PREFIX "program: ")
catch_discover_tests(fjedit-lsp-tests TEST_PREFIX "lsp: ")
