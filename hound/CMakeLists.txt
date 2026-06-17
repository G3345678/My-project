cmake_minimum_required(VERSION 3.16)
project(hound
    VERSION 0.1.0
    DESCRIPTION "High-performance HTTP server with coroutine framework"
    LANGUAGES CXX C ASM
)

# ── C++ Standard ──────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Build Options ─────────────────────────────────────────────
option(HOUND_BUILD_TESTS   "Build unit tests"            ON)
option(HOUND_BUILD_EXAMPLES "Build example programs"      ON)
option(HOUND_ENABLE_ASAN    "Enable AddressSanitizer"     OFF)
option(HOUND_ENABLE_TSAN    "Enable ThreadSanitizer"      OFF)
option(HOUND_ENABLE_UBSAN   "Enable UndefinedBehaviorSan" OFF)

# ── Compiler Warnings ────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /wd4100 /wd4127)
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic -Werror
        -Wshadow -Wnon-virtual-dtor -Wold-style-cast
        -Wconversion -Wsign-conversion
        -Wnull-dereference -Wdouble-promotion
        -Wformat=2
    )
endif()

# ── Sanitizers ────────────────────────────────────────────────
if(HOUND_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(HOUND_ENABLE_TSAN)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

if(HOUND_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()

# ── Platform Detection ───────────────────────────────────────
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(HOUND_PLATFORM_LINUX)
    set(HOUND_PLATFORM_LIBS pthread)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_compile_definitions(HOUND_PLATFORM_DARWIN)
elseif(WIN32)
    add_compile_definitions(HOUND_PLATFORM_WINDOWS)
    set(HOUND_PLATFORM_LIBS ws2_32)
endif()

# ── Subdirectories ───────────────────────────────────────────
add_subdirectory(src)

if(HOUND_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(HOUND_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
