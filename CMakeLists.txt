cmake_minimum_required(VERSION 3.25.0 FATAL_ERROR)

set(PROJECT Dopamine)
set(DOPAMINE_VERSION 1.0.53)

project(${PROJECT} VERSION ${DOPAMINE_VERSION}
        DESCRIPTION "Dopamine by FRKN"
        HOMEPAGE_URL "https://frkn.org/"
)

string(TIMESTAMP CURRENT_DATE "%Y-%m-%d")
set(RELEASE_DATE "${CURRENT_DATE}")

set(APP_MAJOR_VERSION ${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}.${CMAKE_PROJECT_VERSION_PATCH})

# Windows VERSIONINFO resources need 4 numeric parts — pad 3-part marketing
# versions (e.g. 1.0.53) with a trailing .0 for the .rc files
if("${CMAKE_PROJECT_VERSION_TWEAK}" STREQUAL "")
    set(DOPAMINE_VERSION_RC_TWEAK 0)
else()
    set(DOPAMINE_VERSION_RC_TWEAK ${CMAKE_PROJECT_VERSION_TWEAK})
endif()

# Apple bundle build number (CFBundleVersion / CURRENT_PROJECT_VERSION):
# the tweak for 4-part versions (4.8.14.52 -> 52), the patch for 3-part
# marketing ones (1.0.53 -> 53). Must never be empty — iOS refuses to install
# an app whose appex has an empty CFBundleVersion.
if("${CMAKE_PROJECT_VERSION_TWEAK}" STREQUAL "")
    set(DOPAMINE_BUILD_NUMBER ${CMAKE_PROJECT_VERSION_PATCH})
else()
    set(DOPAMINE_BUILD_NUMBER ${CMAKE_PROJECT_VERSION_TWEAK})
endif()
# versionCode must keep growing monotonically (Google Play / APK update
# requirement) — it does NOT follow the marketing version reset to 1.0.x
set(APP_ANDROID_VERSION_CODE 2122)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(MZ_PLATFORM_NAME "linux")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set(MZ_PLATFORM_NAME "windows")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    set(MZ_PLATFORM_NAME "macos")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Android")
    set(MZ_PLATFORM_NAME "android")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "iOS")
    set(MZ_PLATFORM_NAME "ios")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Emscripten")
    set(MZ_PLATFORM_NAME "wasm")
endif()

set(QT_BUILD_TOOLS_WHEN_CROSS_COMPILING ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    if(IOS)
        set(CMAKE_OSX_ARCHITECTURES "arm64")
    elseif(MACOS_NE)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    elseif(NOT CMAKE_OSX_ARCHITECTURES)
        # honor an explicit -DCMAKE_OSX_ARCHITECTURES=... (e.g. arm64 local builds)
        set(CMAKE_OSX_ARCHITECTURES "x86_64")
    endif()
endif()

add_subdirectory(client)

if(NOT IOS AND NOT ANDROID AND NOT MACOS_NE)
    add_subdirectory(service)

    include(${CMAKE_SOURCE_DIR}/deploy/installer/config.cmake)
endif()

set(AMNEZIA_STAGE_DIR "${CMAKE_BINARY_DIR}/stage")

if(WIN32 AND NOT IOS AND NOT ANDROID AND NOT MACOS_NE)
    file(TO_CMAKE_PATH "${AMNEZIA_STAGE_DIR}" AMNEZIA_STAGE_DIR_CMAKE)

    set(CPACK_GENERATOR "WIX")
    set(CPACK_WIX_VERSION 4)
    set(CPACK_PACKAGE_NAME "Dopamine")
    set(CPACK_PACKAGE_VENDOR "FRKN")
    set(CPACK_PACKAGE_VERSION ${DOPAMINE_VERSION})
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "FRKN VPN client")
    set(AMNEZIA_LICENSE_TXT "${CMAKE_BINARY_DIR}/LICENSE.txt")
    configure_file("${CMAKE_SOURCE_DIR}/LICENSE" "${AMNEZIA_LICENSE_TXT}" COPYONLY)
    set(CPACK_RESOURCE_FILE_LICENSE "${AMNEZIA_LICENSE_TXT}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Dopamine")
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")
    set(CPACK_PACKAGE_EXECUTABLES "dopamine" "dopamine")
    set(CPACK_WIX_UPGRADE_GUID "{2D55AC62-96D6-4692-8C05-0D85BBF95485}")
    set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/client/images/app.ico")

    # WiX patches
    set(_AMNEZIA_WIX_PATCH_SERVICE "${CMAKE_SOURCE_DIR}/deploy/installer/wix/service_install_patch.xml")
    set(_AMNEZIA_WIX_PATCH_CLOSE_APP "${CMAKE_SOURCE_DIR}/deploy/installer/wix/close_client_patch.xml")
    file(TO_CMAKE_PATH "${_AMNEZIA_WIX_PATCH_SERVICE}" _AMNEZIA_WIX_PATCH_SERVICE_CMAKE)
    file(TO_CMAKE_PATH "${_AMNEZIA_WIX_PATCH_CLOSE_APP}" _AMNEZIA_WIX_PATCH_CLOSE_APP_CMAKE)
    set(CPACK_WIX_PATCH_FILE "${_AMNEZIA_WIX_PATCH_SERVICE_CMAKE};${_AMNEZIA_WIX_PATCH_CLOSE_APP_CMAKE}")

    # WiX v4 Util extension for CloseApplication + namespace for util
    set(CPACK_WIX_EXTENSIONS "${CPACK_WIX_EXTENSIONS};WixToolset.Util.wixext")
    set(CPACK_WIX_CUSTOM_XMLNS "util=http://wixtoolset.org/schemas/v4/wxs/util")

    set(CPACK_INSTALLED_DIRECTORIES "${AMNEZIA_STAGE_DIR_CMAKE};/")

    include(CPack)
endif()
