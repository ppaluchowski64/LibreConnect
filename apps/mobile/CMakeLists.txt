BuildQTExecutable(appLibreConnect_mobile
        ${CMAKE_CURRENT_SOURCE_DIR}
        LibreConnect.mobile
        network-component-pc
        system-component-pc
        p2p-network-component-pc
        Qt6::Quick
        modules-manager
        network-camera-module
        find-my
        notifications-handler
        remote-input
        localization
)

LinkVirtualCameraLibs(appLibreConnect_mobile)

# The Android Qt kit does not ship LinguistTools, so the mobile catalog is
# shipped as a precompiled .qm file. Regenerate it with:
#   lrelease translations/libreconnect_mobile_zh_CN.ts
set(MOBILE_TRANSLATION_QM "${CMAKE_SOURCE_DIR}/translations/libreconnect_mobile_zh_CN.qm")
set_source_files_properties("${MOBILE_TRANSLATION_QM}"
        PROPERTIES QT_RESOURCE_ALIAS "libreconnect_mobile_zh_CN.qm")
qt_add_resources(appLibreConnect_mobile "mobile_translations"
        PREFIX "/i18n"
        FILES "${MOBILE_TRANSLATION_QM}"
)
