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

list(APPEND CMAKE_PREFIX_PATH $ENV{QT_DIR}/lib/cmake)

find_package(OpenSSL         REQUIRED)
find_package(asio            REQUIRED)
find_package(magic_enum      REQUIRED)
find_package(Tracy           REQUIRED)
find_package(concurrentqueue REQUIRED)
find_package(GTest           REQUIRED)
find_package(benchmark       REQUIRED)
find_package(SQLiteCpp       REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Quick Qml)

FetchContent_Declare(
        debug-log
        GIT_REPOSITORY https://github.com/ddj4747/Debug-Log.git
        GIT_TAG main
)

FetchContent_MakeAvailable(debug-log)
qt_standard_project_setup()

add_library(project_defaults INTERFACE)

target_compile_definitions(project_defaults INTERFACE
        $<$<CONFIG:Debug>:TRACY_ENABLE>
)

BuildStaticLibrary(network-component-pc utilities/network project_defaults openssl::openssl asio::asio Debug-Log Tracy::TracyClient concurrentqueue::concurrentqueue magic_enum::magic_enum)
BuildStaticLibrary(system-component-pc utilities/system project_defaults openssl::openssl asio::asio Debug-Log Tracy::TracyClient concurrentqueue::concurrentqueue magic_enum::magic_enum SQLiteCpp)
BuildStaticLibrary(p2p-network-component-pc utilities/p2p-network project_defaults openssl::openssl asio::asio Debug-Log Tracy::TracyClient network-component-pc system-component-pc magic_enum::magic_enum concurrentqueue::concurrentqueue Qt6::Core)
BuildStaticLibrary(file-entry utilities/file-system project_defaults system-component-pc)

if (ENABLE_TESTS)
    BuildTests(Concurrent_Structures_Test concurrent-structures network-component-pc system-component-pc)
    BuildTests(Asio_Abstractions_Package_Test asio-abstractions/package network-component-pc system-component-pc)
    BuildTestProgram(DeviceType_Test device-type/DetectDeviceType.cpp system-component-pc)
    BuildTestProgram(FileEntryTest file-system/FileEntryTest.cpp file-entry)
    add_subdirectory("tests/asio-abstractions-programs/MulticastConnection")
endif()

add_subdirectory("apps/desktop")
