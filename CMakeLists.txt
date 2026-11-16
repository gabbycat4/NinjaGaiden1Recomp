# Ninja Gaiden (USA) on the NESRecomp cycle backend.
#
#   cmake -S . -B build-play -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-play --parallel
#   ./build-play/ng1recomp ninjagaiden.nes
#
# Your own ROM: ninjagaiden.nes beside this file, or -DNG1_ROM=/path/to/rom.nes.
# Headless (logs, harvesting): add -DNESRECOMP_HEADLESS=ON, separate build dir.
cmake_minimum_required(VERSION 3.20)
project(NinjaGaiden1Recomp C)

# The engine: a nesrecomp submodule here, else the shared fixed fork.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/nesrecomp/runner/cyc/project.cmake")
    set(_ng1_engine "${CMAKE_CURRENT_SOURCE_DIR}/nesrecomp")
else()
    set(_ng1_engine "$ENV{HOME}/Projects/nesrecomp")
endif()
set(NESRECOMP_ROOT "${_ng1_engine}" CACHE PATH "nesrecomp engine checkout")
if(NOT EXISTS "${NESRECOMP_ROOT}/runner/cyc/project.cmake")
    message(FATAL_ERROR "NESRECOMP_ROOT=${NESRECOMP_ROOT} is not a nesrecomp checkout")
endif()

# The launcher and in-game menu (recomp-ui): a submodule here, else the copy
# in DD2Recomp (pinned at the tested commit there).
if(NOT DEFINED CACHE{NESRECOMP_RECOMP_UI}
   AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui/recomp_ui.cmake"
   AND EXISTS "$ENV{HOME}/Projects/DD2Recomp/recomp-ui/recomp_ui.cmake")
    set(NESRECOMP_RECOMP_UI "$ENV{HOME}/Projects/DD2Recomp/recomp-ui" CACHE STRING
        "recomp-ui checkout for the cycle host's launcher and menu")
endif()

set(NG1_ROM "${CMAKE_CURRENT_SOURCE_DIR}/ninjagaiden.nes" CACHE FILEPATH "Ninja Gaiden (USA) ROM")
if(NOT EXISTS "${NG1_ROM}")
    message(FATAL_ERROR "No ROM at ${NG1_ROM}: put your Ninja Gaiden (USA) dump there as ninjagaiden.nes, "
                        "or pass -DNG1_ROM=/path/to/rom.nes")
endif()
option(NESRECOMP_HEADLESS "Build without SDL window/audio support" OFF)

# Rewind starts off: its snapshot every few frames slows the frame after it,
# which breaks the even 120/180/240 pictures a second (players can turn it
# on in config.ini [Rewind] Enabled = 1).
add_compile_definitions(NESRECOMP_REWIND_DEFAULT=0)

include("${NESRECOMP_ROOT}/runner/cyc/project.cmake")

set(ng1_options "")
if(NESRECOMP_HEADLESS)
    list(APPEND ng1_options HEADLESS)
endif()

# seeds.txt: ROM code addresses to compile (no game data, safe to commit).
# capture.txt: code the game copies into RAM (holds game bytes, local only).
set(ng1_capture "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/capture.txt")
    set(ng1_capture CAPTURE_FILE "capture.txt")
endif()
nesrecomp_add_cycle_game(ng1recomp
    ROM          "${NG1_ROM}"
    GAME_CONFIG  "game.toml"
    SEED_FILE    "seeds.txt"
    ${ng1_capture}
    PLAYERS 1
    REGION NTSC
    HOST_EXTRAS  "src/ng1_extras.c"
    ${ng1_options})

if(UNIX)
    target_link_libraries(ng1recomp PRIVATE m)
endif()
