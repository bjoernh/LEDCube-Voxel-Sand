cmake_minimum_required(VERSION 3.7)
project(SandCube VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# libmatrixapplication from the matrixserver project
find_package(matrixapplication 0.4 REQUIRED)

# Boost is required by matrixapplication
find_package(Boost 1.58.0 REQUIRED COMPONENTS thread log)
include_directories(${Boost_INCLUDE_DIR})

# Protobuf is required by matrixapplication
find_package(Protobuf REQUIRED)

# Ensure Homebrew lib path is on the linker search path (needed for Imlib2
# which matrixapplication links as a bare -lImlib2 without an -L prefix)
#if(APPLE)
#    link_directories(/opt/homebrew/lib)
#endif()

# Eigen is used by the IMU helper and the fluid engine
find_package(PkgConfig REQUIRED)
pkg_check_modules(EIGEN3 REQUIRED eigen3)
include_directories(${EIGEN3_INCLUDE_DIRS})
link_directories(${EIGEN3_LIBRARY_DIRS})

# ── Sources shared between both executables ───────────────────────────────────
set(SHARED_SRC
    src/VoxelGrid.cpp
    src/KeyboardTilt.cpp
    src/ImuOrientation.cpp
    include/VoxelGrid.h
    include/Gravity.h
    include/KeyboardTilt.h
    include/OrientationSource.h
    include/ImuOrientation.h
    include/Hash.h
)

# ── SandCube ──────────────────────────────────────────────────────────────────
set(SANDCUBE_SRC
    src/main.cpp
    src/SandCube.cpp
    src/SandCube.h
    src/SandEngine.cpp
    src/SandSpawner.cpp
    include/SandEngine.h
    include/SandSpawner.h
)

add_executable(SandCube ${SANDCUBE_SRC} ${SHARED_SRC})

target_include_directories(SandCube PRIVATE include src)

target_link_libraries(SandCube
    matrixapplication::matrixapplication
    ${Boost_LIBRARIES}
)

target_compile_options(SandCube PRIVATE -Wall -Wextra -Wpedantic)

# ── WaterCube ─────────────────────────────────────────────────────────────────
set(WATERCUBE_SRC
    src/water_main.cpp
    src/WaterCube.cpp
    src/WaterCube.h
    src/FluidEngine.cpp
    include/FluidEngine.h
)

add_executable(WaterCube ${WATERCUBE_SRC} ${SHARED_SRC})

target_include_directories(WaterCube PRIVATE include src)

target_link_libraries(WaterCube
    matrixapplication::matrixapplication
    ${Boost_LIBRARIES}
)

target_compile_options(WaterCube PRIVATE
    -Wall -Wextra -Wpedantic
    -O3 -march=native -ffast-math -funroll-loops -DNDEBUG
)
