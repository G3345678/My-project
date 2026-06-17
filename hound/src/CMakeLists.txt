# ── Collect Sources ──────────────────────────────────────────
file(GLOB_RECURSE HOUND_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/*.S
    ${CMAKE_CURRENT_SOURCE_DIR}/*.asm
)

# ── Hound Library (static) ───────────────────────────────────
add_library(hound STATIC ${HOUND_SOURCES})
target_include_directories(hound PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Platform-specific libraries
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(hound PUBLIC pthread)
elseif(WIN32)
    target_link_libraries(hound PUBLIC ws2_32)
endif()

# Compiler-specific settings
if(MSVC)
    target_compile_definitions(hound PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
