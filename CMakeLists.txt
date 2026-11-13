cmake_minimum_required(VERSION 3.20)
project(AladdinCollection CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # static CRT: no redistributable needed

if(MSVC)
    add_compile_options(/W3 /utf-8 /permissive- /MP /wd4244 /wd4267 /wd4305 /wd4800 /wd4146)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " /O2 /Oi /GL")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /LTCG")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " /LTCG")
endif()

# ---- emulation cores + game modules ---------------------------------------------
add_library(emucore STATIC
    # Mega Drive
    src/core/m68k.cpp
    src/core/vdp.cpp
    src/core/ym2612.cpp
    src/core/md.cpp
    # Super Nintendo
    src/core/cpu65816.cpp
    src/core/apu.cpp
    src/core/dsp.cpp
    src/core/ppu.cpp
    src/core/snes.cpp
    src/core/disasm.cpp
    # Master System (and the Z80 / PSG the Mega Drive shares)
    src/core/z80.cpp
    src/core/psg.cpp
    src/core/sms_vdp.cpp
    src/core/sms.cpp
    # game modules
    src/game/modules.cpp
    src/game/md_common.cpp
    src/game/snes_common.cpp
    src/game/sms_common.cpp
    src/game/md_aladdin.cpp
    src/game/snes_aladdin.cpp
    src/game/sms_aladdin.cpp
)
target_include_directories(emucore PUBLIC src)

# ---- the collection -------------------------------------------------------------
add_executable(AladdinCollection WIN32
    src/app/main.cpp
    src/app/app.cpp
    src/app/app_win.cpp
    src/app/app_achievements.cpp
    src/app/achievements.cpp
    src/app/achexport.cpp
    src/app/updatecheck.cpp
    src/app/app_updates.cpp
    src/app/app_menus.cpp
    src/app/launcher.cpp
    src/app/jukebox.cpp
    src/app/session.cpp
    src/app/session_md.cpp
    src/app/session_snes.cpp
    src/app/session_sms.cpp
    src/app/soundtrack.cpp
    src/app/settings.cpp
    src/app/palette.cpp
    src/app/audio_mixer.cpp
    src/app/romfind.cpp
    src/app/sys_win.cpp
    src/app/unzip.cpp
    src/app/monitors.cpp
    src/app/png.cpp
    src/app/rewind.cpp
    src/app/ui.cpp
    src/app/ornaments.cpp
    src/app/imageload.cpp
    src/platform/video.cpp
    src/platform/slang.cpp
    src/platform/audio.cpp
    src/platform/input.cpp
    res/app.rc
)
target_link_libraries(AladdinCollection PRIVATE emucore
    user32 gdi32 ole32 d3d11 dxgi d3dcompiler xaudio2 xinput dinput8 dxguid dwrite winmm dwmapi shell32 winhttp)
target_include_directories(AladdinCollection PRIVATE third_party/stb)   # (stb_image: the player's cover pictures)
set_target_properties(AladdinCollection PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin-debug)
# Slang shader presets: librashader.dll (loaded at run time when present, third_party/librashader) and the
# bundled presets (a few of the libretro slang-shaders, with their licences) next to the exe
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/librashader/librashader.dll)
    add_custom_command(TARGET AladdinCollection POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/third_party/librashader/librashader.dll $<TARGET_FILE_DIR:AladdinCollection>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/third_party/librashader/LICENSE.md $<TARGET_FILE_DIR:AladdinCollection>/librashader-LICENSE.md)
endif()
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/slang-shaders)
    add_custom_command(TARGET AladdinCollection POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/third_party/slang-shaders $<TARGET_FILE_DIR:AladdinCollection>/shaders)
endif()

# ---- analysis DLLs for tools/mdt.py, tools/snt.py, tools/smst.py ----------------
foreach(tool mdtool snestool smstool)
    add_library(${tool} SHARED src/tools/${tool}.cpp)
    target_link_libraries(${tool} PRIVATE emucore)
    set_target_properties(${tool} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tools
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tools)
endforeach()

# ---- tests of the update check and the achievements export (tests/app_test.cpp) ----
add_executable(app_tests
    tests/app_test.cpp
    src/app/updatecheck.cpp
    src/app/achexport.cpp
    src/app/achievements.cpp
    src/app/png.cpp
    src/app/sys_win.cpp
)
target_link_libraries(app_tests PRIVATE emucore shell32 winhttp)
set_target_properties(app_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/tests)
