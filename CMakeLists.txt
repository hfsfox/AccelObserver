#==============================================================================
# SPDX-FileCopyrightText: 2026 Kravchuk Roman aka hfsfox
#
# SPDX-License-Identifier: MIT
#==============================================================================

#
# - AccelObserver: G-sensor observer daemon and logger
#

cmake_minimum_required(VERSION 3.10)

project("AccelObserver"
        VERSION 0.0.1
        DESCRIPTION "AccelObserver: G-sensor observer daemon and logger"
        HOMEPAGE_URL "http://github.com/hfsfox/AccelObserver/"
        LANGUAGES C CXX
        )

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# =============================================================================
# Standard: Required C++14.
# =============================================================================
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# =============================================================================
# Standard: Required C11.
# =============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# =============================================================================
# Misc. settings
# =============================================================================
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# =============================================================================
# Build options
# =============================================================================
option(ENABLE_OUTPUT_CSV "Build with CSV output support"                   ON)
option(ENABLE_MQTT       "Build with MQTT support (requires libmosquitto)" ON)
option(ENABLE_WEBSOCKET  "Build with WebSocket server support"             ON)

# =============================================================================
# Core sources
# =============================================================================
set(CORE_SOURCES
    #src/core/packet_validator.cpp
    #src/core/storage_manager.cpp
    #src/format/csv_formatter.cpp
    #src/platform/net_platform.cpp
    server/core/source/statuscode.cpp
    server/core/source/network/networkstack.cpp
    )

set(CORE_INCLUDES
    server/core/include
    )

# =============================================================================
# Storage manger sources
# =============================================================================
set(STORAGE_MANGER_SOURCES
    server/storage/source/storagemanager.cpp
    )
set(STORAGE_MANGER_INCLUDES
    server/storage/include
    )

# =============================================================================
# Structure/Class Template Library sources
# =============================================================================
set(SCTL_INCLUDES
    server/sctl/include
    )

# =============================================================================
# Output format sources
# =============================================================================
set(FORMAT_SOURCES "")
set(FORMAT_DEFINES "")
set(FORMAT_INCLUDES "")
set(FORMAT_LIBS "")

set(FORMAT_INCLUDES
    server/format/include
    )

#set(FORMAT_SOURCES
#    server/format/source/impl/csvformatter.cpp
#    )

if(ENABLE_OUTPUT_CSV)
    list(APPEND FORMAT_SOURCES server/format/source/impl/csvformatter.cpp)
    list(APPEND FORMAT_DEFINES HAVE_OUTPUT_CSV)
    message(STATUS "CSV support: ON")
else()
    message(STATUS "CSV support: OFF")
endif()

# ==============================================================================
# Transport protocols
# ==============================================================================
set(TRANSPORT_SOURCES "")
set(TRANSPORT_DEFINES "")
set(TRANSPORT_INCLUDES "")
set(TRANSPORT_LIBS "")

list(APPEND TRANSPORT_INCLUDES server/transport/include)

# ---- WebSocket (только стандартная библиотека + BSD sockets) ----------------
if(ENABLE_WEBSOCKET)
    #list(APPEND TRANSPORT_SOURCES src/transport/ws_subscriber.cpp)
    list(APPEND TRANSPORT_DEFINES HAVE_WEBSOCKET)
    message(STATUS "WebSocket support: ON")
else()
    message(STATUS "WebSocket support: OFF")
endif()

# ---- MQTT (libmosquitto) ----------------------------------------------------
if(ENABLE_MQTT)
    # Поиск библиотеки: стандартный find_package или ручной поиск
    find_path(MOSQUITTO_INCLUDE_DIR
        NAMES mosquitto.h
        PATHS
            /usr/include
            /usr/local/include
            "C:/mosquitto/include"
            "${CMAKE_SOURCE_DIR}/third_party/mosquitto/include"
    )
    find_library(MOSQUITTO_LIBRARY
        NAMES mosquitto
        PATHS
            /usr/lib
            /usr/local/lib
            "C:/mosquitto/lib"
            "C:/Mosquitto/"
            "${CMAKE_SOURCE_DIR}/third_party/mosquitto/lib"
    )

    if(MOSQUITTO_INCLUDE_DIR AND MOSQUITTO_LIBRARY)
        #list(APPEND TRANSPORT_SOURCES  src/transport/mqtt_subscriber.cpp)
        list(APPEND TRANSPORT_DEFINES  HAVE_MQTT)
        list(APPEND TRANSPORT_INCLUDES ${MOSQUITTO_INCLUDE_DIR})
        list(APPEND TRANSPORT_LIBS     ${MOSQUITTO_LIBRARY})
        message(STATUS "MQTT support: ON (${MOSQUITTO_LIBRARY})")
    else()
        message(WARNING
            "MQTT support: OFF — libmosquitto not found.\n"
            "  Install: sudo apt install libmosquitto-dev (Debian/Ubuntu)\n"
            "           brew install mosquitto (macOS)\n"
            "  Or set MOSQUITTO_INCLUDE_DIR / MOSQUITTO_LIBRARY manually.")
        set(ENABLE_MQTT OFF)
    endif()
else()
    message(STATUS "MQTT support: OFF (disabled by option)")
endif()

# =============================================================================
# Server executable
# =============================================================================
add_executable(${CMAKE_PROJECT_NAME}_server
    server/main.cpp
    ${CORE_SOURCES}
    ${FORMAT_SOURCES}
    ${TRANSPORT_SOURCES}
    ${STORAGE_MANGER_SOURCES}
)

target_include_directories(${CMAKE_PROJECT_NAME}_server PUBLIC
    ${CORE_INCLUDES}
    ${SCTL_INCLUDES}
    ${FORMAT_INCLUDES}
    ${TRANSPORT_INCLUDES}
    ${STORAGE_MANGER_INCLUDES}
)

target_include_directories(${CMAKE_PROJECT_NAME}_server
    PRIVATE
        ${TRANSPORT_INCLUDES}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MINGW AND NOT MSVC)
    add_compile_options(-pthread)
endif()

target_compile_definitions(${CMAKE_PROJECT_NAME}_server
    PRIVATE
        ${TRANSPORT_DEFINES}
)

# =============================================================================
# Link platform libraries
# =============================================================================
if(WIN32)
    # Winsock2 — ws2_32 and mswsock
    target_link_libraries(${CMAKE_PROJECT_NAME}_server PRIVATE ws2_32 mswsock)
endif()

if(TRANSPORT_LIBS)
    target_link_libraries(${CMAKE_PROJECT_NAME}_server PRIVATE ${TRANSPORT_LIBS})
endif()

# pthreads on UNIX (std::thread requirement)
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(${CMAKE_PROJECT_NAME}_server PRIVATE Threads::Threads)
endif()

# =============================================================================
# Compiler warnings
# =============================================================================
if(MSVC)
    target_compile_options(${CMAKE_PROJECT_NAME}_server PRIVATE
        /W4          # 4th warning level
        /wd4100      # inhibit: unreferenced formal parameter
        /wd4996      # inhibit: deprecated POSIX names
    )
    add_compile_definitions(${CMAKE_PROJECT_NAME}_server PRIVATE
    UNICODE=1
    _UNICODE=1
    NOMINMAX=1
    )
else()
    # GCC / Clang
    target_compile_options(${CMAKE_PROJECT_NAME}_server PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

# Enable all warnings also in Debug mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-Wall -Wextra -Wpedantic)
    elseif(MSVC)
        add_compile_options(/W4)
    endif()
endif()

#
#
#

get_target_property(OUT_LINK_LIBS ${CMAKE_PROJECT_NAME}_server LINK_LIBRARIES)
get_target_property(OUT_BUILD_FLAGS ${CMAKE_PROJECT_NAME}_server COMPILE_FLAGS)

# ==============================================================================
# Build config output
# ==============================================================================
message(STATUS "")
message(STATUS "${CMAKE_PROJECT_NAME}_server build configuration:")
message(STATUS "C standard:             C${CMAKE_C_STANDARD}")
message(STATUS "C++ standard:           C++${CMAKE_CXX_STANDARD}")
message(STATUS "Platform:               ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler:               ${CMAKE_CXX_COMPILER_ID} \
${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Target arch:            ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Build C flags:          ${CMAKE_C_FLAGS}")
message(STATUS "Build CXX flags:        ${CMAKE_CXX_FLAGS}")
message(STATUS "Build flags:            ${OUT_BUILD_FLAGS}")
message(STATUS "Link libraries:         ${OUT_LINK_LIBS}")
message(STATUS "Build type:             ${CMAKE_BUILD_TYPE}")
message(STATUS "WebSocket:              ${ENABLE_WEBSOCKET}")
message(STATUS "MQTT:                   ${ENABLE_MQTT}")
message(STATUS "==========================================")
message(STATUS "")

# ==============================================================================
# Install (optional)
# ==============================================================================
install(TARGETS ${CMAKE_PROJECT_NAME}_server RUNTIME DESTINATION bin)
