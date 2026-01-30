BuildQTExecutable(appLibreConnect_mobile
        ${CMAKE_CURRENT_SOURCE_DIR}
        LibreConnect.mobile
        network-component-pc
        system-component-pc
        p2p-network-component-pc
        virtual-camera
        Qt6::Quick
        network-camera-module
)

LinkVirtualCameraLibs(appLibreConnect_mobile)