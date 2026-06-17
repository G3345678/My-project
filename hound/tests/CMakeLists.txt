# ── Google Test ───────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

# ── Test Sources ─────────────────────────────────────────────
file(GLOB_RECURSE TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# ── Test Executable ──────────────────────────────────────────
add_executable(hound_tests ${TEST_SOURCES})
target_link_libraries(hound_tests PRIVATE hound gtest gtest_main)
target_include_directories(hound_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Google Test requires at least C++14
target_compile_features(hound_tests PRIVATE cxx_std_17)

# Register with CTest
include(GoogleTest)
gtest_discover_tests(hound_tests)
