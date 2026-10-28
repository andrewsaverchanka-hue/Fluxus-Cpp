cmake_minimum_required(VERSION 3.22)
project(Fluxus VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg)
find_path(LIBSSH_INCLUDE_DIR NAMES libssh/libssh.h REQUIRED)
find_library(LIBSSH_LIBRARY NAMES ssh libssh REQUIRED)

set(FLUXUS_CORE_SOURCES
    src/core/Models.cpp
    src/core/LogUtils.cpp
    src/core/LimaAnalyzer.cpp
    src/core/W2FrontendAnalyzer.cpp
    src/core/W2LowLevelAnalyzer.cpp
    src/core/AnalysisEngine.cpp
    src/core/Serializer.cpp
    src/core/Config.cpp
    src/core/Network.cpp
)

add_library(fluxus_core STATIC ${FLUXUS_CORE_SOURCES})
target_include_directories(fluxus_core PUBLIC src/core third_party ${LIBSSH_INCLUDE_DIR})
target_link_libraries(fluxus_core PUBLIC Threads::Threads ZLIB::ZLIB ${LIBSSH_LIBRARY})
if(MSVC)
    target_compile_options(fluxus_core PRIVATE /W4 /permissive-)
    target_compile_definitions(fluxus_core PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(fluxus_core PRIVATE -Wall -Wextra -Wpedantic -Wno-misleading-indentation)
endif()

add_executable(Fluxus WIN32
    src/app/main.cpp
    src/app/App.h
    src/app/Widgets.cpp
    src/app/ConnectorPage.cpp
    src/app/ParserPage.cpp
    src/app/ResultWindow.cpp
    src/app/MainWindow.cpp
    src/app/resources.qrc
)
target_include_directories(Fluxus PRIVATE src/app)
target_link_libraries(Fluxus PRIVATE fluxus_core Qt6::Widgets Qt6::Network Qt6::Svg)
set_target_properties(Fluxus PROPERTIES OUTPUT_NAME "Fluxus")

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET Fluxus POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --release --no-translations --compiler-runtime "$<TARGET_FILE:Fluxus>"
            COMMENT "Copying Qt runtime"
        )
    endif()
endif()

enable_testing()
add_executable(fluxus_tests tests/test_main.cpp)
target_link_libraries(fluxus_tests PRIVATE fluxus_core)
add_test(NAME fluxus_core_tests COMMAND fluxus_tests)
