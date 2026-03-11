include_directories(${CMAKE_PROJECT_NAME}_client PUBLIC
    include
    )

add_executable( ${CMAKE_PROJECT_NAME}_client
    src/main.c
    src/network/net_platform.c
    )

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
