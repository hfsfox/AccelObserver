#==============================================================================
# SPDX-FileCopyrightText: 2026 Kravchuk Roman aka hfsfox
#
# SPDX-License-Identifier: MIT
#==============================================================================

#
# - AccelObserver: G-sensor observer daemon and logger
#

cmake_minimum_required(VERSION 3.10)

project("gsensor"
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
set(CMAKE_CXX_EXTENSIONS ON)
# =============================================================================
# Standard: Required C11.
# =============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
# =============================================================================
# Misc. settings
# =============================================================================
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# =============================================================================
# Build options
# =============================================================================
option(ENABLE_OUTPUT_CSV  "Build with CSV output support"                   ON)
option(ENABLE_MQTT        "Build with MQTT support (requires libmosquitto)" ON)
option(ENABLE_WEBSOCKET   "Build with WebSocket server support"             ON)
option(ENABLE_CONF_PARSER "Build with config parser functional"             ON)

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

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MINGW AND NOT MSVC)
    add_compile_options(-pthread)
endif()

# Enable all warnings also in Debug mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-Wall -Wextra -Wpedantic)
    elseif(MSVC)
        add_compile_options(/W4)
    endif()
endif()

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
message(STATUS "Build type:             ${CMAKE_BUILD_TYPE}")
message(STATUS "WebSocket:              ${ENABLE_WEBSOCKET}")
message(STATUS "MQTT:                   ${ENABLE_MQTT}")
message(STATUS "Config parser:          ${ENABLE_CONF_PARSER}")
message(STATUS "==========================================")
message(STATUS "")

# ==============================================================================
# Install (optional)
# ==============================================================================
#install(TARGETS ${CMAKE_PROJECT_NAME}_server RUNTIME DESTINATION bin)

add_subdirectory(client)
add_subdirectory(server)
