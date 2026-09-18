cmake_minimum_required(VERSION 3.25...4.4)
project(@PROJECT_NAME@ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)
include(../framework/cmake/Recomp.cmake)

recomp_add_game(@PROJECT_NAME@
    OUTPUT_NAME "@EXECUTABLE_NAME@"
    SOURCES
        src/main.cpp
    SETTINGS "${CMAKE_CURRENT_SOURCE_DIR}/settings/@PROJECT_NAME@.toml"
    ICON "${CMAKE_CURRENT_SOURCE_DIR}/metadata/@PROJECT_NAME@.ico"
    RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/@PROJECT_NAME@.rc"
    DEVELOPMENT_GAME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
