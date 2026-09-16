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
    GIT_TAG        master
)
FetchContent_MakeAvailable(minhook)

option(ENABLE_EXTENDED_HOOKS "Enable extended engine hooks" OFF)

set(EXTENDED_SOURCES "")
if(ENABLE_EXTENDED_HOOKS)
    add_compile_definitions(TRINITY_EXTENDED)
    set(EXTENDED_SOURCES src/game/dlc.cpp)
endif()

# --- Trinity ASI -----------------------------------------------------------
add_library(Trinity SHARED
    src/dllmain.cpp
    src/core/mod.cpp
    src/core/settings.cpp
    src/core/localization.cpp
    src/core/version_detect.cpp
    src/mem/scanner.cpp
    src/game/player.cpp
    src/game/teleport.cpp
    src/game/inventory.cpp
    src/game/world.cpp
    src/game/dye.cpp
    src/game/dye_profiles.cpp
    src/game/equipment.cpp
    src/game/equipment_logic.cpp
    src/game/friendly.cpp
    ${EXTENDED_SOURCES}
    src/game/item_names.cpp
    src/game/pak.cpp
    src/hooks/dx12_hook.cpp
    src/hooks/input.cpp
    src/hooks/xinput_hook.cpp
    src/hooks/dualsense.cpp
    src/gui/framework.cpp
    src/gui/widgets.cpp
    src/gui/icons.cpp
    src/gui/menu.cpp
    src/Trinity.rc               # VERSIONINFO -> legitimate PE metadata for AV/ML scoring
)

target_include_directories(Trinity PRIVATE src)
# Resolve #include "core/version.h" from the .rc via an explicit /I for the RC compiler.
target_include_directories(Trinity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(Trinity PRIVATE imgui minhook d3d12 dxgi dwmapi imm32 xinput9_1_0 version hid setupapi)
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
    add_executable(TrinityDyeProfileTests tests/dye_profiles_tests.cpp src/game/dye_profiles.cpp)
    target_include_directories(TrinityDyeProfileTests PRIVATE src)
    target_compile_definitions(TrinityDyeProfileTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME TrinityDyeProfileTests COMMAND TrinityDyeProfileTests)
    add_executable(TrinityOverlaySyncTests tests/overlay_sync_tests.cpp)
    target_include_directories(TrinityOverlaySyncTests PRIVATE src)
    target_compile_definitions(TrinityOverlaySyncTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(TrinityOverlaySyncTests PRIVATE d3d12 dxgi)
    add_test(NAME TrinityOverlaySyncTests COMMAND TrinityOverlaySyncTests)
    set_tests_properties(TrinityOverlaySyncTests PROPERTIES TIMEOUT 30)
    add_executable(TrinityDyeRecordTests tests/dye_record_tests.cpp)
    target_include_directories(TrinityDyeRecordTests PRIVATE src)
    add_test(NAME TrinityDyeRecordTests COMMAND TrinityDyeRecordTests)
    add_executable(TrinityInventoryScanTests tests/inventory_scan_tests.cpp)
    target_include_directories(TrinityInventoryScanTests PRIVATE src)
    target_compile_definitions(TrinityInventoryScanTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME TrinityInventoryScanTests COMMAND TrinityInventoryScanTests)
    add_executable(TrinityEquipmentTableTests
        tests/equipment_table_tests.cpp
        src/game/equipment_logic.cpp)
    target_include_directories(TrinityEquipmentTableTests PRIVATE src)
    target_compile_definitions(TrinityEquipmentTableTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME TrinityEquipmentTableTests COMMAND TrinityEquipmentTableTests)
    add_executable(TrinitySocketLayoutTests tests/socket_layout_tests.cpp)
    target_include_directories(TrinitySocketLayoutTests PRIVATE src)
    target_compile_definitions(TrinitySocketLayoutTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME TrinitySocketLayoutTests COMMAND TrinitySocketLayoutTests)
    add_executable(TrinityDamagePolicyTests tests/damage_policy_tests.cpp)
    target_include_directories(TrinityDamagePolicyTests PRIVATE src)
    add_test(NAME TrinityDamagePolicyTests COMMAND TrinityDamagePolicyTests)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME TrinityReloadSourceTests
            COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/reload_source_tests.py)
        add_test(NAME TrinityDyeEditorTests
            COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/dye_editor_tests.py)
        add_test(NAME TrinityCharacterIdentityTests
            COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/character_identity_tests.py)
        set_tests_properties(TrinityDyeEditorTests TrinityCharacterIdentityTests PROPERTIES TIMEOUT 90)
    endif()
endif()
