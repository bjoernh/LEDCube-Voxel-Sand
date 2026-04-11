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

# Eigen is used by the IMU helper (Eigen::Vector3f)
find_package(PkgConfig REQUIRED)
pkg_check_modules(EIGEN3 REQUIRED eigen3)
include_directories(${EIGEN3_INCLUDE_DIRS})
link_directories(${EIGEN3_LIBRARY_DIRS})

set(SANDCUBE_SRC
    src/main.cpp
    src/SandCube.cpp
    src/SandCube.h
    src/VoxelGrid.cpp
    src/SandEngine.cpp
    src/SandSpawner.cpp
    src/KeyboardTilt.cpp
    src/ImuOrientation.cpp
    include/VoxelGrid.h
    include/SandEngine.h
    include/SandSpawner.h
    include/KeyboardTilt.h
    include/OrientationSource.h
    include/ImuOrientation.h
    include/Hash.h
)

add_executable(SandCube ${SANDCUBE_SRC})

target_include_directories(SandCube PRIVATE
    include
    src
)

target_link_libraries(SandCube
    matrixapplication::matrixapplication
    ${Boost_LIBRARIES}
)
