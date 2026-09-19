include_guard(GLOBAL)

set(RECOMP_DEPLOYMENT_TARGET "windows_pc" CACHE STRING
    "Deployment target: windows_pc or xbox_console")
set_property(CACHE RECOMP_DEPLOYMENT_TARGET PROPERTY STRINGS windows_pc xbox_console)

if(RECOMP_DEPLOYMENT_TARGET STREQUAL "windows_pc")
    set(RECOMP_IS_WINDOWS_PC ON)
    set(RECOMP_IS_XBOX_CONSOLE OFF)
elseif(RECOMP_DEPLOYMENT_TARGET STREQUAL "xbox_console")
    set(RECOMP_IS_WINDOWS_PC OFF)
    set(RECOMP_IS_XBOX_CONSOLE ON)
    if(NOT RECOMP_SECURE_XBOX_TOOLCHAIN_READY)
        message(FATAL_ERROR
            "The xbox_console profile requires Microsoft's secure Xbox GDK toolchain and "
            "console integration, which are not distributed in this public repository. "
            "Do not turn RECOMP_SECURE_XBOX_TOOLCHAIN_READY on for a desktop toolchain. "
            "See docs/HELIX_READINESS.md.")
    endif()
else()
    message(FATAL_ERROR
        "Unknown RECOMP_DEPLOYMENT_TARGET='${RECOMP_DEPLOYMENT_TARGET}'. "
        "Expected windows_pc or xbox_console.")
endif()
