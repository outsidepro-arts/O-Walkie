cmake_minimum_required(VERSION 3.20)
project(OWalkieDesktop VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MINIAUDIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio")

execute_process(
    COMMAND bash -lc "export PATH=/ucrt64/bin:/usr/bin:$PATH; wx-config --cxxflags"
    OUTPUT_VARIABLE WX_CXXFLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND bash -lc "export PATH=/ucrt64/bin:/usr/bin:$PATH; wx-config --libs core,base"
    OUTPUT_VARIABLE WX_LIBS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
separate_arguments(WX_CXXFLAGS_LIST NATIVE_COMMAND "${WX_CXXFLAGS}")
separate_arguments(WX_LIBS_LIST NATIVE_COMMAND "${WX_LIBS}")

find_package(Boost REQUIRED COMPONENTS system)
find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)

add_executable(owalkie-desktop-cpp WIN32
    src/main.cpp
    src/LocaleInit.cpp
    src/MainFrame.cpp
    src/RelayClient.cpp
    src/AudioEngine.cpp
    src/miniaudio_impl.cpp
)
set_target_properties(owalkie-desktop-cpp PROPERTIES OUTPUT_NAME "owalkie-desktop")

get_filename_component(OWALKIE_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(_owalkie_desktop_version "0.0.0-dev")
if(DEFINED ENV{OWALKIE_VERSION_STRING} AND NOT "$ENV{OWALKIE_VERSION_STRING}" STREQUAL "")
    set(_owalkie_desktop_version "$ENV{OWALKIE_VERSION_STRING}")
elseif(DEFINED OWALKIE_VERSION_STRING AND NOT "${OWALKIE_VERSION_STRING}" STREQUAL "")
    set(_owalkie_desktop_version "${OWALKIE_VERSION_STRING}")
else()
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v*" --long --dirty
            WORKING_DIRECTORY "${OWALKIE_REPO_ROOT}"
            OUTPUT_VARIABLE _ow_git_desc
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(NOT "${_ow_git_desc}" STREQUAL "")
            string(REGEX REPLACE "^v" "" _owalkie_desktop_version "${_ow_git_desc}")
        endif()
    endif()
endif()
target_compile_definitions(owalkie-desktop-cpp PRIVATE
    "OWALKIE_VERSION=\"${_owalkie_desktop_version}\"")

target_include_directories(owalkie-desktop-cpp PRIVATE
    src
    "${MINIAUDIO_DIR}"
)

target_compile_options(owalkie-desktop-cpp PRIVATE ${WX_CXXFLAGS_LIST})

target_link_libraries(owalkie-desktop-cpp PRIVATE
    ${WX_LIBS_LIST}
    Boost::system
    nlohmann_json::nlohmann_json
    PkgConfig::OPUS
    ws2_32
    iphlpapi
)

if(MINGW)
    target_compile_definitions(owalkie-desktop-cpp PRIVATE _WIN32_WINNT=0x0601)
    target_link_libraries(owalkie-desktop-cpp PRIVATE
        winmm
        ole32
        uuid
        avrt
    )
endif()

set(OWALKIE_MSYS_ROOT "C:/dev/msys64" CACHE PATH "MSYS2 install root (for dist/ DLL bundling)")
if(WIN32 AND MINGW)
    set(_bundle_sh "${CMAKE_CURRENT_SOURCE_DIR}/scripts/bundle-ucrt64-deps.sh")
    set(_copy_sounds_cmake "${CMAKE_CURRENT_SOURCE_DIR}/scripts/copy-android-sounds.cmake")
    set(_copy_locale_cmake "${CMAKE_CURRENT_SOURCE_DIR}/scripts/copy-desktop-locale.cmake")
    set(OWALKIE_LOCALE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/locale")
    if(NOT IS_DIRECTORY "${OWALKIE_LOCALE_SRC}")
        message(FATAL_ERROR "Desktop locale directory is missing: ${OWALKIE_LOCALE_SRC}")
    endif()
    set(_owalkie_required_mo "${OWALKIE_LOCALE_SRC}/ru/LC_MESSAGES/owalkie.mo")
    if(NOT EXISTS "${_owalkie_required_mo}")
        message(FATAL_ERROR "Required gettext catalog is missing: ${_owalkie_required_mo}")
    endif()
    add_custom_command(TARGET owalkie-desktop-cpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dist"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:owalkie-desktop-cpp>"
            "${CMAKE_BINARY_DIR}/dist/"
        COMMAND ${CMAKE_COMMAND}
            -DLOCALE_SRC_DIR=${OWALKIE_LOCALE_SRC}
            -DOUT_LOCALE_DIR=$<TARGET_FILE_DIR:owalkie-desktop-cpp>/locale
            -P "${_copy_locale_cmake}"
        COMMAND ${CMAKE_COMMAND}
            -DLOCALE_SRC_DIR=${OWALKIE_LOCALE_SRC}
            -DOUT_LOCALE_DIR=${CMAKE_BINARY_DIR}/dist/locale
            -P "${_copy_locale_cmake}"
        COMMAND ${CMAKE_COMMAND}
            -DANDROID_RAW_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../android/app/src/main/res/raw
            -DOUT_SOUNDS_DIR=$<TARGET_FILE_DIR:owalkie-desktop-cpp>/sounds
            -P "${_copy_sounds_cmake}"
        COMMAND ${CMAKE_COMMAND}
            -DANDROID_RAW_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../android/app/src/main/res/raw
            -DOUT_SOUNDS_DIR=${CMAKE_BINARY_DIR}/dist/sounds
            -P "${_copy_sounds_cmake}"
        COMMAND "${OWALKIE_MSYS_ROOT}/usr/bin/bash.exe" "${_bundle_sh}"
            "$<TARGET_FILE:owalkie-desktop-cpp>"
            "${CMAKE_BINARY_DIR}/dist"
        VERBATIM
    )
endif()
