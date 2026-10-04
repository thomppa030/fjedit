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
    test_history.cpp
    test_keymap.cpp
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
    test_workspace.cpp)
ftk_target_defaults(fjedit-tests)
target_link_libraries(fjedit-tests PRIVATE fjedit::core Catch2::Catch2WithMain)

# The code view, driven as a person drives it through ftk's harness: clicks,
# keys and text into the UI core with no window.
add_executable(fjedit-widget-tests
    log_for_tests.cpp
    test_code_view.cpp)
ftk_target_defaults(fjedit-widget-tests)
target_link_libraries(fjedit-widget-tests PRIVATE fjedit::widget ftk::kit-harness Catch2::Catch2WithMain)

# The program, on folders of the tests' own, through the same harness.
add_executable(fjedit-program-tests
    log_for_tests.cpp
    test_program.cpp)
ftk_target_defaults(fjedit-program-tests)
target_link_libraries(fjedit-program-tests PRIVATE fjedit::program ftk::kit-harness Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(fjedit-tests)
catch_discover_tests(fjedit-widget-tests TEST_PREFIX "view: ")
catch_discover_tests(fjedit-program-tests TEST_PREFIX "program: ")
