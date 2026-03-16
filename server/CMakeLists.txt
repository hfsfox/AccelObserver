set(TRANSPORT_SOURCES "")
set(TRANSPORT_DEFINES "")
set(TRANSPORT_INCLUDES "")
set(TRANSPORT_LIBS "")

set(CONFPARSER_INCLUDES "")
set(CONFPARSER_DEFINES "")

if(ENABLE_WEBSOCKET)
    list(APPEND TRANSPORT_SOURCES src/transport/websocket/ws_subscriber.cpp)
    list(APPEND TRANSPORT_SOURCES src/crypto/sha1.c)
    list(APPEND TRANSPORT_SOURCES src/crypto/base64.c)
    list(APPEND TRANSPORT_DEFINES HAVE_WEBSOCKET)
endif()

if(HAVE_WEBUI)
    if(NOT HAVE_WEBSOCKET)
        message(WARNING "WEB Interface requires HAVE_WEBSOCKET (SHA-1/Base64). Disabling.")
        set(HAVE_WEBUI OFF)
    else()
        list(APPEND DS_SOURCES src/webif/web_interface.cpp)
        list(APPEND DS_DEFINES HAVE_WEBUI)
        message(STATUS "WEBIF      :          ON")
    endif()
else()
    message(STATUS "WEBIF      :          OFF")
endif()

if(NOT MSVC)
    if(ENABLE_MQTT)
        list(APPEND TRANSPORT_SOURCES src/transport/mqtt/mqtt_subscriber.cpp)
        list(APPEND TRANSPORT_DEFINES HAVE_MQTT)
    endif()
endif()
if(ENABLE_CONF_PARSER)
    list(APPEND CONFPARSER_INCLUDES ../shared/include/)
    list(APPEND CONFPARSER_DEFINES ENABLE_CONFPARSER)
endif()

include_directories(${CMAKE_PROJECT_NAME}_server PUBLIC
    include
    ${MOSQUITTO_INCLUDE_DIR}
    ${CONFPARSER_INCLUDES}
    )

add_executable( ${CMAKE_PROJECT_NAME}_server
    src/main.cpp
    src/misc/help.cpp
    src/logger/logger.cpp
    src/conf/confloader.cpp
    src/conf/cliargsparser.cpp
    src/network/net_platform.cpp
    src/storage/storage_manager.cpp
    src/format/csv/csv_formatter.cpp
    src/misc/packet_validator.cpp
    src/misc/stats_analyzer.cpp
    src/transport/websocket/ws_subscriber.cpp
    src/transport/mqtt/mqtt_subscriber.cpp
    #src/sensor/sensor_device.c
    ${TRANSPORT_SOURCES}
    )


if(MSVC)
    target_compile_options(${CMAKE_PROJECT_NAME}_server PRIVATE
    /W4
    /wd4100
    /wd4996
    )
else()
    target_compile_options(${CMAKE_PROJECT_NAME}_server PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        )
endif()

target_link_libraries(${CMAKE_PROJECT_NAME}_server PRIVATE
    ${MOSQUITTO_LIBRARY}
    )
target_compile_definitions(${CMAKE_PROJECT_NAME}_server PRIVATE
    ${TRANSPORT_DEFINES}
    ${CONFPARSER_DEFINES}
    )

if(WIN32)
    target_link_libraries(${CMAKE_PROJECT_NAME}_server PRIVATE
    ws2_32
    )
    target_compile_definitions(${CMAKE_PROJECT_NAME}_server PRIVATE
        _WIN32_WINNT=0x0601
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
    )
endif()

if(HAIKU)
    target_link_libraries(${CMAKE_PROJECT_NAME}_server PRIVATE
    be
    network
    bsd
    stdc++
    supc++
    )
endif()

if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(${CMAKE_PROJECT_NAME}_server PRIVATE
    Threads::Threads
    )
    find_library( mathlib m)
    if(mathlib)
        target_link_libraries(data_subscriber PRIVATE
        ${mathlib}
        )
    endif()
elseif(WIN32)
    target_link_libraries(${CMAKE_PROJECT_NAME}_server PRIVATE
    user32
    kernel32
    ws2_32
    )
endif()

install(TARGETS ${CMAKE_PROJECT_NAME}_server RUNTIME DESTINATION bin)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}../etc/gaccelserver/gaccelserver.conf")
    install(FILES gaccelserver.conf DESTINATION bin OPTIONAL)
endif()
