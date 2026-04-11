cmake_minimum_required(VERSION 3.7)
project(LEDCube-App-Template VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# This template expects matrixapplication to be installed on the system (or in the provided prefix path)
find_package(matrixapplication 0.4 REQUIRED)

# Boost is a common dependency for LEDCube apps based on libmatrixapplication
find_package(Boost 1.58.0 REQUIRED COMPONENTS thread log)
INCLUDE_DIRECTORIES( ${Boost_INCLUDE_DIR} )

# Protobuf is required by matrixapplication
find_package(Protobuf REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(EIGEN3 REQUIRED eigen3)
pkg_check_modules(IMLIB2 REQUIRED imlib2)
include_directories(${EIGEN3_INCLUDE_DIRS} ${IMLIB2_INCLUDE_DIRS})
link_directories(${EIGEN3_LIBRARY_DIRS} ${IMLIB2_LIBRARY_DIRS})

set(MAINSRC
    src/main.cpp 
    src/AppTemplate.cpp 
    src/AppTemplate.h
)

set(MAINLIBS
    matrixapplication::matrixapplication
)

add_executable(AppTemplate ${MAINSRC})
target_link_libraries(AppTemplate ${MAINLIBS} ${Boost_LIBRARIES})
