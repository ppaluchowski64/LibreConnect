find_package(fmt REQUIRED)

file(GLOB_RECURSE SOURCE_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.def
)

file(GLOB_RECURSE HEADER_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/*.hpp
)

add_library(virtual-camera-platform-implementation SHARED
        ${SOURCE_FILES}
        ${HEADER_FILES}
)

target_link_libraries(virtual-camera-platform-implementation PRIVATE
        nlohmann_json::nlohmann_json
        fmt::fmt
)

target_include_directories(virtual-camera-platform-implementation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_include_directories(virtual-camera-platform-implementation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../common-inc)
target_include_directories(virtual-camera-platform-implementation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../global-inc)

add_executable(v4l2loopback-helper v4l2loopback-helper.cpp)
set(DEPLOY_FOLDER ${CMAKE_BINARY_DIR})
set_target_properties(v4l2loopback-helper PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${DEPLOY_FOLDER}"
)