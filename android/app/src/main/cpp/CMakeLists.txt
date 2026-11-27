# Native part of the Android build: SDL2 from source (libSDL2.so) and the port itself (libmain.so, loaded by
# SDLActivity, which calls its SDL_main). The emulation core, game modules and app logic are the Windows
# build's; only the platform layer differs (src/platform/sdl, shared with the Linux build; src/app/*_android.cpp).
cmake_minimum_required(VERSION 3.18)
project(AladdinCollectionAndroid C CXX)

get_filename_component(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../.. ABSOLUTE)
set(SDL_DIR ${ROOT}/third_party/SDL2-2.32.10-src)
if(NOT EXISTS ${SDL_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "SDL2 source not found in ${SDL_DIR}: run android/fetch_sdl.ps1 first")
endif()

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${SDL_DIR} ${CMAKE_BINARY_DIR}/SDL)

set(SRC ${ROOT}/src)
add_library(main SHARED
  ${SRC}/core/m68k.cpp
  ${SRC}/core/vdp.cpp
  ${SRC}/core/ym2612.cpp
  ${SRC}/core/md.cpp
  ${SRC}/core/cpu65816.cpp
  ${SRC}/core/apu.cpp
  ${SRC}/core/dsp.cpp
  ${SRC}/core/ppu.cpp
  ${SRC}/core/hfr_match.cpp
  ${SRC}/core/snes.cpp
  ${SRC}/core/msu1.cpp
  ${SRC}/core/disasm.cpp
  ${SRC}/core/z80.cpp
  ${SRC}/core/psg.cpp
  ${SRC}/core/sms_vdp.cpp
  ${SRC}/core/sms.cpp
  ${SRC}/game/modules.cpp
  ${SRC}/game/md_common.cpp
  ${SRC}/game/snes_common.cpp
  ${SRC}/game/sms_common.cpp
  ${SRC}/game/md_aladdin.cpp
  ${SRC}/game/snes_aladdin.cpp
  ${SRC}/game/sms_aladdin.cpp
  ${SRC}/app/app.cpp
  ${SRC}/app/app_android.cpp
  ${SRC}/app/app_achievements.cpp
  ${SRC}/app/achievements.cpp
  ${SRC}/app/achexport.cpp
  ${SRC}/app/updatecheck.cpp
  ${SRC}/app/app_updates.cpp
  ${SRC}/app/app_menus.cpp
  ${SRC}/app/launcher.cpp
  ${SRC}/app/jukebox.cpp
  ${SRC}/app/session.cpp
  ${SRC}/app/session_md.cpp
  ${SRC}/app/session_snes.cpp
  ${SRC}/app/session_sms.cpp
  ${SRC}/app/soundtrack.cpp
  ${SRC}/app/settings.cpp
  ${SRC}/app/palette.cpp
  ${SRC}/app/audio_mixer.cpp
  ${SRC}/app/romfind.cpp
  ${SRC}/app/unzip.cpp
  ${SRC}/app/sys_android.cpp
  ${SRC}/app/sys_posix.cpp
  ${SRC}/app/png.cpp
  ${SRC}/app/rewind.cpp
  ${SRC}/app/ui.cpp
  ${SRC}/app/ornaments.cpp
  ${SRC}/app/imageload.cpp
  ${SRC}/app/touch.cpp
  ${SRC}/platform/sdl/video_gles.cpp
  ${SRC}/platform/sdl/audio_sdl.cpp
  ${SRC}/platform/sdl/input_sdl.cpp
)
set_target_properties(main PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(main PRIVATE ${SRC} ${ROOT}/third_party/stb)
# (the emulation core needs optimising even in debug builds to reach full speed)
target_compile_options(main PRIVATE -O2 -Wno-unused-parameter -Wno-sign-compare)
target_link_libraries(main PRIVATE SDL2 GLESv3 android log)
