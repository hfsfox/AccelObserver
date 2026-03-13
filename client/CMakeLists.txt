set(TRANSPORT_SOURCES "")
set(TRANSPORT_DEFINES "")
set(TRANSPORT_INCLUDES "")
set(TRANSPORT_LIBS "")

set(CONFPARSER_INCLUDES "")
set(CONFPARSER_DEFINES "")

if(ENABLE_WEBSOCKET)
    list(APPEND TRANSPORT_SOURCES src/transport/websocket/ws_client.c)
    list(APPEND TRANSPORT_DEFINES HAVE_WEBSOCKET)
endif()
if(ENABLE_MQTT)
    list(APPEND TRANSPORT_SOURCES src/transport/mqtt/mqtt_client.c)
    list(APPEND TRANSPORT_SOURCES src/crypto/sha1.c)
    list(APPEND TRANSPORT_SOURCES src/crypto/base64.c)
    list(APPEND TRANSPORT_DEFINES HAVE_MQTT)
endif()
if(ENABLE_CONF_PARSER)
    list(APPEND CONFPARSER_INCLUDES ../shared/include/)
    list(APPEND CONFPARSER_DEFINES ENABLE_CONFPARSER)
endif()

include_directories(${CMAKE_PROJECT_NAME}_client PUBLIC
    include
    ${MOSQUITTO_INCLUDE_DIR}
    ${CONFPARSER_INCLUDES}
    )

add_executable( ${CMAKE_PROJECT_NAME}_client
    src/main.c
    src/network/net_platform.c
    src/misc/conf_validator.c
    src/misc/help.c
    src/sensor/sensor_data_sim.c
    ${TRANSPORT_SOURCES}
    )

target_link_libraries(${CMAKE_PROJECT_NAME}_client PRIVATE ${MOSQUITTO_LIBRARY})
target_compile_definitions(${CMAKE_PROJECT_NAME}_client PRIVATE ${TRANSPORT_DEFINES} ${CONFPARSER_DEFINES})

if(WIN32)
    target_link_libraries(${CMAKE_PROJECT_NAME}_client PRIVATE ws2_32)
    target_compile_definitions(${CMAKE_PROJECT_NAME}_client PRIVATE
        _WIN32_WINNT=0x0601
        WIN32_LEAN_AND_MEAN
    )
endif()

if(HAIKU)
    target_link_libraries(${CMAKE_PROJECT_NAME}_client PRIVATE be network bsd)
endif()

if(UNIX)
    target_link_libraries(${CMAKE_PROJECT_NAME}_client PRIVATE
    m
    )
elseif(WIN32)
    target_link_libraries(${CMAKE_PROJECT_NAME}_client PRIVATE
    user32
    kernel32
    ws2_32
    )
endif()
