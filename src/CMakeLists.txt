# fjedit-core is the editor without a window: the text and how it is edited.
# It needs no UI, so its tests run headless.
add_library(fjedit-core STATIC
    fjedit/core/buffer.cpp
    fjedit/core/commands.cpp
    fjedit/core/history.cpp
    fjedit/core/layout.cpp
    fjedit/core/motion.cpp
    fjedit/core/selections.cpp)
add_library(fjedit::core ALIAS fjedit-core)
ftk_target_defaults(fjedit-core)
target_include_directories(fjedit-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fjedit-core PUBLIC ftk::base)
