cmake_minimum_required(VERSION 3.25...4.4)

# cmake_minimum_required resets policies for this directory, so the opt-out a
# consumer made before add_subdirectory does not reach here. CMP0155 NEW turns
# on C++ module scanning, and clang-scan-deps runs without the compiler's own
# environment, so it fails on the first standard header it meets. Nothing here
# uses modules.
if(POLICY CMP0155)
    cmake_policy(SET CMP0155 OLD)
endif()

if(NOT TARGET rex::runtime)
    project(recomp_common LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(REXSDK_DIR "" CACHE PATH "rexglue-sdk source tree")
    if(REXSDK_DIR)
        add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
    else()
        find_package(rexglue REQUIRED CONFIG)
    endif()
endif()

add_library(recomp_common STATIC
    src/app/game_paths.cpp
    src/app/game_recomp_app.cpp
    src/debug/guest_image_dump.cpp
    src/debug/native_render_probe.cpp
    src/input/controller_menu_watcher.cpp
    src/input/guest_input_gate.cpp
    src/input/imgui_gamepad_bridge.cpp
    src/installer/content_package_installer.cpp
    src/installer/disc_image_installer.cpp
    src/installer/title_update_installer.cpp
    src/installer/xboxunity_catalog.cpp
    src/kernel/usb_camera_stubs.cpp
    src/memory/safe_guest_read.cpp
    src/platform/gaming_runtime_session.cpp
    src/render/native_renderer.cpp
    src/settings/user_settings_store.cpp
    src/ui/dialog_layout.cpp
    src/ui/disc_install_dialog.cpp
    src/ui/monochrome_theme.cpp
    src/ui/settings_dialog.cpp
    src/ui/title_update_dialog.cpp
    src/ui/update_required_dialog.cpp
    src/ui/guide_dialog.cpp
    src/ui/guide_blades.cpp
    src/ui/achievement_popup.cpp
    src/ui/guide_settings.cpp
    src/ui/guide_resources.cpp
    src/ui/guide_sounds.cpp
    src/ui/guide_theme.cpp
    src/ui/xbox_guide.cpp
    src/ui/virtual_keyboard_dialog.cpp
    src/ui/message_box_dialog.cpp
)

if(WIN32)
    target_sources(recomp_common PRIVATE src/platform/native_file_picker_windows.cpp
                                         src/platform/http_download_windows.cpp
                                         src/platform/application_restart_windows.cpp)
else()
    target_sources(recomp_common PRIVATE src/platform/native_file_picker_unix.cpp
                                         src/platform/http_download_unix.cpp
                                         src/platform/application_restart_unix.cpp)
endif()

add_library(recomp::common ALIAS recomp_common)
target_compile_features(recomp_common PUBLIC cxx_std_23)
target_include_directories(recomp_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(recomp_common PUBLIC rex::runtime)

option(RECOMP_BUILD_TESTS "Build recomp-framework unit tests" OFF)
if(RECOMP_BUILD_TESTS)
    enable_testing()
    add_executable(recomp_safe_guest_read_test tests/safe_guest_read_test.cpp)
    target_link_libraries(recomp_safe_guest_read_test PRIVATE recomp::common)
    add_test(NAME recomp.safe_guest_read COMMAND recomp_safe_guest_read_test)
    add_executable(recomp_native_renderer_test tests/native_renderer_test.cpp)
    target_link_libraries(recomp_native_renderer_test PRIVATE recomp::common)
    add_test(NAME recomp.native_renderer_fallback COMMAND recomp_native_renderer_test)
    add_test(NAME recomp.native_renderer_exception_fallback
             COMMAND recomp_native_renderer_test exception)
endif()

if(TARGET imgui)
    target_include_directories(recomp_common PUBLIC $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
endif()

# Windows is a GDK target and nothing else: the April 2026 GDK, whose folder is
# named for its edition. This is the one place to change for a newer one.
set(RECOMP_GDK_EDITION "260404" CACHE STRING "Microsoft GDK edition (April 2026)")
set(RECOMP_GDK_ROOT "C:/Program Files (x86)/Microsoft GDK/${RECOMP_GDK_EDITION}/windows"
    CACHE PATH "Microsoft GDK windows folder containing include and lib/x64")

if(WIN32 AND NOT EXISTS "${RECOMP_GDK_ROOT}/include/XGameRuntimeInit.h")
    # Not fatal, because a plain build still runs; but the Xbox PC app package
    # and the gaming runtime need it, so say which edition is missing rather
    # than letting those quietly not be built.
    message(WARNING "recomp_common: Microsoft GDK ${RECOMP_GDK_EDITION} not found at "
                    "${RECOMP_GDK_ROOT}. Install it (winget install Microsoft.Gaming.GDK) "
                    "for the gaming runtime and GDK packaging.")
endif()

if(WIN32 AND EXISTS "${RECOMP_GDK_ROOT}/include/XGameRuntimeInit.h")
    message(STATUS "recomp_common: Microsoft GDK found at ${RECOMP_GDK_ROOT}")
    target_compile_definitions(recomp_common PRIVATE RECOMP_HAS_GDK)
    target_include_directories(recomp_common PRIVATE "${RECOMP_GDK_ROOT}/include")
    target_link_libraries(recomp_common PUBLIC "${RECOMP_GDK_ROOT}/lib/x64/xgameruntime.lib" delayimp)
    target_link_options(recomp_common INTERFACE "LINKER:/DELAYLOAD:xgameruntime.dll")
endif()
