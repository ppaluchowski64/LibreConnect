cmake_minimum_required(VERSION 3.20)
project(p_connect LANGUAGES CXX C)
enable_language(ASM)

if (APPLE AND NOT IOS)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ENABLE_TESTS ON)
set(ENABLE_MOBILE_TEST_PROGRAMS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(FetchContent)

include(cmake/Tests.cmake)
include(cmake/Build.cmake)
include(cmake/ENV_Loader.cmake)

load_env_file(.env)

set(BUILD_TESTS_ENABLED ON)
if(DEFINED ENV{BUILD_TESTS})
    string(TOLOWER "$ENV{BUILD_TESTS}" BUILD_TESTS_ENV)
    if(BUILD_TESTS_ENV STREQUAL "1" OR BUILD_TESTS_ENV STREQUAL "true" OR BUILD_TESTS_ENV STREQUAL "yes" OR BUILD_TESTS_ENV STREQUAL "on")
        set(BUILD_TESTS_ENABLED ON)
    else()
        set(BUILD_TESTS_ENABLED OFF)
    endif()
endif()

set(ENABLE_TESTS ${BUILD_TESTS_ENABLED})

if (ANDROID)
    list(APPEND CMAKE_PREFIX_PATH $ENV{QT_DIR_ANDROID}/lib/cmake)
    set(ANDROID_SDK_ROOT $ENV{ANDROID_SDK_DIR})
    set(ENABLE_TESTS OFF)
    set(ENABLE_MOBILE_TEST_PROGRAMS ${BUILD_TESTS_ENABLED})
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

    add_library(ffmpeg_headers INTERFACE)
    target_include_directories(ffmpeg_headers INTERFACE ${CMAKE_SOURCE_DIR}/build/ffmpeg/include/)
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
find_package(Boost            REQUIRED)

if (UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Multimedia DBus)
else ()
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Multimedia)
endif ()

if ((WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE)) AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set(DEBUG_LOG_DISABLE_STACKTRACE ON CACHE BOOL "" FORCE)
endif ()

if(ANDROID OR IOS)
    set(DEBUG_LOG_DISABLE_STACKTRACE ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
        debug-log
        GIT_REPOSITORY https://github.com/ddj4747/Debug-Log.git
        GIT_TAG main
)

FetchContent_MakeAvailable(debug-log)

if (WIN32)
    FetchContent_Declare(
            wintoast
            GIT_REPOSITORY https://github.com/mohabouje/WinToast.git
            GIT_TAG master
    )

    FetchContent_MakeAvailable(wintoast)
endif ()

qt_standard_project_setup()

add_library(project_defaults)

if (IOS)
    target_compile_definitions(project_defaults PUBLIC IOS_DEVICE)
endif ()

if (ANDROID)
    target_compile_definitions(project_defaults PUBLIC ANDROID_DEVICE)
endif ()

if(ANDROID OR IOS)
    target_compile_definitions(project_defaults PUBLIC MOBILE_DEVICE)
else()
    target_compile_definitions(project_defaults PUBLIC DESKTOP_DEVICE)
endif()

if(WIN32)
    target_compile_definitions(project_defaults PUBLIC _WIN32_WINNT=0x0601)
endif()

BuildStaticLibrary(network-component-pc utilities/network project_defaults openssl::openssl asio::asio Debug-Log concurrentqueue::concurrentqueue magic_enum::magic_enum boost::boost)
BuildStaticLibrary(system-component-pc utilities/system project_defaults openssl::openssl asio::asio Debug-Log concurrentqueue::concurrentqueue magic_enum::magic_enum boost::boost)
BuildStaticLibrary(p2p-network-component-pc utilities/p2p-network project_defaults openssl::openssl asio::asio Debug-Log network-component-pc system-component-pc magic_enum::magic_enum concurrentqueue::concurrentqueue nlohmann_json::nlohmann_json Qt6::Core boost::boost)
BuildStaticLibrary(file-system utilities/file-system project_defaults system-component-pc Qt6::Core Qt::Gui)
BuildStaticLibrary(srtp-stream utilities/srtp-stream project_defaults Debug-Log Boost::boost network-component-pc system-component-pc libsrtp::libsrtp)
BuildStaticLibrary(permission-manager utilities/permission-manager project_defaults Debug-Log Qt6::Core asio::asio Qt6::CorePrivate)

add_subdirectory("utilities/virtual-camera")
add_subdirectory("utilities/notifications-handler")

BuildStaticLibrary(common-module modules/common Debug-Log Qt6::Core Qt6::Multimedia asio::asio p2p-network-component-pc permission-manager)

if (ANDROID OR IOS)

    BuildApplicationModule(network-camera-module modules/network-camera/common modules/network-camera/mobile
            common-module
            srtp-stream
            ffmpeg_headers
            virtual-camera
    )

    BuildApplicationModule(file-share-module modules/file-share/common modules/file-share/mobile
            common-module
            file-system
            xxHash::xxhash
    )

    BuildApplicationModule(notification-sync-module modules/notification-sync/common modules/notification-sync/mobile
            common-module
            notifications-handler
    )

    BuildStaticLibrary(modules-manager modules/modules-manager
            network-component-pc
            system-component-pc
            p2p-network-component-pc
            common-module
            Debug-Log
            Qt6::Core

            network-camera-module
            file-share-module
            notification-sync-module
    )

elseif (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE))
    BuildApplicationModule(network-camera-module modules/network-camera/common modules/network-camera/desktop
            common-module
            srtp-stream
            ffmpeg_headers
            virtual-camera
    )

    BuildApplicationModule(file-share-module modules/file-share/common modules/file-share/desktop
            common-module
            file-system
            xxHash::xxhash
    )

    BuildApplicationModule(notification-sync-module modules/notification-sync/common modules/notification-sync/desktop
            common-module
            notifications-handler
    )

    BuildStaticLibrary(modules-manager modules/modules-manager
            network-component-pc
            system-component-pc
            p2p-network-component-pc
            common-module
            Debug-Log
            Qt6::Core

            network-camera-module
            file-share-module
            notification-sync-module
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

    BuildTestProgram(NotificationEmitterTest-emitter notification-emitter-test/emitter.cpp notifications-handler Debug-Log)
    if (APPLE AND NOT IOS)
        target_link_libraries(NotificationEmitterTest-emitter PRIVATE Qt6::Core)
        set_target_properties(NotificationEmitterTest-emitter PROPERTIES
                MACOSX_BUNDLE TRUE
                MACOSX_BUNDLE_GUI_IDENTIFIER "com.libreconnect.notificationemittertest"
                MACOSX_BUNDLE_BUNDLE_NAME "NotificationEmitterTest"
        )
        DeployQT(NotificationEmitterTest-emitter)
    endif ()

    target_sources(FileEntryTest PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/file-system/OverloadedStreams.cpp
    )
    target_sources(FileSystemTest PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/file-system/OverloadedStreams.cpp
    )

    add_subdirectory("tests/asio-abstractions-programs/MulticastConnection")
    add_subdirectory("tests/network-camera-module/utilities-test")
    add_subdirectory("tests/test-apps/desktop")

    LinkVirtualCameraLibs(VirtualCameraTest)
endif()

if (ENABLE_MOBILE_TEST_PROGRAMS)
    add_subdirectory("tests/network-camera-module/utilities-test-mobile")
    add_subdirectory("tests/notification-capture-test")
    add_subdirectory("tests/test-apps/mobile")
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
