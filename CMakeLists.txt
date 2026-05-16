cmake_minimum_required(VERSION 3.20)
project(p_connect LANGUAGES CXX C)
enable_language(ASM)

if (APPLE AND NOT IOS)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
elseif (WIN32 OR (UNIX AND NOT APPLE))
    find_package(GTest     REQUIRED)
    find_package(benchmark REQUIRED)

    add_library(ffmpeg_headers INTERFACE)
    target_include_directories(ffmpeg_headers INTERFACE ${CMAKE_SOURCE_DIR}/build/ffmpeg/include/)
elseif (APPLE AND NOT IOS)
    find_package(GTest     REQUIRED)
    find_package(benchmark REQUIRED)

    add_library(ffmpeg_headers INTERFACE)
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
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2 Multimedia DBus)
else ()
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2 Multimedia)
endif ()

set(DEBUG_LOG_DISABLE_STACKTRACE ON CACHE BOOL "" FORCE)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG_LOG_DISABLE_CONSOLE_LOGGING ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
        debug-log
        GIT_REPOSITORY https://github.com/ddj4747/Debug-Log.git
        GIT_TAG main
)

FetchContent_MakeAvailable(debug-log)

if (APPLE AND NOT IOS)
    FetchContent_Declare(
            mediaremote-adapter
            GIT_REPOSITORY https://github.com/ungive/mediaremote-adapter.git
            GIT_TAG master
    )

    FetchContent_MakeAvailable(mediaremote-adapter)
    FetchContent_GetProperties(mediaremote-adapter
            SOURCE_DIR MEDIAREMOTE_ADAPTER_SOURCE_DIR
    )
endif ()

if (WIN32)
    add_library(WinToast STATIC vendor/WinToast/inc/wintoastlib.h vendor/WinToast/src/wintoastlib.cpp)
    target_include_directories(WinToast PUBLIC vendor/WinToast/inc/)
endif ()

qt_standard_project_setup()

add_library(project_defaults)

if (IOS)
    target_compile_definitions(project_defaults PUBLIC IOS_DEVICE)
endif ()

if (APPLE AND NOT IOS)
    target_compile_definitions(project_defaults PUBLIC MACOS_DEVICE)
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
    target_compile_definitions(project_defaults PUBLIC BOOST_ALL_NO_LIB)
endif()

BuildStaticLibrary(network-component-pc utilities/network project_defaults openssl::openssl asio::asio Debug-Log concurrentqueue::concurrentqueue magic_enum::magic_enum boost::boost)
BuildStaticLibrary(system-component-pc utilities/system project_defaults openssl::openssl asio::asio Debug-Log concurrentqueue::concurrentqueue magic_enum::magic_enum boost::boost)
BuildStaticLibrary(p2p-network-component-pc utilities/p2p-network project_defaults openssl::openssl asio::asio Debug-Log network-component-pc system-component-pc magic_enum::magic_enum concurrentqueue::concurrentqueue nlohmann_json::nlohmann_json Qt6::Core Qt6::Network boost::boost)
BuildStaticLibrary(file-system utilities/file-system project_defaults system-component-pc Qt6::Core Qt::Gui)
BuildStaticLibrary(clipboard utilities/clipboard project_defaults system-component-pc Debug-Log Qt6::Core Qt6::Gui)
BuildStaticLibrary(srtp-stream utilities/srtp-stream project_defaults asio::asio Debug-Log Boost::boost network-component-pc system-component-pc libsrtp::libsrtp)
BuildStaticLibrary(permission-manager utilities/permission-manager project_defaults Debug-Log Qt6::Core asio::asio)
BuildStaticLibrary(system-info utilities/system-info project_defaults Debug-Log Qt6::Core Qt6::Gui)

if (ANDROID)
    BuildStaticLibrary(sms-handler utilities/sms-handler network-component-pc system-component-pc p2p-network-component-pc project_defaults Qt6::Core Qt6::Gui)
    BuildStaticLibrary(find-my utilities/find-my project_defaults Qt6::Core)
endif ()

if ((WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE)) AND NOT ANDROID)
    target_link_libraries(p2p-network-component-pc PUBLIC desktop-daemon-utilities)
endif ()

if (APPLE)
    find_library(IOKIT_LIBRARY IOKit)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    target_link_libraries(system-info PRIVATE
            ${IOKIT_LIBRARY}
            ${COREFOUNDATION_LIBRARY}
    )

    if (NOT IOS)
        find_library(DNSSD_LIBRARY dns_sd)
        if (DNSSD_LIBRARY)
            target_link_libraries(permission-manager PUBLIC ${DNSSD_LIBRARY})
        endif ()
    endif ()
endif()

add_subdirectory("utilities/remote-input")
if (NOT (APPLE AND NOT IOS))
    add_subdirectory("utilities/virtual-camera")
endif ()

add_subdirectory("utilities/virtual-microphone")
add_subdirectory("utilities/notifications-handler")

BuildStaticLibrary(common-module modules/common Debug-Log Qt6::Core Qt6::Multimedia asio::asio p2p-network-component-pc permission-manager file-system)

if (ANDROID OR IOS)

    BuildApplicationModule(network-camera-module modules/network-camera/common modules/network-camera/mobile
            common-module
            srtp-stream
            ffmpeg_headers
            virtual-camera
    )

    BuildApplicationModule(network-microphone-module modules/network-microphone/common modules/network-microphone/mobile
            common-module
            virtual-microphone
            srtp-stream
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

    BuildApplicationModule(clipboard-sync-module modules/clipboard-sync/common modules/clipboard-sync/mobile
            common-module
            clipboard
    )

    BuildApplicationModule(remote-input-module modules/remote-input/common modules/remote-input/mobile
            common-module
            remote-input-types
    )

    BuildApplicationModule(sms-bridge-module modules/sms-bridge/common modules/sms-bridge/mobile
            common-module
            sms-handler
    )

    BuildApplicationModule(system-info-share-module modules/system-info-share/common modules/system-info-share/mobile
            common-module
            system-info
    )

elseif (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE))
    if (NOT (APPLE AND NOT IOS))
        BuildApplicationModule(network-camera-module modules/network-camera/common modules/network-camera/desktop
                common-module
                srtp-stream
                ffmpeg_headers
                virtual-camera
        )

        LinkFFMPEGLibs(network-camera-module)
    endif ()

    BuildApplicationModule(file-share-module modules/file-share/common modules/file-share/desktop
            common-module
            file-system
            xxHash::xxhash
    )

    BuildApplicationModule(network-microphone-module modules/network-microphone/common modules/network-microphone/desktop
            common-module
            virtual-microphone
            srtp-stream
    )

    BuildApplicationModule(notification-sync-module modules/notification-sync/common modules/notification-sync/desktop
            common-module
            notifications-handler
    )

    BuildApplicationModule(clipboard-sync-module modules/clipboard-sync/common modules/clipboard-sync/desktop
            common-module
            clipboard
    )

    BuildApplicationModule(remote-input-module modules/remote-input/common modules/remote-input/desktop
            common-module
            remote-input
    )

    BuildApplicationModule(sms-bridge-module modules/sms-bridge/common modules/sms-bridge/desktop
            common-module
    )

    BuildApplicationModule(system-info-share-module modules/system-info-share/common modules/system-info-share/desktop
            common-module
            system-info
    )

endif ()

BuildStaticLibrary(modules-manager modules/modules-manager
        network-component-pc
        system-component-pc
        p2p-network-component-pc
        common-module
        Debug-Log
        Qt6::Core
        file-share-module
        notification-sync-module
        clipboard-sync-module
        remote-input-module
        network-microphone-module
        sms-bridge-module
        system-info-share-module
)

if (NOT (APPLE AND NOT IOS))
    target_link_libraries(modules-manager PUBLIC network-camera-module)
endif ()

if (ANDROID)
    add_subdirectory("apps/mobile")
elseif (WIN32 OR (APPLE AND NOT IOS) OR (UNIX AND NOT APPLE))
    add_subdirectory("daemon")
    add_subdirectory("apps/desktop")
endif()

if (ENABLE_TESTS)
    BuildTests(Concurrent_Structures_Test concurrent-structures network-component-pc system-component-pc)
    BuildTests(Asio_Abstractions_Package_Test asio-abstractions/package network-component-pc system-component-pc)
    BuildTestProgram(DeviceType_Test device-type/DetectDeviceType.cpp system-component-pc)

    if (NOT (APPLE AND NOT IOS))
        BuildTestProgram(VirtualCameraTest virtual-camera/VirtualCameraTest.cpp virtual-camera Debug-Log)
    endif ()

    BuildTestProgram(VirtualMicrophoneTest virtual-microphone/VirtualMicrophoneTest.cpp virtual-microphone Debug-Log)
    BuildTestProgram(FileEntryTest file-system/FileEntryTest.cpp file-system)
    BuildTestProgram(FileSystemTest file-system/FileSystemTest.cpp file-system)
    BuildTestProgram(TextClipboardTest clipboard-sync/TextClipboardTest.cpp clipboard)
    BuildTestProgram(KeyboardTest remote-input/KeyboardTest.cpp remote-input)
    BuildTestProgram(MediaRemoteTest remote-input/MediaRemoteTest.cpp remote-input)
    BuildTestProgram(SystemVolumeControllerTest remote-input/SystemVolumeControllerTest.cpp remote-input)
    if (NOT ANDROID AND NOT IOS)
        BuildTestProgram(MediaTrackInfoTest remote-input/MediaTrackInfoTest.cpp remote-input)
        LinkMediaRemoteAdapter(MediaTrackInfoTest)
    endif ()

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
    if (NOT (APPLE AND NOT IOS))
        add_subdirectory("tests/network-camera-module/utilities-test")
    endif ()
    add_subdirectory("tests/test-apps/desktop")

    if (NOT (APPLE AND NOT IOS))
        LinkVirtualCameraLibs(VirtualCameraTest)
    endif ()
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
