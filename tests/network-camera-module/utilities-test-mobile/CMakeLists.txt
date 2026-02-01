BuildQTExecutable(network-camera-module-utilities-test
        ${CMAKE_CURRENT_SOURCE_DIR}
        NetworkCameraModuleUtilitiesTest
        network-component-pc
        system-component-pc
        p2p-network-component-pc
        network-camera-module
        virtual-camera
        Qt6::Quick
        Qt6::Multimedia
)

if (ANDROID)
    set_property(TARGET network-camera-module-utilities-test PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android)
endif ()

LinkVirtualCameraLibs(network-camera-module-utilities-test)
LinkFFMPEGLibs(network-camera-module-utilities-test)