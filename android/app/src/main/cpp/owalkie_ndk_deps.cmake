# Default vcpkg root on Windows dev machines (override with VCPKG_ROOT).
set(OWALKIE_VCPKG_ROOT_DEFAULT "C:/dev/vcpkg")

function(owalkie_resolve_vcpkg_root out_var)
    if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
        set(${out_var} "$ENV{VCPKG_ROOT}" PARENT_SCOPE)
    else()
        set(${out_var} "${OWALKIE_VCPKG_ROOT_DEFAULT}" PARENT_SCOPE)
    endif()
endfunction()

function(owalkie_vcpkg_triplet out_var)
    if(ANDROID_ABI STREQUAL "arm64-v8a")
        set(${out_var} "arm64-android" PARENT_SCOPE)
    elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
        set(${out_var} "arm-neon-android" PARENT_SCOPE)
    elseif(ANDROID_ABI STREQUAL "x86_64")
        set(${out_var} "x64-android" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Unsupported ANDROID_ABI: ${ANDROID_ABI}")
    endif()
endfunction()

function(owalkie_set_vcpkg_triplet)
    owalkie_vcpkg_triplet(_triplet)
    set(VCPKG_TARGET_TRIPLET "${_triplet}" CACHE STRING "vcpkg triplet for owalkie NDK deps" FORCE)
endfunction()

# Pin *_DIR to the ABI matching install tree (avoids stale x64 paths in CMakeCache).
function(owalkie_pin_vcpkg_package_dirs vcpkg_root triplet)
    set(_prefix "${vcpkg_root}/installed/${triplet}")
    set(VCPKG_INSTALLED_DIR "${_prefix}" CACHE PATH "vcpkg installed prefix" FORCE)
    set(Boost_DIR "${_prefix}/share/boost" CACHE PATH "" FORCE)
    set(Opus_DIR "${_prefix}/share/opus" CACHE PATH "" FORCE)
endfunction()

function(owalkie_assert_vcpkg_installed)
    owalkie_vcpkg_triplet(_triplet)
    owalkie_resolve_vcpkg_root(_vcpkg_root)
    set(_prefix "${_vcpkg_root}/installed/${_triplet}")
    if(NOT EXISTS "${_prefix}/share/boost/BoostConfig.cmake")
        message(FATAL_ERROR
            "Missing vcpkg packages for ${_triplet} under ${_vcpkg_root}.\n"
            "Run: android/scripts/build-ndk-deps.ps1")
    endif()
endfunction()
