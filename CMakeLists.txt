# SPDX-FileCopyrightText: 2026 hyprtacle contributors
#
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(hyprtacle VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core DBus Gui)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
endif()

include(GNUInstallDirs)

qt_standard_project_setup(REQUIRES 6.5)

set(HYPRTACLE_SOURCES
    src/main.cpp
    src/Logging.cpp
    src/ScreenshotService.cpp
    src/ScreenshotService.h
    src/HyprlandInfo.cpp
    src/HyprlandInfo.h
    src/CaptureJob.cpp
    src/CaptureJob.h
    src/PipeWriter.cpp
    src/PipeWriter.h
)

# The D-Bus interface is a verbatim copy of the one KWin exports, so that stock
# Spectacle picks the service up and talks to it as if it were KWin.
qt6_add_dbus_adaptor(HYPRTACLE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/data/org.kde.KWin.ScreenShot2.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ScreenshotService.h
)

add_executable(hyprtacled ${HYPRTACLE_SOURCES})

target_include_directories(hyprtacled PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(hyprtacled PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
)

install(TARGETS hyprtacled RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/data/org.kde.KWin.ScreenShot2.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.kde.KWin.ScreenShot2.service
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.KWin.ScreenShot2.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/data/hyprtacled.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/hyprtacled.service
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/hyprtacled.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/systemd/user
)

# A D-Bus client that talks to the service exactly like Spectacle does.
add_executable(hyprtacle-capture tools/hyprtacle-capture.cpp)
target_link_libraries(hyprtacle-capture PRIVATE Qt6::Core Qt6::DBus Qt6::Gui)
install(TARGETS hyprtacle-capture RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
