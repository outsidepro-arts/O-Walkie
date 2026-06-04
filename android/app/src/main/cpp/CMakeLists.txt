cmake_minimum_required(VERSION 3.22.1)

get_filename_component(OWALKIE_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../" ABSOLUTE)
include("${CMAKE_CURRENT_SOURCE_DIR}/owalkie_ndk_deps.cmake")

# ANDROID_ABI is set by the chain-loaded NDK toolchain before project().
owalkie_set_vcpkg_triplet()

project(owalkie_jni LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

owalkie_vcpkg_triplet(_owalkie_vcpkg_triplet)
owalkie_resolve_vcpkg_root(_owalkie_vcpkg_root)
owalkie_pin_vcpkg_package_dirs("${_owalkie_vcpkg_root}" "${_owalkie_vcpkg_triplet}")
owalkie_assert_vcpkg_installed()

include("${CMAKE_CURRENT_SOURCE_DIR}/owalkie_16kb.cmake")

add_subdirectory("${OWALKIE_REPO_ROOT}/owalkie-core" "${CMAKE_BINARY_DIR}/owalkie-core")

add_library(owalkie_jni SHARED
    owalkie_jni.cpp
    owalkie_opus_jni.cpp
)

target_include_directories(owalkie_jni PRIVATE
    "${OWALKIE_REPO_ROOT}/owalkie-core/include"
)

target_link_libraries(owalkie_jni
    owalkie_core
    log
)

target_include_directories(owalkie_jni PRIVATE
    "${OWALKIE_REPO_ROOT}/owalkie-core/src"
)

owalkie_enable_16kb_page_size(owalkie_jni)
