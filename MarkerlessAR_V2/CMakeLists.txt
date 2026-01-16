cmake_minimum_required(VERSION 2.6)
project(ARProject)

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