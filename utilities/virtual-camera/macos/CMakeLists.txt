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