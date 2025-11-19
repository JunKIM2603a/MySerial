cmake_minimum_required(VERSION 3.15)
project(TestRunner2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR}/..)

set(SOURCES
    main.cpp
    Message.cpp
    ProcessManager.cpp
    ControlServer.cpp
    ControlClient.cpp
)

set(HEADERS
    Protocol.h
    Message.h
    ProcessManager.h
    ControlServer.h
    ControlClient.h
)

add_executable(TestRunner2 ${SOURCES} ${HEADERS})

if(WIN32)
    target_link_libraries(TestRunner2 PRIVATE ws2_32)
    target_compile_options(TestRunner2 PRIVATE /W4)
else()
    target_link_libraries(TestRunner2 PRIVATE pthread)
endif()

