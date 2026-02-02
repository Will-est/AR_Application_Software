cmake_minimum_required(VERSION 2.6)
project(ARProject)

# Enable debug symbols
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")

# Set RPATH so executable can find OpenCV libraries at runtime
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/../opencv/build/lib")

# Set path to custom OpenCV build with OpenGL support
set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../opencv/build" CACHE PATH "Path to OpenCV build directory")
# set(OpenCV_DIR "/opt/opencv/build" CACHE PATH "Path to OpenCV build directory")
# set(OpenCV_DIR "/opt/opencv-3.4-opengl/share/OpenCV" CACHE PATH "Path to OpenCV build directory" FORCE)

find_package(OpenCV REQUIRED )
find_package(OpenGL REQUIRED )
find_package(GLEW REQUIRED )

include_directories(${OpenCV_INCLUDE_DIR})
include_directories(${OpenGL_INCLUDE_DIR})
include_directories(${GLEW_INCLUDE_DIRS})

link_directories(${OpenCV_LIBRARY_DIR})
link_directories(${OpenGL_LIBRARY_DIR})
link_directories(${GLEW_LIBRARY_DIR})
include_directories(${ARProject_SOURCE_DIR}/src)
add_subdirectory(src)

# To install, run: make install
# Automatic install removed to prevent install running before build completes