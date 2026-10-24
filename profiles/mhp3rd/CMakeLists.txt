set(MHP3RD_PROFILE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# The AOT corpus is derived from the user's executable and is not committed.
# Until scripts/generate.sh has produced it, link an empty registry instead.
if(PSPRECOMP_COVERAGE)
    # Public coverage never compiles the user's game-derived corpus.
    set(MHP3RD_GENERATED)
else()
    file(GLOB MHP3RD_GENERATED CONFIGURE_DEPENDS "${MHP3RD_PROFILE_DIR}/generated/*.cpp")
endif()
if(MHP3RD_GENERATED)
    list(LENGTH MHP3RD_GENERATED MHP3RD_GENERATED_COUNT)
    message(STATUS "mhp3rd: ${MHP3RD_GENERATED_COUNT} generated AOT units")
    if(MSVC)
        if(PSPRECOMP_GENERATED_OPT_LEVEL STREQUAL "0")
            set(MHP3RD_GENERATED_OPTIONS "/Od;/bigobj")
        else()
            set(MHP3RD_GENERATED_OPTIONS "/O${PSPRECOMP_GENERATED_OPT_LEVEL};/Ob0;/bigobj")
        endif()
    else()
        set(MHP3RD_GENERATED_OPTIONS "-O${PSPRECOMP_GENERATED_OPT_LEVEL};-g0")
    endif()
    set_source_files_properties(${MHP3RD_GENERATED} PROPERTIES COMPILE_OPTIONS "${MHP3RD_GENERATED_OPTIONS}")

    # The analog camera wraps the game's camera rotation helper, and main.cpp
    # has to hand it the generated unit that helper was compiled into. Which
    # unit that is depends on how the generator partitions the code, so read it
    # from the corpus instead of naming it: a new partition then finds its own
    # unit, and a corpus without the helper stops the build here rather than
    # letting the camera call the wrong code in game.
    set(MHP3RD_CAMERA_HELPER_UNIT "")
    foreach(unit IN LISTS MHP3RD_GENERATED)
        file(STRINGS "${unit}" hit REGEX "register_function\\(0x08878B70u, &recomp_unit_[0-9]+," LIMIT_COUNT 1)
        if(hit)
            string(REGEX MATCH "recomp_unit_[0-9]+" MHP3RD_CAMERA_HELPER_UNIT "${hit}")
            break()
        endif()
    endforeach()
    if(NOT MHP3RD_CAMERA_HELPER_UNIT)
        message(FATAL_ERROR "mhp3rd: no generated unit registers the camera rotation helper 0x08878B70; "
                            "regenerate the corpus from the supported executable, or update "
                            "host/camera/game_camera.cpp if the helper has moved")
    endif()
    message(STATUS "mhp3rd: camera rotation helper in ${MHP3RD_CAMERA_HELPER_UNIT}")

    # Hiding the HUD (host/gpu/game_hud.cpp) wraps the game's 2D functions the
    # same way, so their units are read from the corpus too, in one pass.
    set(MHP3RD_HUD_FUNCTIONS
        08876874 088EA9B0 088EB07C 088EBF0C 088EC060 088EC0F8 088EC1F0 088EC2EC 088EC3E0
        088EC63C 088ECB74 088ECC38 088ECCE4 088ECD9C 088ECE58)
    list(JOIN MHP3RD_HUD_FUNCTIONS "|" hud_pattern)
    set(hud_lines "")
    foreach(unit IN LISTS MHP3RD_GENERATED)
        file(STRINGS "${unit}" hits REGEX "register_function\\(0x(${hud_pattern})u, &recomp_unit_[0-9]+,")
        foreach(hit IN LISTS hits)
            string(REGEX MATCH "0x[0-9A-F]+u, &recomp_unit_[0-9]+" pair "${hit}")
            string(REPLACE ", &" ", " pair "${pair}")
            string(APPEND hud_lines "MHP3RD_HUD_FUNCTION(${pair})\n")
        endforeach()
    endforeach()
    set(MHP3RD_HUD_INC "${CMAKE_CURRENT_BINARY_DIR}/generated_hooks/hud_functions.inc")
    file(CONFIGURE OUTPUT "${MHP3RD_HUD_INC}" CONTENT "${hud_lines}" @ONLY)

    # Layered armor (host/game/layered_armor_hook.cpp) stands in for the game's
    # lookup of an armor part's model and wraps the hunter's load step, so
    # their units are read from the corpus too.
    set(MHP3RD_LAYERED_FUNCTIONS 08869778 088A5564)
    list(JOIN MHP3RD_LAYERED_FUNCTIONS "|" layered_pattern)
    set(layered_lines "")
    foreach(unit IN LISTS MHP3RD_GENERATED)
        file(STRINGS "${unit}" hits REGEX "register_function\\(0x(${layered_pattern})u, &recomp_unit_[0-9]+,")
        foreach(hit IN LISTS hits)
            string(REGEX MATCH "0x[0-9A-F]+u, &recomp_unit_[0-9]+" pair "${hit}")
            string(REPLACE ", &" ", " pair "${pair}")
            string(APPEND layered_lines "MHP3RD_LAYERED_FUNCTION(${pair})\n")
        endforeach()
    endforeach()
    set(MHP3RD_LAYERED_INC "${CMAKE_CURRENT_BINARY_DIR}/generated_hooks/layered_armor_functions.inc")
    file(CONFIGURE OUTPUT "${MHP3RD_LAYERED_INC}" CONTENT "${layered_lines}" @ONLY)
    # A regenerated corpus with the same file names must be read again.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MHP3RD_GENERATED})
else()
    message(STATUS "mhp3rd: no generated AOT units; run profiles/mhp3rd/scripts/generate.sh")
    set(MHP3RD_GENERATED host/generated_stub.cpp)
endif()

# Renderer: SDL3 for the window and Vulkan for drawing. Both are optional so the
# profile still builds headless on a machine without them.
option(MHP3RD_RENDERER "Build the Vulkan renderer" ON)
set(MHP3RD_RENDERER_SOURCES)
if(MHP3RD_RENDERER)
    find_package(SDL3 QUIET)
    find_package(Vulkan QUIET)
    find_program(MHP3RD_GLSLANG NAMES glslangValidator glslang)
    if(SDL3_FOUND AND Vulkan_FOUND AND MHP3RD_GLSLANG)
        set(MHP3RD_SHADER_INC "${CMAKE_CURRENT_BINARY_DIR}/generated_shaders/ge_shaders.inc")
        set(MHP3RD_SHADERS
            "kGeVertexShader=${MHP3RD_PROFILE_DIR}/host/gpu/shaders/ge.vert"
            "kGeRawVertexShader=GE_RAW_VERTICES@${MHP3RD_PROFILE_DIR}/host/gpu/shaders/ge.vert"
            "kGeCheckVertexShader=GE_RAW_VERTICES,GE_CHECK_DECODE@${MHP3RD_PROFILE_DIR}/host/gpu/shaders/ge.vert"
            "kGeFragmentShader=${MHP3RD_PROFILE_DIR}/host/gpu/shaders/ge.frag"
            "kGeFragmentShaderPlain=GE_NO_SPECIALIZATION@${MHP3RD_PROFILE_DIR}/host/gpu/shaders/ge.frag")
        if(ANDROID)
            # Pre-rotation of the finished frame for a display turned sideways.
            list(APPEND MHP3RD_SHADERS
                "kRotateVertexShader=${MHP3RD_PROFILE_DIR}/host/gpu/shaders/rotate.vert"
                "kRotateFragmentShader=${MHP3RD_PROFILE_DIR}/host/gpu/shaders/rotate.frag")
        endif()
        set(MHP3RD_SHADER_SOURCES ${MHP3RD_SHADERS})
        list(TRANSFORM MHP3RD_SHADER_SOURCES REPLACE "^[A-Za-z]+=([A-Z_,]+@)?" "")
        list(REMOVE_DUPLICATES MHP3RD_SHADER_SOURCES)
        add_custom_command(
            OUTPUT "${MHP3RD_SHADER_INC}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated_shaders"
            COMMAND python3 "${MHP3RD_PROFILE_DIR}/tools/embed_shaders.py" "${MHP3RD_GLSLANG}" "${MHP3RD_SHADER_INC}"
                    ${MHP3RD_SHADERS}
            DEPENDS ${MHP3RD_SHADER_SOURCES}
                    "${MHP3RD_PROFILE_DIR}/tools/embed_shaders.py"
            COMMENT "Compiling GE shaders to SPIR-V")
        add_custom_target(mhp3rd_shaders DEPENDS "${MHP3RD_SHADER_INC}")
        # Dear ImGui (third_party/imgui, unmodified) draws the port's own
        # interface over the game.
        set(MHP3RD_RENDERER_SOURCES
            host/gpu/descriptor_pools.cpp
            host/gpu/device_report.cpp
            host/gpu/frame_interpolation.cpp
            host/gpu/frame_pacing.cpp
            host/gpu/replacement_textures.cpp
            host/gpu/mmpx.cpp
            host/gpu/ui_textures.cpp
            host/gpu/vulkan_renderer.cpp
            host/input/gamepad_devices.cpp
            host/ui/bindings_editor.cpp
            host/ui/controllers_screen.cpp
            host/ui/file_browser.cpp
            host/ui/font_menu.cpp
            host/ui/input_script.cpp
            host/ui/layer.cpp
            host/ui/menu.cpp
            host/ui/layered_armor_screen.cpp
            host/ui/mods_screen.cpp
            host/ui/save_screen.cpp
            host/ui/setup_screens.cpp
            host/ui/text_input.cpp
            host/ui/touch_editor.cpp
            host/ui/touch_overlay.cpp
            host/ui/texture_pack_screen.cpp
            host/ui/widgets.cpp
            third_party/imgui/imgui.cpp
            third_party/imgui/imgui_draw.cpp
            third_party/imgui/imgui_tables.cpp
            third_party/imgui/imgui_widgets.cpp
            third_party/imgui/backends/imgui_impl_sdl3.cpp
            third_party/imgui/backends/imgui_impl_vulkan.cpp)
        message(STATUS "mhp3rd: Vulkan renderer enabled")
    else()
        if(PSPRECOMP_COVERAGE)
            message(FATAL_ERROR "Requested renderer coverage requires SDL3, Vulkan and glslang")
        endif()
        message(STATUS "mhp3rd: renderer disabled (SDL3, Vulkan or glslang not found)")
        set(MHP3RD_RENDERER OFF)
    endif()
endif()

# The build the in-game menu shows: `git describe` of the checkout, refreshed
# on every build but rewritten only when it changes, so only the menu
# recompiles after a commit.
set(MHP3RD_VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated_version/yakumo_version.hpp")
add_custom_target(mhp3rd_version
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DOUTPUT=${MHP3RD_VERSION_HEADER}
            -P "${MHP3RD_PROFILE_DIR}/tools/write_version.cmake"
    BYPRODUCTS "${MHP3RD_VERSION_HEADER}"
    COMMENT "Checking the build version")

# FFmpeg decodes the game's music and its movies; by default it is built with
# the project (see cmake/FFmpeg.cmake).
include("${MHP3RD_PROFILE_DIR}/cmake/FFmpeg.cmake")

# GE parsing, texture decoding and the audio mixer are backend independent and
# always built; only the device the sink opens depends on SDL.
set(MHP3RD_HOST_SOURCES
    host/app_paths.cpp
    host/audio/atrac_decoder.cpp
    host/movie/avc_decoder.cpp
    host/movie/psmf_demuxer.cpp
    host/audio/audio_sink.cpp
    host/audio/sas_core.cpp
    host/camera_probe.cpp
    host/camera/camera_input.cpp
    host/camera/game_aspect.cpp
    host/camera/game_camera.cpp
    host/camera/lock_on.cpp
    host/camera/free_camera.cpp
    host/gpu/game_hud.cpp
    host/gpu/ge_state.cpp
    host/gpu/texture_decode.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/gamepad_mapping.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/gpu/texture_pack.cpp
    host/gpu/texture_pack_import.cpp
    host/gpu/screenshot.cpp
    host/perf/frame_stats.cpp
    host/perf/perf_overlay.cpp
    host/platform/utf8_path.cpp
    host/overlays.cpp
    host/mhp3rd_profile.cpp
    host/kernel/kernel.cpp
    host/kernel/load_trace.cpp
    host/kernel/fast_loading.cpp
    host/kernel/fast_forward.cpp
    host/kernel/fast_forward_switch.cpp
    host/kernel/load_detector.cpp
    host/kernel/iso_image.cpp
    host/hle/hle_common.cpp
    host/hle/hle_threadman.cpp
    host/hle/hle_sysmem.cpp
    host/hle/hle_io.cpp
    host/hle/hle_system.cpp
    host/hle/hle_media.cpp
    host/audio/guest_pcm.cpp
    host/hle/hle_atrac.cpp
    host/hle/hle_mpeg.cpp
    host/fonts/game_font.cpp
    host/hle/hle_font.cpp
    host/hle/hle_utility.cpp
    host/hle/hle_savedata.cpp
    host/hle/hle_adhoc.cpp
    host/adhoc/client.cpp
    host/adhoc/discovery.cpp
    host/adhoc/host.cpp
    host/adhoc/server.cpp
    host/save_data/aes128.cpp
    host/save_data/param_sfo.cpp
    host/save_data/savedata_crypto.cpp
    host/save_data/savedata_store.cpp
    host/save_data/save_transfer.cpp
    host/install/executable_preparation.cpp
    host/install/front_end.cpp
    host/install/installer.cpp
    host/install/installer_dialogs.cpp
    host/install/user_data.cpp
    host/settings/settings.cpp
    host/mods/file_overlay.cpp
    host/mods/mod_import.cpp
    host/mods/mod_ini.cpp
    host/mods/mod_library.cpp
    host/mods/mod_session.cpp
    host/mods/mhp3rd_data_bin.cpp
    host/mods/mhp3rd_mod_format.cpp
    host/mods/mhp3rd_mods.cpp
    host/game/guest_ram.cpp
    host/game/game_data.cpp
    host/game/equipment_models.cpp
    host/game/layered_armor.cpp
    host/game/layered_armor_hook.cpp
    third_party/tiny_aes/aes.c
)
# tiny-AES-c is C; the project only enables C++, which compiles it unchanged.
set_source_files_properties(third_party/tiny_aes/aes.c PROPERTIES LANGUAGE CXX)

# The recompiled game code gets a target of its own. It needs only the
# psprecomp headers, so definitions and include paths the host adds to
# Yakumo (a renderer switch, a new library's headers) do not change its
# compile commands and rebuild all of it. Its objects still link straight into
# the executable, which exports their symbols to the overlay libraries.
add_library(mhp3rd_generated OBJECT ${MHP3RD_GENERATED})
target_include_directories(mhp3rd_generated PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${MHP3RD_PROFILE_DIR}/generated")
set_target_properties(mhp3rd_generated PROPERTIES JOB_POOL_COMPILE psprecomp_generated)

# An Android app is a shared library, libmain.so, that SDL's Java activity
# loads and calls; the command-line executable still builds for Android without
# this and runs from adb shell.
option(MHP3RD_ANDROID_APP "Build the Android app's libmain.so instead of an executable" OFF)
if(MHP3RD_ANDROID_APP AND NOT (ANDROID AND MHP3RD_RENDERER))
    message(FATAL_ERROR "MHP3RD_ANDROID_APP needs the Android NDK toolchain and the renderer (SDL3 and Vulkan)")
endif()
set(MHP3RD_PROGRAM_SOURCES
    host/main.cpp
    ${MHP3RD_HOST_SOURCES}
    ${MHP3RD_RENDERER_SOURCES}
    $<TARGET_OBJECTS:mhp3rd_generated>)
if(MHP3RD_ANDROID_APP)
    add_library(Yakumo SHARED ${MHP3RD_PROGRAM_SOURCES} host/platform/android_app.cpp
        host/platform/android_jni.cpp host/platform/android_documents.cpp host/platform/android_performance.cpp
        host/platform/android_fatal.cpp)
    target_compile_definitions(Yakumo PRIVATE MHP3RD_ANDROID_APP=1)
    set_target_properties(Yakumo PROPERTIES
        OUTPUT_NAME main
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
else()
    add_executable(Yakumo ${MHP3RD_PROGRAM_SOURCES})
endif()
if(WIN32)
    set(MHP3RD_WINDOWS_ICON "${MHP3RD_PROFILE_DIR}/packaging/windows/emblem.ico")
    configure_file(packaging/windows/Yakumo.rc.in Yakumo.rc @ONLY)
    target_sources(Yakumo PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Yakumo.rc")
endif()
if(MHP3RD_RENDERER)
    add_dependencies(Yakumo mhp3rd_shaders mhp3rd_version)
    # The sink opens its own SDL audio subsystem, so it only needs SDL to exist.
    target_compile_definitions(Yakumo PRIVATE MHP3RD_HAS_RENDERER=1 MHP3RD_HAS_SDL_AUDIO=1 MHP3RD_HAS_SDL=1)
    target_include_directories(Yakumo PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated_shaders"
        "${CMAKE_CURRENT_BINARY_DIR}/generated_version"
        "${MHP3RD_PROFILE_DIR}/third_party/imgui")
    target_link_libraries(Yakumo PRIVATE SDL3::SDL3 Vulkan::Vulkan)
endif()
# The host only: the generated code never sees FFmpeg.
mhp3rd_use_ffmpeg(Yakumo)
target_include_directories(Yakumo PRIVATE
    "${MHP3RD_PROFILE_DIR}/host"
    "${MHP3RD_PROFILE_DIR}/third_party"
    "${MHP3RD_PROFILE_DIR}/generated")
target_link_libraries(Yakumo PRIVATE psprecomp_core ${CMAKE_DL_LIBS})
if(WIN32)
    # The ad hoc client's and server's sockets, and the list of interfaces;
    # CommandLineToArgvW for the arguments as UTF-8.
    target_link_libraries(Yakumo PRIVATE ws2_32 iphlpapi shell32)
endif()
find_package(Threads REQUIRED)
target_link_libraries(Yakumo PRIVATE Threads::Threads)
# The overlay libraries resolve psprecomp and runtime symbols against the
# executable that loads them, so it has to export them.
set_target_properties(Yakumo PROPERTIES
    ENABLE_EXPORTS TRUE
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
# Only main.cpp needs the camera helper's unit; keeping it off the target keeps
# it off every other unit's command line.
if(MHP3RD_CAMERA_HELPER_UNIT)
    set_property(SOURCE host/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        MHP3RD_CAMERA_HELPER_UNIT=${MHP3RD_CAMERA_HELPER_UNIT})
endif()
if(MHP3RD_HUD_INC)
    target_compile_definitions(Yakumo PRIVATE
        "MHP3RD_HUD_FUNCTIONS_INC=\"${MHP3RD_HUD_INC}\"")
endif()
if(MHP3RD_LAYERED_INC)
    target_compile_definitions(Yakumo PRIVATE
        "MHP3RD_LAYERED_FUNCTIONS_INC=\"${MHP3RD_LAYERED_INC}\"")
endif()
# The NID table is compiled in, so the program reads nothing from the checkout
# it was built in.
set(MHP3RD_NIDS_CSV "${PROJECT_SOURCE_DIR}/configs/nids.csv")
set(MHP3RD_NIDS_INC "${CMAKE_CURRENT_BINARY_DIR}/generated_nids/nid_table.inc")
add_custom_command(
    OUTPUT "${MHP3RD_NIDS_INC}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${MHP3RD_NIDS_CSV} -DOUTPUT=${MHP3RD_NIDS_INC}
            -P "${MHP3RD_PROFILE_DIR}/tools/embed_nids.cmake"
    DEPENDS "${MHP3RD_NIDS_CSV}" "${MHP3RD_PROFILE_DIR}/tools/embed_nids.cmake"
    COMMENT "Embedding the NID table")
target_sources(Yakumo PRIVATE "${MHP3RD_NIDS_INC}")
target_include_directories(Yakumo PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated_nids")

# A release build runs from wherever it is unpacked: it has no fallback to
# this checkout's game directory, and on Linux it finds the libraries it ships
# in lib/ next to the executable and nothing else.
option(MHP3RD_RELEASE "Build for distribution: no checkout paths, libraries from lib/ next to the executable" OFF)
if(NOT MHP3RD_RELEASE)
    # The checkout's profiles/mhp3rd/game, a developer fallback after the
    # per-user data directory. Only main.cpp reads it; on the target it would be
    # on every generated unit's command line, so moving the checkout would
    # rebuild all of them and no other checkout could reuse their cache entries.
    set_property(SOURCE host/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        MHP3RD_DEFAULT_GAME_DIR="${MHP3RD_PROFILE_DIR}/game")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(Yakumo PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/lib")
    # The C++ runtime is linked in statically, so a build made against an old
    # glibc runs wherever that glibc does, and its symbols stay private: the
    # executable exports its own symbols to the overlay libraries, and a
    # second, exported libstdc++ would clash with the system's one that the
    # graphics driver loads.
    set(MHP3RD_RELEASE_LINK_OPTIONS -static-libstdc++ "LINKER:--exclude-libs,libstdc++.a:libsupc++.a")
    target_link_options(Yakumo PRIVATE ${MHP3RD_RELEASE_LINK_OPTIONS})
endif()
set_target_properties(Yakumo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    JOB_POOL_COMPILE psprecomp_generated)

# Developer tools (host/debug): the menu's Debug page, with cheats for testing,
# and the command file that drives it from a script. Never in a release build:
# MHP3RD_RELEASE compiles all of it out whatever this option says. A developer
# build still shows it only when MHP3RD_DEBUG_MENU=1 is set at run time.
option(MHP3RD_DEBUG_MENU "Compile the developer tools (Debug page) into non-release builds" ON)
if(MHP3RD_DEBUG_MENU AND NOT MHP3RD_RELEASE)
    target_sources(Yakumo PRIVATE
        host/debug/debug_console.cpp
        host/debug/debug_tools.cpp
        host/debug/game_state.cpp
        host/debug/quest_start.cpp)
    if(MHP3RD_RENDERER)
        target_sources(Yakumo PRIVATE host/ui/debug_screen.cpp)
    endif()
    target_compile_definitions(Yakumo PRIVATE MHP3RD_DEBUG_MENU=1)
    message(STATUS "mhp3rd: developer tools compiled in (shown with MHP3RD_DEBUG_MENU=1)")

    # The game's structures as the tools read and write them, on a buffer.
    add_executable(mhp3rd_debug_tools_tests
        tests/debug_tools_tests.cpp
        host/debug/game_state.cpp
        host/debug/quest_start.cpp
        host/game/game_data.cpp)
    target_include_directories(mhp3rd_debug_tools_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
    set_target_properties(mhp3rd_debug_tools_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(PSPRECOMP_BUILD_TESTS)
        add_test(NAME mhp3rd_debug_tools_tests COMMAND mhp3rd_debug_tools_tests)
    endif()
else()
    message(STATUS "mhp3rd: developer tools not compiled in")
endif()

# Save-data checks that need no game data: crypto test vectors and round trips.
add_executable(mhp3rd_savedata_tests
    tests/savedata_tests.cpp
    host/save_data/aes128.cpp
    host/save_data/param_sfo.cpp
    host/save_data/savedata_crypto.cpp
    host/save_data/savedata_store.cpp
    host/save_data/save_transfer.cpp
    host/platform/utf8_path.cpp
    third_party/tiny_aes/aes.c)
target_include_directories(mhp3rd_savedata_tests PRIVATE
    "${MHP3RD_PROFILE_DIR}/host"
    "${MHP3RD_PROFILE_DIR}/third_party")
set_target_properties(mhp3rd_savedata_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_savedata_tests COMMAND mhp3rd_savedata_tests)
endif()

# Camera dispatch, ownership and integration checks without game data.
add_executable(mhp3rd_camera_tests
    tests/analog_camera_tests.cpp
    host/camera/camera_input.cpp
    host/camera/game_camera.cpp
    host/camera/lock_on.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhp3rd_camera_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
target_link_libraries(mhp3rd_camera_tests PRIVATE psprecomp_core)
set_target_properties(mhp3rd_camera_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_camera_tests COMMAND mhp3rd_camera_tests)
endif()

# Lock-on: which monster a tap picks, and how it turns the game's camera.
add_executable(mhp3rd_lock_on_tests
    tests/lock_on_tests.cpp
    host/camera/camera_input.cpp
    host/camera/game_camera.cpp
    host/camera/lock_on.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhp3rd_lock_on_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
target_link_libraries(mhp3rd_lock_on_tests PRIVATE psprecomp_core)
set_target_properties(mhp3rd_lock_on_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_lock_on_tests COMMAND mhp3rd_lock_on_tests)
endif()

# The free camera's geometry, and that it hooks nothing while off.
add_executable(mhp3rd_free_camera_tests
    tests/free_camera_tests.cpp
    host/camera/free_camera.cpp
    host/settings/settings.cpp
    host/install/user_data.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/app_paths.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhp3rd_free_camera_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
target_link_libraries(mhp3rd_free_camera_tests PRIVATE psprecomp_core)
set_target_properties(mhp3rd_free_camera_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_free_camera_tests COMMAND mhp3rd_free_camera_tests)
endif()

# The game's view shape: what is written to guest memory, and what is left.
add_executable(mhp3rd_aspect_tests
    tests/game_aspect_tests.cpp
    host/camera/game_aspect.cpp)
target_include_directories(mhp3rd_aspect_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
target_link_libraries(mhp3rd_aspect_tests PRIVATE psprecomp_core)
set_target_properties(mhp3rd_aspect_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_aspect_tests COMMAND mhp3rd_aspect_tests)
endif()

# Each platform's settings defaults.
add_executable(mhp3rd_settings_tests
    tests/settings_defaults_tests.cpp
    host/settings/settings.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhp3rd_settings_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
target_link_libraries(mhp3rd_settings_tests PRIVATE psprecomp_core)
set_target_properties(mhp3rd_settings_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_settings_tests COMMAND mhp3rd_settings_tests)
endif()

# Where the data directory is (portable or per-user) and the copy of an
# installed copy's data into a portable folder. No game data.
add_executable(mhp3rd_data_directory_tests
    tests/data_directory_tests.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhp3rd_data_directory_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
target_link_libraries(mhp3rd_data_directory_tests PRIVATE psprecomp_core)
set_target_properties(mhp3rd_data_directory_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_data_directory_tests COMMAND mhp3rd_data_directory_tests)
endif()

# Screenshots: their names, and the PNG files written. Works in a temporary
# directory; no game data.
add_executable(mhp3rd_screenshot_tests
    tests/screenshot_tests.cpp
    host/gpu/screenshot.cpp
    host/install/user_data.cpp
    host/app_paths.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhp3rd_screenshot_tests PRIVATE
    "${MHP3RD_PROFILE_DIR}/host"
    "${MHP3RD_PROFILE_DIR}/third_party")
target_link_libraries(mhp3rd_screenshot_tests PRIVATE psprecomp_core Threads::Threads)
set_target_properties(mhp3rd_screenshot_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_screenshot_tests COMMAND mhp3rd_screenshot_tests)
endif()

# Fast loading's decision: when a load runs ahead of real time.
add_executable(mhp3rd_fast_loading_tests
    tests/fast_loading_tests.cpp
    host/kernel/load_detector.cpp)
target_include_directories(mhp3rd_fast_loading_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
set_target_properties(mhp3rd_fast_loading_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_fast_loading_tests COMMAND mhp3rd_fast_loading_tests)
endif()

# Fast-forward's switch: hold, toggle, and what keeps real time.
add_executable(mhp3rd_fast_forward_tests
    tests/fast_forward_tests.cpp
    host/kernel/fast_forward_switch.cpp)
target_include_directories(mhp3rd_fast_forward_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
set_target_properties(mhp3rd_fast_forward_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_fast_forward_tests COMMAND mhp3rd_fast_forward_tests)
endif()

# The on-screen touch controls: layout, fingers and the stick's maths.
add_executable(mhp3rd_touch_tests
    tests/touch_controls_tests.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp)
target_include_directories(mhp3rd_touch_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
set_target_properties(mhp3rd_touch_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_touch_tests COMMAND mhp3rd_touch_tests)
endif()

# Texture pack import: finding a pack in the layouts packs come in, the
# checks made before copying, and the copy, swap and backup; and the PSP's DXT
# block layout. No game data.
add_executable(mhp3rd_texture_pack_tests
    tests/texture_pack_tests.cpp
    host/gpu/texture_decode.cpp
    host/gpu/texture_pack.cpp
    host/gpu/texture_pack_import.cpp
    host/platform/utf8_path.cpp)
target_include_directories(mhp3rd_texture_pack_tests PRIVATE
    "${MHP3RD_PROFILE_DIR}/host"
    "${MHP3RD_PROFILE_DIR}/host/gpu"
    "${MHP3RD_PROFILE_DIR}/third_party")
target_link_libraries(mhp3rd_texture_pack_tests PRIVATE psprecomp_core Threads::Threads)
set_target_properties(mhp3rd_texture_pack_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_texture_pack_tests COMMAND mhp3rd_texture_pack_tests)
endif()

# Keyboard and mouse bindings: names, settings.ini spelling, the menu's
# assignment rules and what held keys press. No SDL or game data.
add_executable(mhp3rd_input_tests
    tests/input_tests.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp)
target_include_directories(mhp3rd_input_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
set_target_properties(mhp3rd_input_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_input_tests COMMAND mhp3rd_input_tests)
endif()

# Mappings for gamepads SDL does not know (#147): the setup's detection and
# the mapping lines and file it writes. No SDL.
add_executable(mhp3rd_gamepad_mapping_tests
    tests/gamepad_mapping_tests.cpp
    host/input/gamepad_mapping.cpp)
target_include_directories(mhp3rd_gamepad_mapping_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
set_target_properties(mhp3rd_gamepad_mapping_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_gamepad_mapping_tests COMMAND mhp3rd_gamepad_mapping_tests)
endif()

# Mods: the community's mod folders, choices, conflicts and import, the
# DATA.BIN obfuscation and an archive served with mods' files. No game data.
add_executable(mhp3rd_mods_tests
    tests/mods_tests.cpp
    host/mods/file_overlay.cpp
    host/mods/mhp3rd_data_bin.cpp
    host/mods/mhp3rd_mod_format.cpp
    host/mods/mod_import.cpp
    host/mods/mod_ini.cpp
    host/mods/mod_library.cpp
    host/mods/mod_session.cpp)
target_include_directories(mhp3rd_mods_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
set_target_properties(mhp3rd_mods_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_mods_tests COMMAND mhp3rd_mods_tests)
endif()

# Which file holds a piece of equipment's model, read from the game's tables
# as equipment mods need it, on a buffer standing for guest memory. No game data.
add_executable(mhp3rd_equipment_models_tests
    tests/equipment_models_tests.cpp
    host/game/game_data.cpp
    host/game/equipment_models.cpp
    host/game/layered_armor.cpp)
target_include_directories(mhp3rd_equipment_models_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
set_target_properties(mhp3rd_equipment_models_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_equipment_models_tests COMMAND mhp3rd_equipment_models_tests)
endif()

# Renderer helpers that need no GPU.
# MMPX, the 2D interface's pixel-art upscaler (host/gpu/mmpx.hpp).
add_executable(mhp3rd_mmpx_tests tests/mmpx_tests.cpp host/gpu/mmpx.cpp)
target_include_directories(mhp3rd_mmpx_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
set_target_properties(mhp3rd_mmpx_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_mmpx_tests COMMAND mhp3rd_mmpx_tests)
endif()

# Descriptor pools counted the way a phone's driver counts them: growth,
# freeing, a driver that refuses early, the pool limit. No GPU is used.
if(Vulkan_FOUND)
    add_executable(mhp3rd_descriptor_pool_tests tests/descriptor_pool_tests.cpp host/gpu/descriptor_pools.cpp)
    target_include_directories(mhp3rd_descriptor_pool_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host"
                                                                     ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(mhp3rd_descriptor_pool_tests PRIVATE Vulkan::Vulkan)
    set_target_properties(mhp3rd_descriptor_pool_tests PROPERTIES
                          RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(PSPRECOMP_BUILD_TESTS)
        add_test(NAME mhp3rd_descriptor_pool_tests COMMAND mhp3rd_descriptor_pool_tests)
    endif()
endif()

add_executable(mhp3rd_ge_state_tests tests/ge_state_tests.cpp host/gpu/ge_state.cpp)
target_include_directories(mhp3rd_ge_state_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
target_link_libraries(mhp3rd_ge_state_tests PRIVATE psprecomp_core)
set_target_properties(mhp3rd_ge_state_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_ge_state_tests COMMAND mhp3rd_ge_state_tests)
    add_custom_target(mhp3rd_ge_state_slow_tests DEPENDS mhp3rd_ge_state_tests)
    add_test(NAME mhp3rd_ge_state_slow_tests COMMAND mhp3rd_ge_state_tests)
    set_tests_properties(mhp3rd_ge_state_slow_tests PROPERTIES ENVIRONMENT "MHP3RD_NO_FAST_DECODE=1")
    add_custom_target(mhp3rd_ge_state_checked_tests DEPENDS mhp3rd_ge_state_tests)
    add_test(NAME mhp3rd_ge_state_checked_tests COMMAND mhp3rd_ge_state_tests)
    set_tests_properties(mhp3rd_ge_state_checked_tests PROPERTIES
        ENVIRONMENT "MHP3RD_CHECK_DECODE=1;MHP3RD_TRACE_MATERIAL=1;MHP3RD_TRACE_LIGHTING=1;MHP3RD_TRACE_FB_TEXTURES=1"
        TIMEOUT 30 FAIL_REGULAR_EXPRESSION "FAIL:;\\[decode-check\\].*differs over")
endif()

add_executable(mhp3rd_texture_decode_tests
    tests/texture_decode_tests.cpp
    host/gpu/texture_decode.cpp)
target_include_directories(mhp3rd_texture_decode_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
target_link_libraries(mhp3rd_texture_decode_tests PRIVATE psprecomp_core)
set_target_properties(mhp3rd_texture_decode_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_texture_decode_tests COMMAND mhp3rd_texture_decode_tests)
    add_custom_target(mhp3rd_texture_decode_slow_tests DEPENDS mhp3rd_texture_decode_tests)
    add_test(NAME mhp3rd_texture_decode_slow_tests COMMAND mhp3rd_texture_decode_tests)
    set_tests_properties(mhp3rd_texture_decode_slow_tests PROPERTIES ENVIRONMENT "MHP3RD_NO_FAST_TEXTURE_DECODE=1;MHP3RD_NO_BUFFER_REUSE=1")
    add_custom_target(mhp3rd_texture_decode_checked_tests DEPENDS mhp3rd_texture_decode_tests)
    add_test(NAME mhp3rd_texture_decode_checked_tests COMMAND mhp3rd_texture_decode_tests)
    set_tests_properties(mhp3rd_texture_decode_checked_tests PROPERTIES
        ENVIRONMENT "MHP3RD_CHECK_TEXTURE_DECODE=1" TIMEOUT 30
        FAIL_REGULAR_EXPRESSION "FAIL:;[1-9][0-9]* differed")
endif()

add_executable(mhp3rd_render_tests tests/render_tests.cpp)
target_include_directories(mhp3rd_render_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
target_link_libraries(mhp3rd_render_tests PRIVATE psprecomp_core)
set_target_properties(mhp3rd_render_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_render_tests COMMAND mhp3rd_render_tests)
endif()

# Frame interpolation checks: matching draws, cuts, blending, the present
# clock and the frame rate governor.
add_executable(mhp3rd_interpolation_tests
    tests/interpolation_tests.cpp
    host/gpu/frame_interpolation.cpp
    host/gpu/frame_pacing.cpp)
target_include_directories(mhp3rd_interpolation_tests PRIVATE
    "${MHP3RD_PROFILE_DIR}/host"
    "${PROJECT_SOURCE_DIR}/include")
set_target_properties(mhp3rd_interpolation_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_interpolation_tests COMMAND mhp3rd_interpolation_tests)
endif()

# The built-in ad hoc server against the client and a raw protocol peer, and
# local network discovery, all over the loopback address.
add_executable(mhp3rd_adhoc_tests
    tests/adhoc_tests.cpp
    host/adhoc/client.cpp
    host/adhoc/discovery.cpp
    host/adhoc/server.cpp)
target_include_directories(mhp3rd_adhoc_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
target_link_libraries(mhp3rd_adhoc_tests PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(mhp3rd_adhoc_tests PRIVATE ws2_32 iphlpapi)
endif()
set_target_properties(mhp3rd_adhoc_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_adhoc_tests COMMAND mhp3rd_adhoc_tests)
endif()

# Files under folder names outside ASCII: a Cyrillic user folder, a Japanese
# folder for the disc image. Settings, saves, texture packs, fonts, the disc
# image and the executable, in a temporary folder. No game data.
add_executable(mhp3rd_path_tests
    tests/path_tests.cpp
    host/app_paths.cpp
    host/fonts/game_font.cpp
    host/gpu/screenshot.cpp
    host/gpu/texture_pack.cpp
    host/gpu/texture_pack_import.cpp
    host/input/bindings.cpp
    host/input/chords.cpp
    host/input/presets.cpp
    host/input/touch_action.cpp
    host/input/touch_controls.cpp
    host/install/user_data.cpp
    host/kernel/iso_image.cpp
    host/platform/utf8_path.cpp
    host/save_data/aes128.cpp
    host/save_data/param_sfo.cpp
    host/save_data/savedata_crypto.cpp
    host/save_data/savedata_store.cpp
    host/save_data/save_transfer.cpp
    host/settings/settings.cpp
    third_party/tiny_aes/aes.c)
target_include_directories(mhp3rd_path_tests PRIVATE
    "${MHP3RD_PROFILE_DIR}/host"
    "${MHP3RD_PROFILE_DIR}/host/gpu"
    "${MHP3RD_PROFILE_DIR}/third_party")
target_link_libraries(mhp3rd_path_tests PRIVATE psprecomp_core Threads::Threads)
if(WIN32)
    target_link_libraries(mhp3rd_path_tests PRIVATE shell32)
endif()
set_target_properties(mhp3rd_path_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_path_tests COMMAND mhp3rd_path_tests)
endif()

# The host's sources are UTF-8: its text literals (menu text, the Japanese
# sample the font picker checks fonts with) must stay UTF-8 under MSVC, which
# otherwise reads sources in the system's ANSI code page. The recompiled code
# is ASCII and keeps its own flags.
if(MSVC)
    get_property(mhp3rd_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS mhp3rd_targets)
        get_target_property(target_type ${target} TYPE)
        if((target STREQUAL "Yakumo" OR target MATCHES "^mhp3rd_.*_tests$")
                AND NOT target_type STREQUAL "UTILITY")
            target_compile_options(${target} PRIVATE /utf-8)
        endif()
    endforeach()
endif()

# Chained AOT calls nest native frames; give the guest-executing main thread
# the same 64 MiB stack the Windows profiles use.
if(MSVC)
    target_link_options(Yakumo PRIVATE /STACK:67108864)
elseif(APPLE)
    target_link_options(Yakumo PRIVATE "LINKER:-stack_size,0x4000000")
endif()

# Recompiled overlay corpora, produced by tools/add_overlay.py. Each directory
# under overlays/ holds one corpus and becomes one shared module that the host
# loads at run time, so a new overlay never relinks the executable.
set(MHP3RD_OVERLAY_DIR "${CMAKE_BINARY_DIR}/bin/overlays")

function(mhp3rd_add_overlay meta_path)
    get_filename_component(source_dir "${meta_path}" DIRECTORY)
    get_filename_component(MHP3RD_OVERLAY_PREFIX "${source_dir}" NAME)
    file(STRINGS "${meta_path}" meta_lines REGEX "^(base|size|code_size|hash|name)=")
    foreach(line IN LISTS meta_lines)
        string(REGEX MATCH "^([a-z_]+)=(.+)$" matched "${line}")
        if(NOT matched)
            continue()
        endif()
        string(TOUPPER "${CMAKE_MATCH_1}" key)
        set(MHP3RD_OVERLAY_${key} "${CMAKE_MATCH_2}")
    endforeach()
    if(NOT MHP3RD_OVERLAY_BASE OR NOT MHP3RD_OVERLAY_SIZE OR NOT MHP3RD_OVERLAY_CODE_SIZE
       OR NOT MHP3RD_OVERLAY_HASH OR NOT MHP3RD_OVERLAY_NAME)
        message(WARNING "mhp3rd: incomplete overlay metadata in ${meta_path}")
        return()
    endif()

    file(GLOB sources CONFIGURE_DEPENDS "${source_dir}/*.cpp")
    if(NOT sources)
        message(WARNING "mhp3rd: no sources for overlay ${MHP3RD_OVERLAY_PREFIX}")
        return()
    endif()
    set(entry_point "${CMAKE_CURRENT_BINARY_DIR}/overlays/${MHP3RD_OVERLAY_PREFIX}_module.cpp")
    configure_file("${MHP3RD_PROFILE_DIR}/host/overlay_module.cpp.in" "${entry_point}" @ONLY)

    set(target "overlay_${MHP3RD_OVERLAY_PREFIX}")
    add_library(${target} MODULE ${sources} "${entry_point}")
    # Headers only: linking psprecomp_core would give the module its own copy of
    # the runtime state the host already owns.
    target_include_directories(${target} PRIVATE
        "${source_dir}" "${MHP3RD_PROFILE_DIR}/host" "${PROJECT_SOURCE_DIR}/include")
    if(WIN32 OR APPLE OR MHP3RD_ANDROID_APP)
        # Both linkers want the host binary while linking the module: MSVC for
        # its import library, ld64 as the bundle loader. ELF leaves the host
        # symbols undefined and resolves them when the module is loaded. In an
        # Android app the host is libmain.so, which the app loads privately,
        # so the module names it as a dependency to find its symbols.
        target_link_libraries(${target} PRIVATE Yakumo)
    else()
        add_dependencies(${target} Yakumo)
        if(ANDROID)
            # The NDK links every shared library with --no-undefined; these
            # leave the host's symbols to the loader like any other ELF.
            target_link_options(${target} PRIVATE "LINKER:-z,undefs")
        endif()
    endif()
    if(MSVC)
        target_compile_definitions(${target} PRIVATE PSPRECOMP_IMPORT_HOST_SYMBOLS=1)
        if(PSPRECOMP_GENERATED_OPT_LEVEL STREQUAL "0")
            target_compile_options(${target} PRIVATE /Od /bigobj)
        else()
            target_compile_options(${target} PRIVATE /O${PSPRECOMP_GENERATED_OPT_LEVEL} /Ob0 /bigobj)
        endif()
    else()
        target_compile_options(${target} PRIVATE -O${PSPRECOMP_GENERATED_OPT_LEVEL} -g0)
    endif()
    if(MHP3RD_RELEASE_LINK_OPTIONS)
        target_link_options(${target} PRIVATE ${MHP3RD_RELEASE_LINK_OPTIONS})
    endif()
    set_target_properties(${target} PROPERTIES
        JOB_POOL_COMPILE psprecomp_generated
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        PREFIX ""
        SUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}"
        OUTPUT_NAME "${MHP3RD_OVERLAY_PREFIX}"
        LIBRARY_OUTPUT_DIRECTORY "${MHP3RD_OVERLAY_DIR}")
    if(MHP3RD_ANDROID_APP)
        # An APK only carries libraries named lib*.so.
        set_target_properties(${target} PROPERTIES PREFIX "lib")
    endif()
endfunction()

file(GLOB MHP3RD_OVERLAY_METADATA CONFIGURE_DEPENDS "${MHP3RD_PROFILE_DIR}/overlays/*/meta.txt")
foreach(meta IN LISTS MHP3RD_OVERLAY_METADATA)
    mhp3rd_add_overlay("${meta}")
endforeach()
list(LENGTH MHP3RD_OVERLAY_METADATA MHP3RD_OVERLAY_COUNT)
message(STATUS "mhp3rd: ${MHP3RD_OVERLAY_COUNT} overlay librar(ies) in ${MHP3RD_OVERLAY_DIR}")

# Guest PCM staging validates the full mapped range before allocating.
add_executable(mhp3rd_guest_pcm_tests
    tests/guest_pcm_tests.cpp
    host/audio/guest_pcm.cpp)
target_include_directories(mhp3rd_guest_pcm_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
target_link_libraries(mhp3rd_guest_pcm_tests PRIVATE psprecomp_core)
set_target_properties(mhp3rd_guest_pcm_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_guest_pcm_tests COMMAND mhp3rd_guest_pcm_tests)
endif()

# PNG size bounds, stride handling and allocation failure recovery.
add_executable(mhp3rd_png_size_tests tests/png_size_tests.cpp)
target_include_directories(mhp3rd_png_size_tests PRIVATE "${MHP3RD_PROFILE_DIR}/third_party")
set_target_properties(mhp3rd_png_size_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_png_size_tests COMMAND mhp3rd_png_size_tests)
endif()

# Synthetic 16-bit PNG conversion and checked allocation bounds.
add_executable(mhp3rd_image_conversion_tests tests/image_conversion_tests.cpp)
target_include_directories(mhp3rd_image_conversion_tests PRIVATE "${MHP3RD_PROFILE_DIR}/third_party")
set_target_properties(mhp3rd_image_conversion_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_image_conversion_tests COMMAND mhp3rd_image_conversion_tests)
endif()

# Exercise the actual vendor debug formatter, including 64-bit texture IDs.
add_executable(mhp3rd_imgui_security_tests
    tests/imgui_security_tests.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_tables.cpp)
target_include_directories(mhp3rd_imgui_security_tests PRIVATE "${MHP3RD_PROFILE_DIR}/third_party/imgui")
set_target_properties(mhp3rd_imgui_security_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_imgui_security_tests COMMAND mhp3rd_imgui_security_tests)
endif()

# Both independently maintained stb_truetype copies use the same synthetic font cases.
foreach(font_test IN ITEMS mhp3rd_font_bitmap_tests mhp3rd_imgui_font_bitmap_tests)
    add_executable(${font_test} tests/font_bitmap_security_tests.cpp)
    target_include_directories(${font_test} PRIVATE
        "${MHP3RD_PROFILE_DIR}/third_party" "${MHP3RD_PROFILE_DIR}/third_party/imgui")
    set_target_properties(${font_test} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(PSPRECOMP_BUILD_TESTS)
        add_test(NAME ${font_test} COMMAND ${font_test})
    endif()
endforeach()
target_compile_definitions(mhp3rd_imgui_font_bitmap_tests PRIVATE TEST_IMGUI_STB)

# Synthetic HLE suites link the real headless host with a public empty registry.
add_library(mhp3rd_test_host STATIC ${MHP3RD_HOST_SOURCES} host/generated_stub.cpp "${MHP3RD_NIDS_INC}")
target_include_directories(mhp3rd_test_host PUBLIC
    "${MHP3RD_PROFILE_DIR}/host" "${MHP3RD_PROFILE_DIR}/third_party"
    "${CMAKE_CURRENT_BINARY_DIR}/generated_nids")
target_link_libraries(mhp3rd_test_host PUBLIC psprecomp_core Threads::Threads ${CMAKE_DL_LIBS})
mhp3rd_use_ffmpeg(mhp3rd_test_host)
if(WIN32)
    target_link_libraries(mhp3rd_test_host PUBLIC ws2_32 iphlpapi shell32)
endif()
add_executable(mhp3rd_kernel_hle_tests tests/kernel_hle_tests.cpp)
target_compile_definitions(mhp3rd_kernel_hle_tests PRIVATE PSPRECOMP_TEST_NIDS_CSV="${PROJECT_SOURCE_DIR}/configs/nids.csv")
target_link_libraries(mhp3rd_kernel_hle_tests PRIVATE mhp3rd_test_host)
set_target_properties(mhp3rd_kernel_hle_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_kernel_hle_tests COMMAND mhp3rd_kernel_hle_tests)
    set_tests_properties(mhp3rd_kernel_hle_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "MHP3RD_NO_RENDER=1;MHP3RD_TRACE_OSK=1")
endif()

add_executable(mhp3rd_media_tests
    tests/media_tests.cpp
    host/audio/atrac_decoder.cpp
    host/movie/avc_decoder.cpp
    host/audio/sas_core.cpp
    host/movie/psmf_demuxer.cpp)
target_include_directories(mhp3rd_media_tests PRIVATE "${MHP3RD_PROFILE_DIR}/host")
target_link_libraries(mhp3rd_media_tests PRIVATE psprecomp_core)
mhp3rd_use_ffmpeg(mhp3rd_media_tests)
set_target_properties(mhp3rd_media_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_PROFILE_TESTS)
    add_test(NAME mhp3rd_media_tests COMMAND mhp3rd_media_tests)
endif()

add_executable(mhp3rd_media_hle_tests tests/media_hle_tests.cpp)
target_compile_definitions(mhp3rd_media_hle_tests PRIVATE PSPRECOMP_TEST_NIDS_CSV="${PROJECT_SOURCE_DIR}/configs/nids.csv")
target_link_libraries(mhp3rd_media_hle_tests PRIVATE mhp3rd_test_host)
set_target_properties(mhp3rd_media_hle_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(PSPRECOMP_BUILD_TESTS)
    add_test(NAME mhp3rd_media_hle_tests COMMAND mhp3rd_media_hle_tests)
    set_tests_properties(mhp3rd_media_hle_tests PROPERTIES TIMEOUT 60 ENVIRONMENT "MHP3RD_NO_RENDER=1;MHP3RD_NO_AUDIO=1")
endif()

# Real public-buffer renderer/UI contracts are opt-in: use software Vulkan and
# a bounded Xvfb display in CI, or an explicitly selected local GPU.
option(MHP3RD_RENDERER_TESTS "Run real SDL/Vulkan public-buffer renderer tests" OFF)
if(MHP3RD_RENDERER_TESTS)
    if(NOT MHP3RD_RENDERER)
        message(FATAL_ERROR "Renderer tests require MHP3RD_RENDERER=ON")
    endif()
    add_executable(mhp3rd_renderer_ui_tests tests/renderer_ui_tests.cpp
        ${MHP3RD_HOST_SOURCES} ${MHP3RD_RENDERER_SOURCES}
        $<TARGET_OBJECTS:mhp3rd_generated>)
    add_dependencies(mhp3rd_renderer_ui_tests mhp3rd_shaders mhp3rd_version)
    target_sources(mhp3rd_renderer_ui_tests PRIVATE "${MHP3RD_NIDS_INC}")
    target_compile_definitions(mhp3rd_renderer_ui_tests PRIVATE MHP3RD_HAS_RENDERER=1 MHP3RD_HAS_SDL_AUDIO=1 MHP3RD_HAS_SDL=1
        PSPRECOMP_TEST_NIDS_CSV="${PROJECT_SOURCE_DIR}/configs/nids.csv")
    target_include_directories(mhp3rd_renderer_ui_tests PRIVATE
        "${MHP3RD_PROFILE_DIR}/host" "${MHP3RD_PROFILE_DIR}/third_party"
        "${MHP3RD_PROFILE_DIR}/third_party/imgui"
        "${CMAKE_CURRENT_BINARY_DIR}/generated_shaders"
        "${CMAKE_CURRENT_BINARY_DIR}/generated_version"
        "${CMAKE_CURRENT_BINARY_DIR}/generated_nids")
    target_link_libraries(mhp3rd_renderer_ui_tests PRIVATE psprecomp_core SDL3::SDL3 Vulkan::Vulkan Threads::Threads ${CMAKE_DL_LIBS})
    mhp3rd_use_ffmpeg(mhp3rd_renderer_ui_tests)
    if(WIN32)
        target_link_libraries(mhp3rd_renderer_ui_tests PRIVATE ws2_32 iphlpapi shell32)
    endif()
    set_target_properties(mhp3rd_renderer_ui_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(PSPRECOMP_BUILD_TESTS)
        add_test(NAME mhp3rd_renderer_ui_tests COMMAND mhp3rd_renderer_ui_tests)
        set_tests_properties(mhp3rd_renderer_ui_tests PROPERTIES TIMEOUT 90)
        add_custom_target(mhp3rd_input_script_tests DEPENDS mhp3rd_renderer_ui_tests)
        add_test(NAME mhp3rd_input_script_tests COMMAND mhp3rd_renderer_ui_tests --input-script)
        set_tests_properties(mhp3rd_input_script_tests PROPERTIES TIMEOUT 45)
        add_custom_target(mhp3rd_input_script_static_tests DEPENDS mhp3rd_renderer_ui_tests)
        add_test(NAME mhp3rd_input_script_static_tests COMMAND mhp3rd_renderer_ui_tests --input-script-static)
        set_tests_properties(mhp3rd_input_script_static_tests PROPERTIES TIMEOUT 45)
        add_custom_target(mhp3rd_ui_backend_lifecycle_tests DEPENDS mhp3rd_renderer_ui_tests)
        add_test(NAME mhp3rd_ui_backend_lifecycle_tests COMMAND mhp3rd_renderer_ui_tests --ui-lifecycle)
        set_tests_properties(mhp3rd_ui_backend_lifecycle_tests PROPERTIES TIMEOUT 45)
        add_custom_target(mhp3rd_renderer_texture_cache_tests DEPENDS mhp3rd_renderer_ui_tests)
        add_test(NAME mhp3rd_renderer_texture_cache_tests COMMAND mhp3rd_renderer_ui_tests --texture-cache)
        set_tests_properties(mhp3rd_renderer_texture_cache_tests PROPERTIES TIMEOUT 45
            ENVIRONMENT "MHP3RD_TEXTURE_CACHE_LIMIT=2")
        # Exercise the documented conservative path even when Auto keeps it off.
        add_custom_target(mhp3rd_renderer_ui_compat_tests DEPENDS mhp3rd_renderer_ui_tests)
        add_test(NAME mhp3rd_renderer_ui_compat_tests COMMAND mhp3rd_renderer_ui_tests)
        set_tests_properties(mhp3rd_renderer_ui_compat_tests PROPERTIES
            TIMEOUT 90
            ENVIRONMENT "MHP3RD_GPU_COMPAT=on")
        # Documented rollback paths must preserve the same independent pixel contracts.
        add_custom_target(mhp3rd_renderer_ui_legacy_tests DEPENDS mhp3rd_renderer_ui_tests)
        add_test(NAME mhp3rd_renderer_ui_legacy_tests COMMAND mhp3rd_renderer_ui_tests)
        set_tests_properties(mhp3rd_renderer_ui_legacy_tests PROPERTIES
            TIMEOUT 90
            ENVIRONMENT "MHP3RD_PAD_FOLLOW=0;MHP3RD_NO_DIRECT_VERTICES=1;MHP3RD_NO_LOOKUP_CACHE=1;MHP3RD_NO_BUFFER_REUSE=1;MHP3RD_NO_DRAW_MERGE=1;MHP3RD_NO_FAST_STORE=1;MHP3RD_NO_ALPHA_VARIANTS=1;MHP3RD_NO_CLEAR_LOAD=1;MHP3RD_SYNC_UPLOADS=1;MHP3RD_SYNC_TEXTURE_DECODE=1")
        # The same public buffers also check shader decode and direct index expansion
        # against independently specified host vertices in the diagnostic paths.
        add_custom_target(mhp3rd_renderer_ui_checked_tests DEPENDS mhp3rd_renderer_ui_tests)
        add_test(NAME mhp3rd_renderer_ui_checked_tests COMMAND mhp3rd_renderer_ui_tests)
        set_tests_properties(mhp3rd_renderer_ui_checked_tests PROPERTIES
            TIMEOUT 90
            ENVIRONMENT "MHP3RD_CHECK_DIRECT_VERTICES=1;MHP3RD_CHECK_GPU_DECODE=1;MHP3RD_CHECK_REPLAY=1;MHP3RD_TRACE_FB_TEXTURES=1;MHP3RD_TRACE_3D=1"
            PASS_REGULAR_EXPRESSION "replay check at frame.*: 0 of"
            FAIL_REGULAR_EXPRESSION "FAIL:;\\[gpu-decode-check\\].*differs;\\[direct-check\\] draw.*differ;replay check at frame.*: [1-9][0-9]* of")
    endif()
endif()
