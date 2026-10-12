cmake_minimum_required(VERSION 3.25)

project(OpenSpeakNative VERSION 0.2.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(OPENSPEAK_BUILD_CLIENT "Compila o cliente (requer libdatachannel e FFmpeg via vcpkg)" ON)
option(OPENSPEAK_BUILD_TESTS "Compila os testes" ON)

if(MSVC)
    add_compile_options(/utf-8 /W3 /permissive- /Zc:__cplusplus)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network WebSockets HttpServer)
qt_standard_project_setup(REQUIRES 6.8)

# ---------------------------------------------------------------- common
add_library(openspeak_common INTERFACE)
target_include_directories(openspeak_common INTERFACE common)

# ---------------------------------------------------------------- servidor
add_library(openspeak_server_core STATIC
    server/VirtualServer.cpp server/VirtualServer.h
    server/ServerHost.cpp server/ServerHost.h)
target_link_libraries(openspeak_server_core PUBLIC openspeak_common Qt6::Core Qt6::Network Qt6::WebSockets Qt6::HttpServer)
target_include_directories(openspeak_server_core PUBLIC server)

qt_add_executable(openspeak-server server/main.cpp)
target_link_libraries(openspeak-server PRIVATE openspeak_server_core)

# ---------------------------------------------------------------- cliente
if(OPENSPEAK_BUILD_CLIENT)
    # No escopo raiz para que os testes (tests/client) enxerguem os alvos importados.
    find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Quick QuickControls2 Multimedia)
    find_package(LibDataChannel CONFIG REQUIRED)
    # FFmpeg estático do vcpkg (release/debug escolhidos por configuração).
    set(_vcpkg_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    add_library(openspeak_ffmpeg INTERFACE)
    target_include_directories(openspeak_ffmpeg INTERFACE "${_vcpkg_root}/include")
    foreach(lib avcodec swscale swresample avutil dav1d opus openh264 vpl)
        set(_dbg ${lib})
        if(lib STREQUAL "vpl")
            set(_dbg vpld)
        endif()
        target_link_libraries(openspeak_ffmpeg INTERFACE
            "$<IF:$<CONFIG:Debug>,${_vcpkg_root}/debug/lib/${_dbg}.lib,${_vcpkg_root}/lib/${lib}.lib>")
    endforeach()
    target_link_libraries(openspeak_ffmpeg INTERFACE mfuuid ole32 strmiids user32 advapi32 bcrypt)
    add_subdirectory(client)
endif()

# ---------------------------------------------------------------- testes
if(OPENSPEAK_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(tst_virtualserver tests/tst_virtualserver.cpp)
    target_link_libraries(tst_virtualserver PRIVATE openspeak_server_core Qt6::Test)
    add_test(NAME tst_virtualserver COMMAND tst_virtualserver)
    if(OPENSPEAK_BUILD_CLIENT)
        add_subdirectory(tests/client)
        add_test(NAME tst_windowthumbnail COMMAND tst_windowthumbnail)
        set_tests_properties(tst_windowthumbnail PROPERTIES TIMEOUT 15)
        foreach(panel setup source view-menu)
            add_test(NAME ui_${panel} COMMAND OpenSpeak --ui-smoke-test --profile ui-smoke-${panel} --open ${panel})
            set_tests_properties(ui_${panel} PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
        endforeach()
    endif()
endif()
