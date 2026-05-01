cmake_minimum_required(VERSION 3.20)
project(ChessEngine VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler detection
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# Build type defaults
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Release flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fsanitize=address,undefined")

# x86-only: AVX2 + POPCNT (both the compiler flags and the USE_AVX2 define)
# On ARM/Apple Silicon these blocks are skipped entirely.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
    if(COMPILER_SUPPORTS_AVX2)
        add_compile_options(-mavx2)
        add_compile_definitions(USE_AVX2)
    endif()
    check_cxx_compiler_flag("-mpopcnt" COMPILER_SUPPORTS_POPCNT)
    if(COMPILER_SUPPORTS_POPCNT)
        add_compile_options(-mpopcnt)
    endif()
endif()

# Fathom Syzygy tablebase library (C code — compiled separately)
add_library(fathom STATIC src/fathom/src/tbprobe.c src/fathom/src/tbchess.c)
target_include_directories(fathom PUBLIC src/fathom/src)
set_target_properties(fathom PROPERTIES C_STANDARD 11)

# Source files
set(ENGINE_SOURCES
    src/main.cpp
    src/bitboard.cpp
    src/position.cpp
    src/movegen.cpp
    src/search.cpp
    src/evaluate.cpp
    src/nnue.cpp
    src/tt.cpp
    src/history.cpp
    src/timeman.cpp
    src/book.cpp
    src/uci.cpp
    src/thread.cpp
    src/syzygy.cpp
)

# Main engine executable
add_executable(chess_engine ${ENGINE_SOURCES})
target_include_directories(chess_engine PRIVATE src)
target_link_libraries(chess_engine PRIVATE fathom)

# Enable LTO for release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set_property(TARGET chess_engine PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Threads
find_package(Threads REQUIRED)
target_link_libraries(chess_engine PRIVATE Threads::Threads)

# Perft test executable
add_executable(perft_test
    src/bitboard.cpp
    src/position.cpp
    src/movegen.cpp
    tests/perft.cpp
)
target_include_directories(perft_test PRIVATE src)
target_link_libraries(perft_test PRIVATE Threads::Threads)

# Unit test executable
add_executable(unit_tests
    src/bitboard.cpp
    src/position.cpp
    src/movegen.cpp
    src/evaluate.cpp
    src/nnue.cpp
    src/tt.cpp
    src/history.cpp
    tests/unit_tests.cpp
)
target_include_directories(unit_tests PRIVATE src)
target_link_libraries(unit_tests PRIVATE Threads::Threads)

# Install
install(TARGETS chess_engine DESTINATION bin)
