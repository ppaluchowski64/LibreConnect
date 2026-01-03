cmake_minimum_required(VERSION 3.20)
project(p_connect LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ENABLE_TESTS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(FetchContent)

include(cmake/Tests.cmake)
include(cmake/Build.cmake)
include(cmake/ENV_Loader.cmake)

load_env_file(.env)

if (ANDROID)
    list(APPEND CMAKE_PREFIX_PATH $ENV{QT_DIR_ANDROID}/lib/cmake)
    set(ANDROID_SDK_ROOT $ENV{ANDROID_SDK_DIR})
    set(ENABLE_TESTS OFF)
elseif (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE))
    list(APPEND CMAKE_PREFIX_PATH $ENV{QT_DIR_DESKTOP}/lib/cmake)
endif()

find_package(OpenSSL         REQUIRED)
find_package(asio            REQUIRED)
find_package(magic_enum      REQUIRED)
find_package(fmt             REQUIRED)
find_package(concurrentqueue REQUIRED)
find_package(GTest           REQUIRED)
find_package(benchmark       REQUIRED)
find_package(SQLiteCpp       REQUIRED)
find_package(nlohmann_json   REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml)

FetchContent_Declare(
        debug-log
        GIT_REPOSITORY https://github.com/ddj4747/Debug-Log.git
        GIT_TAG main
)

FetchContent_MakeAvailable(debug-log)
qt_standard_project_setup()

add_library(project_defaults INTERFACE)

BuildStaticLibrary(network-component-pc utilities/network project_defaults openssl::openssl asio::asio Debug-Log concurrentqueue::concurrentqueue magic_enum::magic_enum)
BuildStaticLibrary(system-component-pc utilities/system project_defaults openssl::openssl asio::asio Debug-Log concurrentqueue::concurrentqueue magic_enum::magic_enum SQLiteCpp)
BuildStaticLibrary(p2p-network-component-pc utilities/p2p-network project_defaults openssl::openssl asio::asio Debug-Log network-component-pc system-component-pc magic_enum::magic_enum concurrentqueue::concurrentqueue nlohmann_json::nlohmann_json Qt6::Core)
BuildStaticLibrary(file-entry utilities/file-system project_defaults system-component-pc)

add_subdirectory("utilities/virtual-camera")

if (ANDROID)
    add_subdirectory("apps/mobile")
elseif (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE))
    add_subdirectory("apps/desktop")
endif()

if (ENABLE_TESTS)
    BuildTests(Concurrent_Structures_Test concurrent-structures network-component-pc system-component-pc)
    BuildTests(Asio_Abstractions_Package_Test asio-abstractions/package network-component-pc system-component-pc)
    BuildTestProgram(DeviceType_Test device-type/DetectDeviceType.cpp system-component-pc)
    BuildTestProgram(FileEntryTest file-system/FileEntryTest.cpp file-entry)
    BuildTestProgram(VirtualCameraTest virtual-camera/VirtualCameraTest.cpp virtual-camera Debug-Log)

    add_subdirectory("tests/asio-abstractions-programs/MulticastConnection")
    LinkVirtualCameraLibs(VirtualCameraTest)
endif()


if (ANDROID)
elseif(APPLE AND NOT IOS)
elseif (UNIX AND NOT APPLE)
    add_custom_target(copy_scripts ALL
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/scripts/linux
            ${CMAKE_BINARY_DIR}/scripts
    )
elseif (WIN32)
endif()