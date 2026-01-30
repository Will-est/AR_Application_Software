cmake_minimum_required(VERSION 2.6)
project(ARProject)

# Set path to custom OpenCV build with OpenGL support
set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../opencv/build" CACHE PATH "Path to OpenCV build directory")
# set(OpenCV_DIR "/opt/opencv/build" CACHE PATH "Path to OpenCV build directory")


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

# Automatically run install after build
add_custom_target(install_after_build ALL
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR}
    COMMENT "Installing ARProject after build"
)