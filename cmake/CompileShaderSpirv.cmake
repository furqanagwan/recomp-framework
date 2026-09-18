foreach(required SHADER_SOURCE SHADER_OUTPUT SHADER_PROFILE SHADER_ENTRY_POINT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "CompileShaderSpirv.cmake requires -D${required}")
    endif()
endforeach()

if(NOT DXC_EXECUTABLE)
    find_program(DXC_EXECUTABLE NAMES dxc dxc.exe)
endif()
if(NOT DXC_EXECUTABLE)
    message(FATAL_ERROR
        "DXC was not found. Install the DirectX Shader Compiler or configure "
        "-DRECOMP_DXC_EXECUTABLE=<path-to-dxc>.")
endif()

string(REPLACE "|" ";" SHADER_INCLUDE_DIRS "${SHADER_INCLUDE_DIRS}")
set(include_args "")
foreach(include_dir IN LISTS SHADER_INCLUDE_DIRS)
    list(APPEND include_args -I "${include_dir}")
endforeach()

execute_process(
    COMMAND "${DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.1 -fvk-use-dx-layout
            -T "${SHADER_PROFILE}" -E "${SHADER_ENTRY_POINT}" ${include_args}
            -Fo "${SHADER_OUTPUT}" "${SHADER_SOURCE}"
    RESULT_VARIABLE dxc_result
    COMMAND_ECHO STDOUT)
if(NOT dxc_result EQUAL 0)
    message(FATAL_ERROR "DXC failed for ${SHADER_SOURCE} (${dxc_result})")
endif()
