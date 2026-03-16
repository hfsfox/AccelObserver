set(TRANSPORT_SOURCES "")
set(TRANSPORT_DEFINES "")
set(TRANSPORT_INCLUDES "")
set(TRANSPORT_LIBS "")

set(CONFPARSER_INCLUDES "")
set(CONFPARSER_DEFINES "")

if(ENABLE_WEBSOCKET)
    #list(APPEND TRANSPORT_SOURCES src/transport/websocket/ws_client.c)
    #list(APPEND TRANSPORT_SOURCES src/crypto/sha1.c)
    #list(APPEND TRANSPORT_SOURCES src/crypto/base64.c)
    list(APPEND TRANSPORT_DEFINES HAVE_WEBSOCKET)
endif()
if(NOT MSVC)
    if(ENABLE_MQTT)
        #list(APPEND TRANSPORT_SOURCES src/transport/mqtt/mqtt_client.c)
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
    #src/network/net_platform.c
    #src/misc/conf_validator.c
    #src/misc/help.c
    #src/sensor/sensor_data_sim.c
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
