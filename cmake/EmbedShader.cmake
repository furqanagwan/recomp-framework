foreach(required SHADER_SOURCE SHADER_SPIRV SHADER_OUTPUT SHADER_NAMESPACE
                 SHADER_IDENTIFIER SHADER_ENTRY_POINT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "EmbedShader.cmake requires -D${required}")
    endif()
endforeach()

string(REPLACE "|" ";" SHADER_INCLUDE_DIRS "${SHADER_INCLUDE_DIRS}")

function(expand_hlsl source stack output dependencies)
    cmake_path(NORMAL_PATH source OUTPUT_VARIABLE source)
    if(source IN_LIST stack)
        message(FATAL_ERROR "Cyclic HLSL include involving ${source}")
    endif()
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR "HLSL source does not exist: ${source}")
    endif()

    file(READ "${source}" content)
    list(APPEND stack "${source}")
    set(all_dependencies "${source}")

    while(content MATCHES "#[ \t]*include[ \t]*\"([^\"]+)\"")
        set(include_directive "${CMAKE_MATCH_0}")
        set(include_name "${CMAKE_MATCH_1}")
        get_filename_component(source_dir "${source}" DIRECTORY)
        set(search_dirs "${source_dir}" ${SHADER_INCLUDE_DIRS})
        set(include_path "")
        foreach(search_dir IN LISTS search_dirs)
            if(EXISTS "${search_dir}/${include_name}")
                cmake_path(ABSOLUTE_PATH include_name BASE_DIRECTORY "${search_dir}"
                           NORMALIZE OUTPUT_VARIABLE include_path)
                break()
            endif()
        endforeach()
        if(include_path STREQUAL "")
            message(FATAL_ERROR "${source}: cannot find quoted include '${include_name}'")
        endif()

        expand_hlsl("${include_path}" "${stack}" include_content include_dependencies)
        string(FIND "${content}" "${include_directive}" include_position)
        string(LENGTH "${include_directive}" directive_length)
        string(SUBSTRING "${content}" 0 ${include_position} before_include)
        math(EXPR after_position "${include_position} + ${directive_length}")
        string(SUBSTRING "${content}" ${after_position} -1 after_include)
        get_filename_component(source_name "${source}" NAME)
        string(CONCAT content "${before_include}" "\n#line 1 \"${include_name}\"\n"
                      "${include_content}" "\n#line 1 \"${source_name}\"\n" "${after_include}")
        list(APPEND all_dependencies ${include_dependencies})
    endwhile()

    list(REMOVE_DUPLICATES all_dependencies)
    set(${output} "${content}" PARENT_SCOPE)
    set(${dependencies} "${all_dependencies}" PARENT_SCOPE)
endfunction()

function(bytes_to_cpp input output)
    string(HEX "${input}" input_hex)
    string(LENGTH "${input_hex}" hex_length)
    set(result "")
    set(offset 0)
    while(offset LESS hex_length)
        string(SUBSTRING "${input_hex}" ${offset} 2 byte)
        string(APPEND result "0x${byte},")
        math(EXPR offset "${offset} + 2")
    endwhile()
    set(${output} "${result}" PARENT_SCOPE)
endfunction()

expand_hlsl("${SHADER_SOURCE}" "" hlsl_source hlsl_dependencies)
bytes_to_cpp("${hlsl_source}" hlsl_bytes)

if(NOT EXISTS "${SHADER_SPIRV}")
    message(FATAL_ERROR
        "Missing committed SPIR-V: ${SHADER_SPIRV}\n"
        "Run the game's <target>_spirv build target with DXC installed, then commit the file.")
endif()
file(READ "${SHADER_SPIRV}" spirv_hex HEX)
string(LENGTH "${spirv_hex}" spirv_hex_length)
math(EXPR spirv_remainder "${spirv_hex_length} % 8")
if(NOT spirv_remainder EQUAL 0)
    message(FATAL_ERROR "SPIR-V size is not a multiple of four bytes: ${SHADER_SPIRV}")
endif()

set(spirv_words "")
set(offset 0)
while(offset LESS spirv_hex_length)
    string(SUBSTRING "${spirv_hex}" ${offset} 8 word)
    string(SUBSTRING "${word}" 0 2 b0)
    string(SUBSTRING "${word}" 2 2 b1)
    string(SUBSTRING "${word}" 4 2 b2)
    string(SUBSTRING "${word}" 6 2 b3)
    string(APPEND spirv_words "0x${b3}${b2}${b1}${b0}u,")
    math(EXPR offset "${offset} + 8")
endwhile()

get_filename_component(output_dir "${SHADER_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(WRITE "${SHADER_OUTPUT}"
    "// Generated from ${SHADER_SOURCE} -- DO NOT EDIT\n"
    "#pragma once\n\n"
    "namespace recomp::shaders::${SHADER_NAMESPACE} {\n"
    "inline constexpr char ${SHADER_IDENTIFIER}_hlsl_data[] = {${hlsl_bytes}0};\n"
    "inline constexpr uint32_t ${SHADER_IDENTIFIER}_spirv_data[] = {${spirv_words}};\n"
    "inline constexpr EmbeddedShader ${SHADER_IDENTIFIER}{\n"
    "    std::string_view(${SHADER_IDENTIFIER}_hlsl_data, sizeof(${SHADER_IDENTIFIER}_hlsl_data) - 1),\n"
    "    std::span<const uint32_t>(${SHADER_IDENTIFIER}_spirv_data),\n"
    "    \"${SHADER_ENTRY_POINT}\"};\n"
    "}  // namespace recomp::shaders::${SHADER_NAMESPACE}\n")
