include_directories(${CMAKE_PROJECT_NAME}_client PUBLIC
    include
    )

add_executable( ${CMAKE_PROJECT_NAME}_client
    src/main.c
    src/network/net_platform.c
    )

if(UNIX)
    target_link_libraries(${CMAKE_PROJECT_NAME}_client PUBLIC
    )
elseif(HAIKU)
    target_link_libraries(${CMAKE_PROJECT_NAME}_client PUBLIC
    be
    bsd
    network
    )
elseif(WINDOWS)
    target_link_libraries(${CMAKE_PROJECT_NAME}_client PUBLIC
    user32
    kernel32
    ws2_32
    )
endif()
