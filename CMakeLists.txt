#-------------------------------------------------------------------------------
# Basic Settings
#-------------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.15)

project(TocDaemonProject VERSION 1.0 LANGUAGES CXX)

#-------------------------------------------------------------------------------
# Find External Packages
#-------------------------------------------------------------------------------
find_package(nlohmann_json CONFIG REQUIRED)

#-------------------------------------------------------------------------------
# Set Source Files
#-------------------------------------------------------------------------------
set(TOCDAEMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/TocDaemonProcess.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TocDaemonServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Heartbeat/TocHeartbeat.cpp
)

#-------------------------------------------------------------------------------
# Build the TocDaemonProcess Executable
#-------------------------------------------------------------------------------
add_executable(TocDaemonProcess ${TOCDAEMON_SOURCES})

target_link_libraries(TocDaemonProcess
    PRIVATE
        nlohmann_json::nlohmann_json
)

target_include_directories(TocDaemonProcess PRIVATE ${CMAKE_INSTALL_PREFIX}/include)

install(TARGETS TocDaemonProcess
    RUNTIME DESTINATION bin
)

#-------------------------------------------------------------------------------
# End of CMake File
#-------------------------------------------------------------------------------
