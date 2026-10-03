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
    test_buffer.cpp
    test_commands.cpp
    test_edit.cpp
    test_editing.cpp
    test_find.cpp
    test_history.cpp
    test_keymap.cpp
    test_motion.cpp
    test_select.cpp
    test_selections.cpp)
ftk_target_defaults(fjedit-tests)
target_link_libraries(fjedit-tests PRIVATE fjedit::core Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(fjedit-tests)
