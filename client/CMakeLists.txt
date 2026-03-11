include_directories(${CMAKE_PROJECT_NAME}_client PUBLIC
    include
    ${MOSQUITTO_INCLUDE_DIR}
    )

add_executable( ${CMAKE_PROJECT_NAME}_client
    src/main.c
    src/network/net_platform.c
    src/misc/conf_validator.c
    src/misc/help.c
    src/sensor/sensor_data_sim.c
    src/transport/websocket/ws_client.c
    src/transport/mqtt/mqtt_client.c
    )

target_link_libraries(${CMAKE_PROJECT_NAME}_client PRIVATE ${MOSQUITTO_LIBRARY})

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
    )
elseif(WIN32)
    target_link_libraries(${CMAKE_PROJECT_NAME}_client PRIVATE
    user32
    kernel32
    ws2_32
    )
endif()
