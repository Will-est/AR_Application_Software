add_executable(ARProject ARDrawingContext.cpp
ARDrawingContext.hpp
CameraCalibration.cpp
CameraCalibration.hpp
GeometryTypes.cpp
GeometryTypes.hpp
main.cpp
ARPipeline.hpp
ARPipeline.cpp
Pattern.cpp
Pattern.hpp
PatternDetector.cpp
PatternDetector.hpp
DebugHelpers.hpp
objloader.cpp
objloader.hpp
texture.cpp
texture.hpp
)

set_target_properties(ARProject PROPERTIES OUTPUT_NAME "ARProject.out")

# Enable debug visualization
target_compile_definitions(ARProject PRIVATE _DEBUG=1)

target_link_libraries( ARProject ${OpenCV_LIBRARIES} )
target_link_libraries( ARProject ${OPENGL_LIBRARIES} )
target_link_libraries( ARProject GLEW::GLEW )
     
# installs the executable to the src folder in the project directory
install(TARGETS ARProject DESTINATION ${ARProject_SOURCE_DIR}/src)
