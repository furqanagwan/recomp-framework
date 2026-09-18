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

# The Windows SDK ships a dxc.exe built without SPIR-V, and it is the one on
# PATH on a machine with no Vulkan SDK. Left to itself it fails per shader with
# "SPIR-V CodeGen not available", which reads like a broken shader rather than
# the wrong compiler.
execute_process(COMMAND "${DXC_EXECUTABLE}" --version
                OUTPUT_VARIABLE dxc_version ERROR_VARIABLE dxc_version_error
                OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND "${DXC_EXECUTABLE}" -spirv -T ps_6_0 -E main -Fo "${SHADER_OUTPUT}.probe"
                        "${CMAKE_CURRENT_LIST_DIR}/SpirvSupportProbe.hlsl"
                RESULT_VARIABLE spirv_probe_result OUTPUT_QUIET ERROR_VARIABLE spirv_probe_error)
file(REMOVE "${SHADER_OUTPUT}.probe")
if(NOT spirv_probe_result EQUAL 0 AND spirv_probe_error MATCHES "SPIR-V CodeGen not available")
    message(FATAL_ERROR
        "This dxc cannot emit SPIR-V:
  ${DXC_EXECUTABLE}
  ${dxc_version}
"
        "The Windows SDK builds dxc without SPIR-V. Install the Vulkan SDK, or a "
        "DirectX Shader Compiler release from Microsoft's GitHub, and point "
        "-DRECOMP_DXC_EXECUTABLE at that one. The DXIL half does not need it.")
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
