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
    set(ENABLE_MOBILE_TEST_PROGRAMS ON)
elseif (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE))
    list(APPEND CMAKE_PREFIX_PATH $ENV{QT_DIR_DESKTOP}/lib/cmake)
    set(ENABLE_MOBILE_TEST_PROGRAMS OFF)
endif()


if(ANDROID)
    find_package(ffmpeg REQUIRED)
    add_library(ffmpeg_headers ALIAS ffmpeg::ffmpeg)
elseif (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE))
    find_package(GTest     REQUIRED)
    find_package(benchmark REQUIRED)

    file(GLOB_RECURSE FFMPEG_HEADER_FILES
            ${CMAKE_SOURCE_DIR}/build/ffmpeg/include
    )

    add_library(ffmpeg_headers ${FFMPEG_HEADER_FILES})
    target_include_directories(ffmpeg_headers PUBLIC ${CMAKE_SOURCE_DIR}/build/ffmpeg/include/)
endif()

find_package(OpenSSL          REQUIRED)
find_package(asio             REQUIRED)
find_package(Boost            REQUIRED)
find_package(magic_enum       REQUIRED)
find_package(fmt              REQUIRED)
find_package(concurrentqueue  REQUIRED)
find_package(nlohmann_json    REQUIRED)
find_package(libsrtp          REQUIRED)
find_package(xxHash           REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Multimedia)

FetchContent_Declare(
        debug-log
        GIT_REPOSITORY https://github.com/ddj4747/Debug-Log.git
        GIT_TAG main
)

FetchContent_MakeAvailable(debug-log)
qt_standard_project_setup()

add_library(project_defaults)
if(ANDROID OR IOS)
    target_compile_definitions(project_defaults PUBLIC MOBILE_DEVICE)
else()
    target_compile_definitions(project_defaults PUBLIC DESKTOP_DEVICE)
endif()

if(WIN32)
    target_compile_definitions(project_defaults PUBLIC _WIN32_WINNT=0x0601)
endif()

BuildStaticLibrary(network-component-pc utilities/network project_defaults openssl::openssl asio::asio Debug-Log concurrentqueue::concurrentqueue magic_enum::magic_enum)
BuildStaticLibrary(system-component-pc utilities/system project_defaults openssl::openssl asio::asio Debug-Log concurrentqueue::concurrentqueue magic_enum::magic_enum)
BuildStaticLibrary(p2p-network-component-pc utilities/p2p-network project_defaults openssl::openssl asio::asio Debug-Log network-component-pc system-component-pc magic_enum::magic_enum concurrentqueue::concurrentqueue nlohmann_json::nlohmann_json Qt6::Core)
BuildStaticLibrary(file-system utilities/file-system project_defaults system-component-pc Qt6::Core Qt::Gui)
BuildStaticLibrary(srtp-stream utilities/srtp-stream project_defaults Debug-Log Boost::boost network-component-pc system-component-pc libsrtp::libsrtp)

add_subdirectory("utilities/virtual-camera")
add_subdirectory("utilities/notifications-capture")

BuildStaticLibrary(common-module modules/common Debug-Log Qt6::Core Qt6::Multimedia asio::asio)

if (ANDROID OR IOS)

    BuildApplicationModule(network-camera-module modules/network-camera/common modules/network-camera/mobile
            common-module
            srtp-stream
            p2p-network-component-pc
            ffmpeg_headers
            virtual-camera
    )

    BuildApplicationModule(file-share-module modules/file-share/common modules/file-share/mobile
            common-module
            file-system
            xxHash::xxhash
            p2p-network-component-pc
    )

elseif (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE))


    BuildApplicationModule(network-camera-module modules/network-camera/common modules/network-camera/desktop
            common-module
            srtp-stream
            p2p-network-component-pc
            ffmpeg_headers
            virtual-camera
    )

    BuildApplicationModule(file-share-module modules/file-share/common modules/file-share/desktop
            common-module
            file-system
            xxHash::xxhash
            p2p-network-component-pc
    )

endif ()


if (ANDROID)
    add_subdirectory("apps/mobile")
elseif (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE))
    add_subdirectory("apps/desktop")
endif()

if (ENABLE_TESTS)
    BuildTests(Concurrent_Structures_Test concurrent-structures network-component-pc system-component-pc)
    BuildTests(Asio_Abstractions_Package_Test asio-abstractions/package network-component-pc system-component-pc)
    BuildTestProgram(DeviceType_Test device-type/DetectDeviceType.cpp system-component-pc)
    BuildTestProgram(VirtualCameraTest virtual-camera/VirtualCameraTest.cpp virtual-camera Debug-Log)
    BuildTestProgram(FileEntryTest file-system/FileEntryTest.cpp file-system)
    BuildTestProgram(FileSystemTest file-system/FileSystemTest.cpp file-system)
    BuildTestProgram(TransferFileChannelTest file-share-module/TransferFileChannelTest.cpp file-share-module)
    BuildTestProgram(TransferDirectoryChannelTest file-share-module/TransferDirectoryChannelTest.cpp file-share-module)

    target_sources(FileEntryTest PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/file-system/OverloadedStreams.cpp
    )
    target_sources(FileSystemTest PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/file-system/OverloadedStreams.cpp
    )

    add_subdirectory("tests/asio-abstractions-programs/MulticastConnection")
    add_subdirectory("tests/network-camera-module/utilities-test")

    LinkVirtualCameraLibs(VirtualCameraTest)
endif()

if (ENABLE_MOBILE_TEST_PROGRAMS)
    add_subdirectory("tests/network-camera-module/utilities-test-mobile")
endif ()

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
