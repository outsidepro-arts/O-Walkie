cmake_minimum_required(VERSION 3.20)
project(OWalkieDesktopCpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)

add_executable(owalkie-desktop-cpp
    src/main.cpp
    src/MainFrame.cpp
    src/RelayClient.cpp
    src/AudioEngine.cpp
)

target_include_directories(owalkie-desktop-cpp PRIVATE src)
target_compile_options(owalkie-desktop-cpp PRIVATE ${WX_CXXFLAGS_LIST})

target_link_libraries(owalkie-desktop-cpp PRIVATE
    ${WX_LIBS_LIST}
    Boost::system
    nlohmann_json::nlohmann_json
    PkgConfig::PORTAUDIO
    PkgConfig::OPUS
    ws2_32
    iphlpapi
)

if(MINGW)
    target_compile_definitions(owalkie-desktop-cpp PRIVATE _WIN32_WINNT=0x0601)
endif()
