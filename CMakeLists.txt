cmake_minimum_required(VERSION 3.21)
project(UtaNoidStudio VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets Multimedia)
qt_standard_project_setup()
add_library(utano_core STATIC
    src/Project.cpp
    src/Project.h
    src/Wav.cpp
    src/Wav.h
    src/Oto.cpp
    src/Oto.h
)
target_link_libraries(utano_core PUBLIC Qt6::Core Qt6::Gui)
target_include_directories(utano_core PUBLIC src)
if(MSVC)
    target_compile_options(utano_core PRIVATE /utf-8)
endif()
qt_add_executable(UtaNoidStudio WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/Waveform.cpp
    src/Waveform.h
)
target_link_libraries(UtaNoidStudio PRIVATE utano_core Qt6::Widgets Qt6::Multimedia)
if(MSVC)
    target_compile_options(UtaNoidStudio PRIVATE /utf-8)
endif()
set_target_properties(UtaNoidStudio PROPERTIES OUTPUT_NAME "UtaNoid Studio")
option(UTANOID_BUILD_TESTS "Build tests" ON)
if(UTANOID_BUILD_TESTS)
    enable_testing()
    add_executable(core_tests tests/CoreTests.cpp)
    target_link_libraries(core_tests PRIVATE utano_core)
    add_test(NAME core_tests COMMAND core_tests)
endif()
