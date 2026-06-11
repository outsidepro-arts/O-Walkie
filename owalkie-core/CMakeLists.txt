cmake_minimum_required(VERSION 3.20)
project(owalkie-core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OWALKIE_CORE_BUILD_TESTS "Build unit tests" ON)
option(OWALKIE_CORE_BUILD_SESSION "Build session/transport (Boost.Asio + libopus)" ON)

if(ANDROID OR OWALKIE_CORE_USE_FETCH_JSON)
    set(OWALKIE_CORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
else()
    find_package(nlohmann_json REQUIRED)
endif()

if(NOT OWALKIE_CORE_TARGET)
    set(OWALKIE_CORE_TARGET owalkie_core)
endif()

add_library(${OWALKIE_CORE_TARGET}
    src/protocol.cpp
    src/json.cpp
    src/signal.cpp
    src/link_signal.cpp
    src/udp.cpp
    src/version.cpp
    src/c_api.cpp
)

target_include_directories(${OWALKIE_CORE_TARGET}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(${OWALKIE_CORE_TARGET}
    PUBLIC
        nlohmann_json::nlohmann_json
)

if(OWALKIE_CORE_BUILD_SESSION)
    if(ANDROID)
        if(NOT Boost_NO_SYSTEM_PATHS)
            if(DEFINED ENV{OWALKIE_BOOST_ROOT} AND NOT "$ENV{OWALKIE_BOOST_ROOT}" STREQUAL "")
                set(BOOST_ROOT "$ENV{OWALKIE_BOOST_ROOT}")
                set(Boost_NO_SYSTEM_PATHS ON)
            endif()
        endif()
        find_package(Boost CONFIG REQUIRED COMPONENTS system)
        find_package(Opus CONFIG REQUIRED)
        set(OPUS_TARGET Opus::opus)
    elseif(APPLE AND DEFINED VCPKG_TARGET_TRIPLET)
        find_package(Boost CONFIG REQUIRED COMPONENTS system)
        find_package(Opus CONFIG REQUIRED)
        set(OPUS_TARGET Opus::opus)
    elseif(WIN32 AND DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
        list(PREPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows")
        find_package(Boost CONFIG REQUIRED COMPONENTS system)
        find_package(Opus CONFIG REQUIRED)
        set(OPUS_TARGET Opus::opus)
    else()
        find_package(Boost REQUIRED COMPONENTS system)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
        set(OPUS_TARGET PkgConfig::OPUS)
    endif()

    target_sources(${OWALKIE_CORE_TARGET} PRIVATE
        src/opus_codec.cpp
        src/session.cpp
        src/session_manager.cpp
        src/activity_probe.cpp
    )
    target_compile_definitions(${OWALKIE_CORE_TARGET} PUBLIC OWALKIE_CORE_HAS_SESSION=1)
    target_link_libraries(${OWALKIE_CORE_TARGET}
        PUBLIC
            Boost::system
            ${OPUS_TARGET}
    )
    if(WIN32)
        target_link_libraries(${OWALKIE_CORE_TARGET} PUBLIC ws2_32)
    endif()
endif()

if(OWALKIE_CORE_BUILD_TESTS)
    enable_testing()
    add_executable(owalkie_core_tests
        tests/test_protocol.cpp
        tests/test_json.cpp
        tests/test_json_wire.cpp
        tests/test_signal.cpp
        tests/test_link_signal.cpp
        tests/test_session_info.cpp
        tests/test_client_events.cpp
        tests/test_udp.cpp
        tests/test_main.cpp
    )
    target_link_libraries(owalkie_core_tests PRIVATE ${OWALKIE_CORE_TARGET})
    add_test(NAME owalkie_core_tests COMMAND owalkie_core_tests)
endif()
