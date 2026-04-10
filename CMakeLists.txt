cmake_minimum_required(VERSION 3.16)
project(SandCube VERSION 1.0.0 LANGUAGES CXX)

# ── Language standard ────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ── Build type defaults to Release ──────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Compiler flags ──────────────────────────────────────────────────────────
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    "$<$<CONFIG:Release>:-O3>"
    "$<$<CONFIG:Release>:-march=native>"   # ARMv8 on RPi 4
    "$<$<CONFIG:Debug>:-Og>"
    "$<$<CONFIG:Debug>:-g3>"
)

# ── rpi-rgb-led-matrix ───────────────────────────────────────────────────────
#
#   Expected layout (clone the library next to this repo):
#
#     ├── rpi-rgb-led-matrix/      ← hzeller/rpi-rgb-led-matrix
#     │     ├── include/
#     │     │     └── led-matrix.h
#     │     └── lib/
#     │           └── librgbmatrix.a   (built with "make -C lib")
#     └── sand_cube/               ← this project
#          └── CMakeLists.txt
#
#   Override the path with:
#     cmake -DRGBMATRIX_DIR=/absolute/path/to/rpi-rgb-led-matrix ..

set(RGBMATRIX_DIR
    "${CMAKE_SOURCE_DIR}/../rpi-rgb-led-matrix"
    CACHE PATH "Root directory of the hzeller/rpi-rgb-led-matrix library")

# Validate that the library exists before proceeding.
if(NOT EXISTS "${RGBMATRIX_DIR}/lib/librgbmatrix.a")
    message(FATAL_ERROR
        "librgbmatrix.a not found at '${RGBMATRIX_DIR}/lib/librgbmatrix.a'.\n"
        "Build it first:\n"
        "  cd ${RGBMATRIX_DIR} && make -C lib\n"
        "Then re-run cmake, or set -DRGBMATRIX_DIR=<path>.")
endif()

add_library(rgbmatrix STATIC IMPORTED GLOBAL)
set_target_properties(rgbmatrix PROPERTIES
    IMPORTED_LOCATION             "${RGBMATRIX_DIR}/lib/librgbmatrix.a"
    INTERFACE_INCLUDE_DIRECTORIES "${RGBMATRIX_DIR}/include"
)

# ── SandCube executable ──────────────────────────────────────────────────────
add_executable(sand_cube
    src/VoxelGrid.cpp
    src/SandEngine.cpp
    src/SandSpawner.cpp
    src/FaceMapper.cpp
    src/main.cpp
)

target_include_directories(sand_cube PRIVATE include)

target_link_libraries(sand_cube PRIVATE
    rgbmatrix
    pthread     # librgbmatrix uses pthreads internally
    rt          # clock_nanosleep / POSIX realtime
    m           # libm (used by some BSP GPIO helpers)
)

# ── Optional: install target ─────────────────────────────────────────────────
install(TARGETS sand_cube RUNTIME DESTINATION bin)

# ── Print configuration summary ─────────────────────────────────────────────
message(STATUS "=================================================")
message(STATUS " SandCube build configuration")
message(STATUS "   Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "   C++ standard : C++${CMAKE_CXX_STANDARD}")
message(STATUS "   rgbmatrix    : ${RGBMATRIX_DIR}")
message(STATUS "=================================================")
