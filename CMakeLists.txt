#-------------------------------------------------------------------------------
# Basic Settings
#-------------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.15)

# Use forward slashes and force the compiler paths
set(CMAKE_C_COMPILER "C:/msys64/ucrt64/bin/gcc.exe" CACHE STRING "C compiler" FORCE)
set(CMAKE_CXX_COMPILER "C:/msys64/ucrt64/bin/g++.exe" CACHE STRING "C++ compiler" FORCE)

project(TocDaemonProject VERSION 1.0 LANGUAGES CXX)

#-------------------------------------------------------------------------------
# Locate CMake Modules
#-------------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake
    ${CMAKE_SOURCE_DIR}/cmake
    ${CMAKE_INSTALL_PREFIX}/cmake
    ${CMAKE_INSTALL_PREFIX}/lib/cmake
)
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_MODULE_PATH})

#-------------------------------------------------------------------------------
# Find External Packages
#-------------------------------------------------------------------------------
find_package(nlohmann_json CONFIG REQUIRED)

#-------------------------------------------------------------------------------
# Include Directories and Link Directories
#-------------------------------------------------------------------------------
include_directories(${CMAKE_INSTALL_PREFIX}/include)
link_directories(${CMAKE_INSTALL_PREFIX}/lib)

#-------------------------------------------------------------------------------
# Set Source Files and Additional Libraries
#-------------------------------------------------------------------------------
# Update these paths as needed so CMake can locate your source files
set(TOCDAEMON_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/TocDaemonProcess.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TocDaemonServer.cpp
    # Uncomment the following if needed:
    # ${CMAKE_CURRENT_SOURCE_DIR}/daemon/AqtError.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/daemon/DirectoryServicesClient.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/daemon/DirectoryServicesServer.cpp
)

list(APPEND DAEMON_LIBS
    mnemosyne
    CameraControl
    database
    module
)

#-------------------------------------------------------------------------------
# Build the TocDaemon Library
#-------------------------------------------------------------------------------
add_library(TocDaemon STATIC ${TOCDAEMON_SOURCE})

target_link_libraries(TocDaemon PUBLIC
    ${DAEMON_LIBS}
    nlohmann_json::nlohmann_json
)

target_include_directories(TocDaemon PUBLIC
    ${CMAKE_INSTALL_PREFIX}/include
)

# Force CMake to treat this target as C++ (if it can’t automatically detect)
set_target_properties(TocDaemon PROPERTIES LINKER_LANGUAGE CXX)

install(TARGETS TocDaemon
    RUNTIME DESTINATION bin COMPONENT lib
    LIBRARY DESTINATION lib COMPONENT lib
    ARCHIVE DESTINATION lib COMPONENT lib
    INCLUDES DESTINATION include
)

#-------------------------------------------------------------------------------
# Build the TocDaemonProcess Executable
#-------------------------------------------------------------------------------
add_executable(TocDaemonProcess 
    ${CMAKE_CURRENT_SOURCE_DIR}/TocDaemonProcess.cpp
)

target_link_libraries(TocDaemonProcess
    TocDaemon
)

# Force the executable’s linker language to C++
set_target_properties(TocDaemonProcess PROPERTIES LINKER_LANGUAGE CXX)

install(TARGETS TocDaemonProcess
    RUNTIME DESTINATION bin COMPONENT lib
)

#-------------------------------------------------------------------------------
# End of CMake File
#-------------------------------------------------------------------------------
