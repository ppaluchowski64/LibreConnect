

BuildQTExecutable(appLibreConnect_mobile
        ${CMAKE_CURRENT_SOURCE_DIR}
        LibreConnect.mobile
        network-component-pc
        system-component-pc
        p2p-network-component-pc
        Qt6::Quick
        modules-manager
        network-camera-module
)

LinkVirtualCameraLibs(appLibreConnect_mobile)
