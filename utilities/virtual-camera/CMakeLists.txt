if (WIN32)
    include(ExternalProject)
    find_program(MSBUILD_EXECUTABLE NAMES MSBuild.exe MSBuild)

    if (NOT MSBUILD_EXECUTABLE)
        message(FATAL_ERROR "Required MSBuild not found")
    endif()

    ExternalProject_Add(
            softcam_project
            GIT_REPOSITORY https://github.com/tshino/softcam.git
            GIT_TAG v1.8.1
            CONFIGURE_COMMAND ""
            BUILD_IN_SOURCE 1
            BUILD_COMMAND
            "${MSBUILD_EXECUTABLE}"
            "<SOURCE_DIR>/softcam.sln"
            /p:Configuration=Release
            /p:Platform=x64
            /t:softcam
            INSTALL_COMMAND ""
    )

    ExternalProject_Get_Property(softcam_project SOURCE_DIR)

    set(SOFTCAM_INCLUDE_DIR "${SOURCE_DIR}/dist/include")
    file(MAKE_DIRECTORY ${SOFTCAM_INCLUDE_DIR})

    add_library(Softcam::Client SHARED IMPORTED GLOBAL)
    add_dependencies(Softcam::Client softcam_project)

    set_target_properties(Softcam::Client PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${SOFTCAM_INCLUDE_DIR}"
            IMPORTED_IMPLIB "${SOURCE_DIR}/x64/Release/softcam.lib"
            IMPORTED_LOCATION "${SOURCE_DIR}/x64/Release/softcam.dll"
    )
endif()