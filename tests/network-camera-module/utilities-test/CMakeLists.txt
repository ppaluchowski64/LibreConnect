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

LinkVirtualCameraLibs(network-camera-module-utilities-test)