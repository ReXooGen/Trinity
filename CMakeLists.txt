cmake_minimum_required(VERSION 3.20)
project(Trinity LANGUAGES CXX RC)

# ---------------------------------------------------------------------------
# Trinity - internal ImGui mod menu for Crimson Desert (DirectX 12).
# Builds a Trinity.asi that an ASI loader injects into the game process.
# ---------------------------------------------------------------------------

if(NOT WIN32)
    message(FATAL_ERROR "Trinity targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # static CRT -> no VC++ redist needed in-game

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

# --- Dear ImGui (docking branch, classic DX12 backend API) -----------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5-docking
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# --- MinHook (function detours) --------------------------------------------
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    # Pin the source because the local patch below targets this reviewed tree.
    GIT_TAG        8af6b4acae5a9388fd742b56fa79ece89d96f823
)
FetchContent_MakeAvailable(minhook)

# Crimson Desert PE 2944 reserves every allocation-granularity slot around
# gameplay code, so upstream MinHook cannot create its near rel32 relay even
# when a unique target AOB is present.  Apply the audited absolute-jump
# fallback reproducibly after FetchContent.  The reverse check makes repeated
# CMake configuration idempotent and fails closed if upstream source changes.
find_package(Git REQUIRED)
set(TRINITY_MINHOOK_PATCH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/minhook-absolute-detour-fallback.patch")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check "${TRINITY_MINHOOK_PATCH}"
    WORKING_DIRECTORY "${minhook_SOURCE_DIR}"
    OUTPUT_QUIET
    ERROR_QUIET
    RESULT_VARIABLE trinity_minhook_patch_forward)
if(trinity_minhook_patch_forward EQUAL 0)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply "${TRINITY_MINHOOK_PATCH}"
        WORKING_DIRECTORY "${minhook_SOURCE_DIR}"
        RESULT_VARIABLE trinity_minhook_patch_apply)
    if(NOT trinity_minhook_patch_apply EQUAL 0)
        message(FATAL_ERROR "Unable to apply Trinity's MinHook absolute-detour patch.")
    endif()
else()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${TRINITY_MINHOOK_PATCH}"
        WORKING_DIRECTORY "${minhook_SOURCE_DIR}"
        OUTPUT_QUIET
        ERROR_QUIET
        RESULT_VARIABLE trinity_minhook_patch_reverse)
    if(NOT trinity_minhook_patch_reverse EQUAL 0)
        message(FATAL_ERROR "Fetched MinHook no longer matches Trinity's reviewed absolute-detour patch.")
    endif()
endif()

option(ENABLE_EXTENDED_HOOKS "Enable extended engine hooks" OFF)

set(EXTENDED_SOURCES "")
if(ENABLE_EXTENDED_HOOKS)
    add_compile_definitions(TRINITY_EXTENDED)
    set(EXTENDED_SOURCES src/game/dlc.cpp)
endif()

# --- Trinity ASI -----------------------------------------------------------
add_library(Trinity SHARED
    src/dllmain.cpp
    src/core/crash_diagnostics.cpp
    src/core/crash_diagnostics_logic.cpp
    src/core/mod.cpp
    src/core/readiness.cpp
    src/core/version_mapping.cpp
    src/mem/section_filter.cpp
    src/core/settings.cpp
    src/core/localization.cpp
    src/core/version_detect.cpp
    src/mem/scanner.cpp
    src/game/player.cpp
    src/game/teleport.cpp
    src/game/inventory.cpp
    src/game/inventory_logic.cpp
    src/game/player_logic.cpp
    src/game/equipment_logic.cpp
    src/game/world.cpp
    src/game/equipment.cpp
    src/game/dye.cpp
    src/game/friendly.cpp
    src/game/friendly_logic.cpp
    src/game/worker.cpp
    ${EXTENDED_SOURCES}
    src/game/item_names.cpp
    src/game/pak.cpp
    src/hooks/dx12_hook.cpp
    src/hooks/input.cpp
    src/hooks/xinput_hook.cpp
    src/gui/framework.cpp
    src/gui/widgets.cpp
    src/gui/icons.cpp
    src/gui/menu.cpp
    src/Trinity.rc               # VERSIONINFO -> legitimate PE metadata for AV/ML scoring
)

target_include_directories(Trinity PRIVATE src)
# Resolve #include "core/version.h" from the .rc via an explicit /I for the RC compiler.
target_include_directories(Trinity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(Trinity PRIVATE imgui minhook d3d12 dxgi dwmapi imm32 xinput9_1_0 version bcrypt dbghelp)
target_compile_definitions(Trinity PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

# .asi is just a DLL with a different extension; strip the lib prefix too.
set_target_properties(Trinity PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
    OUTPUT_NAME "Trinity"
)

# --- Binary hardening ------------------------------------------------------
# ASLR (/DYNAMICBASE + /HIGHENTROPYVA), DEP (/NXCOMPAT) and Control Flow Guard
# make the PE look like a modern, legitimately-compiled binary. Combined with a
# code signature these markers materially improve AV/ML reputation scoring; they
# cost nothing at runtime for a mod of this size.
if(MSVC)
    target_compile_options(Trinity PRIVATE /guard:cf)
    target_link_options(Trinity PRIVATE
        /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /guard:cf)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(TrinityMapMarkerTests tests/map_marker_tests.cpp)
    target_include_directories(TrinityMapMarkerTests PRIVATE src)
    add_test(NAME TrinityMapMarkerTests COMMAND TrinityMapMarkerTests)

    add_executable(TrinityTravelLogicTests tests/travel_logic_tests.cpp)
    target_include_directories(TrinityTravelLogicTests PRIVATE src)
    add_test(NAME TrinityTravelLogicTests COMMAND TrinityTravelLogicTests)

    add_executable(TrinityNoClipLogicTests tests/noclip_logic_tests.cpp)
    target_include_directories(TrinityNoClipLogicTests PRIVATE src)
    add_test(NAME TrinityNoClipLogicTests COMMAND TrinityNoClipLogicTests)

    add_executable(TrinityReadinessTests
        tests/readiness_tests.cpp
        src/core/readiness.cpp
        src/core/version_mapping.cpp
        src/game/friendly_logic.cpp
        src/game/inventory_logic.cpp
        src/game/player_logic.cpp
        src/game/equipment_logic.cpp
        src/mem/section_filter.cpp
    )
    target_include_directories(TrinityReadinessTests PRIVATE src)
    add_test(NAME TrinityReadinessTests COMMAND TrinityReadinessTests)

    add_test(
        NAME TrinityCrashReportingContractTests
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_crash_reporting_contract.ps1")

    add_executable(TrinityCrashDiagnosticsTests
        tests/crash_diagnostics_tests.cpp
        src/core/crash_diagnostics_logic.cpp
    )
    target_include_directories(TrinityCrashDiagnosticsTests PRIVATE src)
    add_test(NAME TrinityCrashDiagnosticsTests COMMAND TrinityCrashDiagnosticsTests)

    add_executable(TrinityCrashDiagnosticsHarness
        tests/crash_diagnostics_harness.cpp
        src/core/crash_diagnostics.cpp
        src/core/crash_diagnostics_logic.cpp
    )
    target_include_directories(TrinityCrashDiagnosticsHarness PRIVATE src)
    target_compile_definitions(TrinityCrashDiagnosticsHarness PRIVATE NOMINMAX)
    target_link_libraries(TrinityCrashDiagnosticsHarness PRIVATE minhook dbghelp bcrypt version)

    add_test(
        NAME TrinityCrashDiagnosticsHarnessTests
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_crash_diagnostics_harness.ps1"
                -HarnessPath "$<TARGET_FILE:TrinityCrashDiagnosticsHarness>")

    # Regression for PE 2944's fragmented virtual address space: proves that
    # the reviewed MinHook patch can install and remove a far trampoline when
    # upstream's complete +/-512 MiB relay window is already reserved.
    add_executable(TrinityMinHookFallbackTests tests/minhook_fallback_tests.cpp)
    target_link_libraries(TrinityMinHookFallbackTests PRIVATE minhook)
    add_test(NAME TrinityMinHookFallbackTests COMMAND TrinityMinHookFallbackTests)
endif()
