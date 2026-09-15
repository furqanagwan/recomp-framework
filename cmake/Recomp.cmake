include_guard(GLOBAL)

set(RECOMP_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "")

if(NOT TARGET recomp_common)
    add_subdirectory("${RECOMP_ROOT}/common" "${CMAKE_BINARY_DIR}/recomp_common")
endif()

function(recomp_add_game target)
    cmake_parse_arguments(GAME "" "OUTPUT_NAME;SETTINGS;ICON;RESOURCE;DEVELOPMENT_GAME_ROOT" "SOURCES" ${ARGN})

    if(WIN32)
        add_executable(${target} WIN32 ${GAME_SOURCES})
    else()
        add_executable(${target} ${GAME_SOURCES})
    endif()

    if(GAME_OUTPUT_NAME)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${GAME_OUTPUT_NAME}")
    endif()

    rexglue_setup_target(${target} GPU_PLUGINS xenos)
    target_link_libraries(${target} PRIVATE recomp::common)

    if(GAME_DEVELOPMENT_GAME_ROOT)
        target_compile_definitions(${target} PRIVATE
            RECOMP_DEVELOPMENT_GAME_ROOT="${GAME_DEVELOPMENT_GAME_ROOT}")
    endif()

    if(WIN32 AND GAME_RESOURCE AND GAME_ICON AND EXISTS "${GAME_ICON}")
        enable_language(RC)
        target_sources(${target} PRIVATE "${GAME_RESOURCE}")
        set_source_files_properties("${GAME_RESOURCE}" PROPERTIES OBJECT_DEPENDS "${GAME_ICON}")
    endif()

    if(GAME_SETTINGS)
        get_filename_component(settings_name "${GAME_SETTINGS}" NAME)
        set(staged_settings "${CMAKE_CURRENT_BINARY_DIR}/${settings_name}")
        add_custom_command(
            OUTPUT "${staged_settings}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GAME_SETTINGS}" "${staged_settings}"
            DEPENDS "${GAME_SETTINGS}"
            VERBATIM)
        add_custom_target(${target}_settings ALL DEPENDS "${staged_settings}")
    endif()
endfunction()
