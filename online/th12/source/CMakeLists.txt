cmake_minimum_required(VERSION 3.20)
project(th12_coop LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "The TH12 co-op bootstrap targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The clean VS developer-shell build intentionally starts with an empty PATH.
# Some CMake/VS combinations then omit the normal Release flag defaults, which
# silently produces an unoptimized DLL. Keep the real-time patch deterministic
# and fast by making the intended Release flags explicit.
if (MSVC)
    add_compile_options($<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/Ob2>)
    add_compile_definitions($<$<CONFIG:Release>:NDEBUG>)
    add_link_options($<$<CONFIG:Release>:/INCREMENTAL:NO>
                     $<$<CONFIG:Release>:/OPT:REF>
                     $<$<CONFIG:Release>:/OPT:ICF>)
endif()

function(enable_project_warnings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endfunction()

add_library(player_context STATIC
    patch/player_context.cpp
)
target_include_directories(player_context PUBLIC patch)
enable_project_warnings(player_context)

add_library(resource_bank STATIC
    patch/resource_bank.cpp
)
target_include_directories(resource_bank PUBLIC patch)
enable_project_warnings(resource_bank)

add_library(player2_input STATIC
    patch/player2_input.cpp
)
target_include_directories(player2_input PUBLIC patch)
enable_project_warnings(player2_input)

add_library(frame_input STATIC
    patch/frame_input.cpp
)
target_include_directories(frame_input PUBLIC patch)
enable_project_warnings(frame_input)

add_library(script_input STATIC
    patch/script_input.cpp
)
target_include_directories(script_input PUBLIC patch)
enable_project_warnings(script_input)

add_library(lockstep_timeline STATIC
    patch/lockstep_timeline.cpp
)
target_include_directories(lockstep_timeline PUBLIC patch)
target_link_libraries(lockstep_timeline PUBLIC frame_input)
enable_project_warnings(lockstep_timeline)

add_library(determinism_trace STATIC
    patch/determinism_trace.cpp
)
target_include_directories(determinism_trace PUBLIC patch)
enable_project_warnings(determinism_trace)

add_library(collision_ownership STATIC
    patch/collision_ownership.cpp
)
target_include_directories(collision_ownership PUBLIC patch)
enable_project_warnings(collision_ownership)

add_library(coop_network STATIC
    patch/network_protocol.cpp
    patch/lan_session.cpp
)
target_include_directories(coop_network PUBLIC patch)
target_link_libraries(coop_network PUBLIC frame_input ws2_32)
enable_project_warnings(coop_network)

add_library(shared_resource STATIC
    patch/shared_resource.cpp
)
target_include_directories(shared_resource PUBLIC patch)
enable_project_warnings(shared_resource)

add_library(boss_health STATIC
    patch/boss_health.cpp
)
target_include_directories(boss_health PUBLIC patch)
enable_project_warnings(boss_health)

add_library(th12_coop SHARED
    patch/patch_dll.cpp
    patch/memory_patch.cpp
    patch/runtime_bomb.cpp
    patch/runtime_boss.cpp
    patch/runtime_collision.cpp
    patch/runtime_determinism.cpp
    patch/runtime_dialogue.cpp
    patch/runtime_frame_input.cpp
    patch/runtime_player2.cpp
    patch/runtime_resources.cpp
    patch/runtime_resource_tx.cpp
    patch/runtime_player_context.cpp
    patch/version_map.cpp
    common/path_util.cpp
    common/log.cpp
    common/sha256.cpp
)
target_include_directories(th12_coop PRIVATE common)
target_compile_definitions(th12_coop PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(th12_coop PRIVATE player_context resource_bank player2_input
    frame_input script_input lockstep_timeline determinism_trace collision_ownership coop_network)
target_link_libraries(th12_coop PRIVATE shared_resource)
target_link_libraries(th12_coop PRIVATE boss_health)
target_link_libraries(th12_coop PRIVATE bcrypt)
set_target_properties(th12_coop PROPERTIES OUTPUT_NAME "th12_coop")
target_link_options(th12_coop PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/patch/th12_coop.def")
enable_project_warnings(th12_coop)

add_library(dinput8_proxy SHARED
    proxy/dinput8_proxy.cpp
    common/path_util.cpp
    common/log.cpp
)
target_include_directories(dinput8_proxy PRIVATE common)
target_compile_definitions(dinput8_proxy PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX DIRECTINPUT_VERSION=0x0800)
set_target_properties(dinput8_proxy PROPERTIES OUTPUT_NAME "dinput8")
target_link_options(dinput8_proxy PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/proxy/dinput8.def")
enable_project_warnings(dinput8_proxy)

add_executable(coop_launcher WIN32
    launcher/launcher.cpp
    launcher/launcher.rc
    common/path_util.cpp
    common/log.cpp
    common/sha256.cpp
)
target_include_directories(coop_launcher PRIVATE common)
target_compile_definitions(coop_launcher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(coop_launcher PRIVATE bcrypt shell32 comctl32 ws2_32)
set_target_properties(coop_launcher PROPERTIES OUTPUT_NAME "coop-launcher")
enable_project_warnings(coop_launcher)

add_executable(proxy_smoke
    tests/proxy_smoke.cpp
)
target_compile_definitions(proxy_smoke PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX DIRECTINPUT_VERSION=0x0800)
set_target_properties(proxy_smoke PROPERTIES OUTPUT_NAME "proxy-smoke")
enable_project_warnings(proxy_smoke)

add_executable(shared_resource_tests
    tests/shared_resource_tests.cpp
)
target_link_libraries(shared_resource_tests PRIVATE shared_resource)
enable_project_warnings(shared_resource_tests)

enable_testing()
add_test(NAME proxy_load_chain
    COMMAND $<TARGET_FILE:proxy_smoke> $<TARGET_FILE:dinput8_proxy>
)
add_test(NAME shared_resource_transactions
    COMMAND $<TARGET_FILE:shared_resource_tests>
)

add_executable(boss_health_tests
    tests/boss_health_tests.cpp
)
target_link_libraries(boss_health_tests PRIVATE boss_health)
enable_project_warnings(boss_health_tests)
add_test(NAME boss_health_scaling
    COMMAND $<TARGET_FILE:boss_health_tests>
)

add_executable(player_context_tests
    tests/player_context_tests.cpp
)
target_link_libraries(player_context_tests PRIVATE player_context)
enable_project_warnings(player_context_tests)
add_test(NAME player_context_switching
    COMMAND $<TARGET_FILE:player_context_tests>
)

add_executable(resource_bank_tests
    tests/resource_bank_tests.cpp
)
target_link_libraries(resource_bank_tests PRIVATE resource_bank)
enable_project_warnings(resource_bank_tests)
add_test(NAME dual_resource_banks
    COMMAND $<TARGET_FILE:resource_bank_tests>
)

add_executable(player2_input_tests
    tests/player2_input_tests.cpp
)
target_link_libraries(player2_input_tests PRIVATE player2_input)
enable_project_warnings(player2_input_tests)
add_test(NAME player2_input_mapping
    COMMAND $<TARGET_FILE:player2_input_tests>
)

add_executable(frame_input_tests
    tests/frame_input_tests.cpp
)
target_link_libraries(frame_input_tests PRIVATE frame_input)
enable_project_warnings(frame_input_tests)
add_test(NAME frame_input_timeline
    COMMAND $<TARGET_FILE:frame_input_tests>
)

add_executable(script_input_tests
    tests/script_input_tests.cpp
)
target_link_libraries(script_input_tests PRIVATE script_input)
enable_project_warnings(script_input_tests)
add_test(NAME script_input_projection
    COMMAND $<TARGET_FILE:script_input_tests>
)

add_executable(lockstep_timeline_tests
    tests/lockstep_timeline_tests.cpp
)
target_link_libraries(lockstep_timeline_tests PRIVATE lockstep_timeline)
enable_project_warnings(lockstep_timeline_tests)
add_test(NAME lockstep_timeline
    COMMAND $<TARGET_FILE:lockstep_timeline_tests>
)

add_executable(network_protocol_tests
    tests/network_protocol_tests.cpp
)
target_link_libraries(network_protocol_tests PRIVATE coop_network)
enable_project_warnings(network_protocol_tests)
add_test(NAME network_protocol
    COMMAND $<TARGET_FILE:network_protocol_tests>
)

add_executable(lan_session_tests
    tests/lan_session_tests.cpp
)
target_link_libraries(lan_session_tests PRIVATE coop_network)
enable_project_warnings(lan_session_tests)
add_test(NAME lan_session_loopback
    COMMAND $<TARGET_FILE:lan_session_tests>
)

add_executable(determinism_trace_tests
    tests/determinism_trace_tests.cpp
)
target_link_libraries(determinism_trace_tests PRIVATE determinism_trace)
enable_project_warnings(determinism_trace_tests)
add_test(NAME determinism_trace_format
    COMMAND $<TARGET_FILE:determinism_trace_tests>
)

add_executable(collision_ownership_tests
    tests/collision_ownership_tests.cpp
)
target_link_libraries(collision_ownership_tests PRIVATE collision_ownership)
enable_project_warnings(collision_ownership_tests)
add_test(NAME collision_ownership_rules
    COMMAND $<TARGET_FILE:collision_ownership_tests>
)

add_executable(phase7_compare
    tools/phase7_compare.cpp
)
target_link_libraries(phase7_compare PRIVATE determinism_trace)
enable_project_warnings(phase7_compare)
set_target_properties(phase7_compare PROPERTIES OUTPUT_NAME "phase7-compare")

add_executable(sht_inspect
    research/tools/sht_inspect.cpp
)
enable_project_warnings(sht_inspect)
set_target_properties(sht_inspect PROPERTIES OUTPUT_NAME "sht-inspect")

set(PLAYER_SHT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/research/extracted")
foreach(sht_file IN ITEMS pl00a.sht pl00b.sht pl01a.sht pl01b.sht pl02a.sht pl02b.sht)
    add_test(NAME "inspect_${sht_file}"
        COMMAND $<TARGET_FILE:sht_inspect> --check "${PLAYER_SHT_DIR}/${sht_file}"
    )
endforeach()

install(TARGETS coop_launcher dinput8_proxy th12_coop
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
    ARCHIVE DESTINATION .
)
