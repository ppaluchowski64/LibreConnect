file(GLOB_RECURSE COMMON_SOURCE_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc
)

file(GLOB_RECURSE COMMON_HEADER_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/*.hpp
)

if (WIN32)
    file(GLOB_RECURSE SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/src-win/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src-win/*.cxx
            ${CMAKE_CURRENT_SOURCE_DIR}/src-win/*.cc
    )

    file(GLOB_RECURSE HEADER_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/inc-win/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/inc-win/*.hpp
    )
endif()


add_library(virtual-camera SHARED
        ${SOURCE_FILES}
        ${HEADER_FILES}
        ${COMMON_SOURCE_FILES}
        ${COMMON_HEADER_FILES}
)

target_include_directories(virtual-camera PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/
)

if (WIN32)
    find_package(wil REQUIRED)

    target_include_directories(virtual-camera PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/inc-win/
    )

    target_link_libraries(virtual-camera PRIVATE WIL::WIL)
endif()