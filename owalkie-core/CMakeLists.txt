cmake_minimum_required(VERSION 3.20)
project(owalkie-core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OWALKIE_CORE_BUILD_TESTS "Build unit tests" ON)
option(OWALKIE_CORE_BUILD_SESSION "Build session/transport (Boost.Asio + libopus)" ON)

find_package(nlohmann_json REQUIRED)

add_library(owalkie_core
    src/protocol.cpp
    src/json.cpp
    src/signal.cpp
    src/udp.cpp
    src/version.cpp
    src/c_api.cpp
)

target_include_directories(owalkie_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(owalkie_core
    PUBLIC
        nlohmann_json::nlohmann_json
)

if(OWALKIE_CORE_BUILD_SESSION)
    find_package(Boost REQUIRED COMPONENTS system)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)

    target_sources(owalkie_core PRIVATE
        src/opus_codec.cpp
        src/session.cpp
    )
    target_compile_definitions(owalkie_core PUBLIC OWALKIE_CORE_HAS_SESSION=1)
    target_link_libraries(owalkie_core
        PUBLIC
            Boost::system
            PkgConfig::OPUS
    )
    if(WIN32)
        target_link_libraries(owalkie_core PUBLIC ws2_32)
    endif()
endif()

if(OWALKIE_CORE_BUILD_TESTS)
    enable_testing()
    add_executable(owalkie_core_tests
        tests/test_protocol.cpp
        tests/test_json.cpp
        tests/test_signal.cpp
        tests/test_udp.cpp
        tests/test_tx_signal.cpp
        tests/test_main.cpp
    )
    target_link_libraries(owalkie_core_tests PRIVATE owalkie_core)
    add_test(NAME owalkie_core_tests COMMAND owalkie_core_tests)
endif()
