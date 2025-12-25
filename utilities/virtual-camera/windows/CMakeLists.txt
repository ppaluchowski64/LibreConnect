find_package(wil REQUIRED)

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
        WIL::WIL
        windowsapp
        Mfplat.lib
        Mf.lib
        Mfuuid.lib
        Uuid.lib
        Ole32.lib
        Kernel32.lib
        Mfsensorgroup.lib
)

target_include_directories(virtual-camera-platform-implementation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc/priv)
target_include_directories(virtual-camera-platform-implementation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc/pub)