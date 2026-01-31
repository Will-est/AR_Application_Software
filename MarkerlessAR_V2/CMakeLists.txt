cmake_minimum_required(VERSION 2.6)
project(ARProject)

# Enable debug symbols
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")

# Set path to custom OpenCV build with OpenGL support
# set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../opencv/build" CACHE PATH "Path to OpenCV build directory")
set(OpenCV_DIR "/opt/opencv/build" CACHE PATH "Path to OpenCV build directory")


find_package(OpenCV REQUIRED )
find_package(OpenGL REQUIRED )
find_package(GLEW REQUIRED )

include_directories(${OpenCV_INCLUDE_DIR})
include_directories(${OpenGL_INCLUDE_DIR})
include_directories(${GLEW_INCLUDE_DIRS})

link_directories(${OpenCV_LIBRARY_DIR})
link_directories(${OpenGL_LIBRARY_DIR})

include_directories(${ARProject_SOURCE_DIR}/src)
add_subdirectory(src)

# To install, run: make install
# Automatic install removed to prevent install running before build completes