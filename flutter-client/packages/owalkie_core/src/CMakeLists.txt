cmake_minimum_required(VERSION 3.14)

project(owalkie_core_plugin LANGUAGES CXX C)

set(_owalkie_search_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(OWALKIE_REPO_ROOT "")
foreach(_unused RANGE 12)
    if(EXISTS "${_owalkie_search_dir}/owalkie-core/CMakeLists.txt")
        set(OWALKIE_REPO_ROOT "${_owalkie_search_dir}")
        break()
    endif()
    get_filename_component(_owalkie_search_dir "${_owalkie_search_dir}/.." ABSOLUTE)
endforeach()
if(NOT OWALKIE_REPO_ROOT)
    message(FATAL_ERROR "owalkie-core repo root not found above ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

set(OWALKIE_MINIAUDIO_DIR "${OWALKIE_REPO_ROOT}/windows-client-cpp/third_party/miniaudio")

set(OWALKIE_CORE_TARGET owalkie_core_lib CACHE STRING "" FORCE)
set(OWALKIE_CORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)

if(ANDROID)
    include("${OWALKIE_REPO_ROOT}/android/app/src/main/cpp/owalkie_ndk_deps.cmake")
    owalkie_set_vcpkg_triplet()
    owalkie_resolve_vcpkg_root(_owalkie_vcpkg_root)
    owalkie_vcpkg_session_ready(_owalkie_session_ready)
    set(_owalkie_force_session OFF)
    if(DEFINED ENV{OWALKIE_FLUTTER_FULL_SESSION} AND "$ENV{OWALKIE_FLUTTER_FULL_SESSION}" STREQUAL "ON")
        set(_owalkie_force_session ON)
    endif()
    if(_owalkie_force_session OR _owalkie_session_ready)
        owalkie_pin_vcpkg_package_dirs("${_owalkie_vcpkg_root}" "${VCPKG_TARGET_TRIPLET}")
        owalkie_assert_vcpkg_installed()
        include("${OWALKIE_REPO_ROOT}/android/app/src/main/cpp/owalkie_16kb.cmake")
        set(OWALKIE_CORE_BUILD_SESSION ON CACHE BOOL "" FORCE)
    else()
        set(OWALKIE_CORE_BUILD_SESSION OFF CACHE BOOL "" FORCE)
        message(WARNING
            "Android session transport disabled (no vcpkg NDK deps for ${VCPKG_TARGET_TRIPLET}). "
            "Run android/scripts/build-ndk-deps.ps1 or set OWALKIE_FLUTTER_FULL_SESSION=ON after installing deps.")
    endif()
elseif(APPLE)
    set(OWALKIE_CORE_USE_FETCH_JSON ON CACHE BOOL "" FORCE)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/owalkie_ios_deps.cmake")
    set(_owalkie_force_session OFF)
    if(DEFINED ENV{OWALKIE_FLUTTER_FULL_SESSION} AND "$ENV{OWALKIE_FLUTTER_FULL_SESSION}" STREQUAL "ON")
        set(_owalkie_force_session ON)
    endif()
    owalkie_ios_vcpkg_session_ready(_owalkie_session_ready)
    if(_owalkie_force_session OR _owalkie_session_ready)
        owalkie_resolve_vcpkg_root(_owalkie_vcpkg_root)
        owalkie_ios_triplet_from_cmake(_owalkie_ios_triplet)
        owalkie_pin_vcpkg_ios_dirs("${_owalkie_vcpkg_root}" "${_owalkie_ios_triplet}")
        if(_owalkie_force_session)
            owalkie_assert_vcpkg_ios_installed()
        endif()
        set(OWALKIE_CORE_BUILD_SESSION ON CACHE BOOL "" FORCE)
    else()
        set(OWALKIE_CORE_BUILD_SESSION OFF CACHE BOOL "" FORCE)
        message(STATUS
            "iOS session transport disabled (no vcpkg ios deps). "
            "Run ios/scripts/build-ios-deps.sh then rebuild.")
    endif()
else()
    set(OWALKIE_CORE_USE_FETCH_JSON ON CACHE BOOL "" FORCE)
    if(WIN32 AND DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
        set(OWALKIE_CORE_BUILD_SESSION ON CACHE BOOL "" FORCE)
    elseif(WIN32)
        set(OWALKIE_CORE_BUILD_SESSION OFF CACHE BOOL "" FORCE)
    endif()
endif()

add_subdirectory("${OWALKIE_REPO_ROOT}/owalkie-core" "${CMAKE_BINARY_DIR}/owalkie-core-build")

if(APPLE)
    add_library(owalkie_core STATIC
        miniaudio_impl.cpp
        owalkie_flutter_audio.cpp
        owalkie_flutter_bridge.cpp
        owalkie_flutter_relay.cpp
    )
else()
    add_library(owalkie_core SHARED
        miniaudio_impl.cpp
        owalkie_flutter_audio.cpp
        owalkie_flutter_bridge.cpp
        owalkie_flutter_relay.cpp
    )
endif()

set_target_properties(owalkie_core PROPERTIES
    OUTPUT_NAME "owalkie_core"
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(owalkie_core PRIVATE
    "${OWALKIE_REPO_ROOT}/owalkie-core/include"
    "${OWALKIE_MINIAUDIO_DIR}"
)

target_link_libraries(owalkie_core PRIVATE ${OWALKIE_CORE_TARGET})

target_compile_definitions(owalkie_core PUBLIC DART_SHARED_LIB)

if(ANDROID AND OWALKIE_CORE_BUILD_SESSION)
    owalkie_enable_16kb_page_size(owalkie_core)
    target_link_options(owalkie_core PRIVATE "-Wl,-z,max-page-size=16384")
endif()

if(ANDROID)
    target_link_libraries(owalkie_core PRIVATE log android)
endif()

if(APPLE)
    target_link_libraries(owalkie_core PRIVATE
        "-framework AudioToolbox"
        "-framework AVFoundation"
        "-framework CoreAudio"
    )
endif()

if(WIN32)
    target_link_libraries(owalkie_core PRIVATE winmm ole32)
endif()

# Bundle vcpkg runtime DLLs next to the app (Flutter INSTALL → runner/<Config>/).
if(WIN32 AND OWALKIE_CORE_BUILD_SESSION AND DEFINED ENV{VCPKG_ROOT})
    set(_owalkie_vcpkg_root "$ENV{VCPKG_ROOT}")
    install(
        FILES "${_owalkie_vcpkg_root}/installed/x64-windows/debug/bin/opus.dll"
        DESTINATION "${CMAKE_INSTALL_PREFIX}"
        CONFIGURATIONS Debug
        COMPONENT Runtime
    )
    install(
        FILES "${_owalkie_vcpkg_root}/installed/x64-windows/bin/opus.dll"
        DESTINATION "${CMAKE_INSTALL_PREFIX}"
        CONFIGURATIONS Release RelWithDebInfo MinSizeRel Profile
        COMPONENT Runtime
    )
endif()
