cmake_minimum_required(VERSION 3.20)
project(crescent_hawks_rebuild LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Where the player's own copy of the game is (docs/rules.md, rule 1: we never
# ship it). The CHR_GAME_DIR environment variable or `chr --game-dir` overrides
# this at run time. The tests that need real game files skip without it.
set(CHR_GAME_DIR "${CMAKE_SOURCE_DIR}/original/dos-1.03" CACHE PATH
    "The original game's folder (BTECH.EXE, *.CMP, ...) the program and tests read")

# Packages (tools/build_package.py): the build's own folders left out of
# what the compiler writes into the program (__FILE__ in SDL's assertions), so
# no local path ships.
option(CHR_TRIM_PATHS "Map the source and build folders out of the binaries" OFF)
if(CHR_TRIM_PATHS AND NOT MSVC)
  add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=." "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
  if(CHR_SDL_SOURCE_DIR)
    add_compile_options("-ffile-prefix-map=${CHR_SDL_SOURCE_DIR}=SDL")
  endif()
elseif(CHR_TRIM_PATHS)
  add_compile_options(/d1trimfile:${CMAKE_SOURCE_DIR}\\ /d1trimfile:${CMAKE_BINARY_DIR}\\)
  if(CHR_SDL_SOURCE_DIR)
    add_compile_options(/d1trimfile:${CHR_SDL_SOURCE_DIR}\\)
  endif()
endif()

# SDL3: a package builds its own from source (CHR_SDL_SOURCE_DIR, SDL 3.4.12
# pinned by commit, docs/RELEASING.md), shared, beside the program; otherwise
# the system's.
set(CHR_SDL_SOURCE_DIR "" CACHE PATH "SDL3's source, built here as a shared library (packages)")
if(CHR_SDL_SOURCE_DIR)
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  add_subdirectory("${CHR_SDL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/sdl3" EXCLUDE_FROM_ALL)
endif()

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# The game logic. Plain C++: no SDL, no renderer types, never touches pixels.
# It speaks in game terms and asset IDs (the goal of a later visual makeover as a mod).
# Readers for the original's files, documented in docs/formats/. Standard C++ only.
add_library(chrformats STATIC
  src/formats/adf.cpp
  src/formats/amiga_anm.cpp
  src/formats/amiga_bld.cpp
  src/formats/amiga_map.cpp
  src/formats/amiga_picture.cpp
  src/formats/amiga_program.cpp
  src/formats/game_source.cpp
  src/formats/anm.cpp
  src/formats/bld.cpp
  src/formats/cells.cpp
  src/formats/cmp.cpp
  src/formats/d64.cpp
  src/formats/exe_data.cpp
  src/formats/exepack.cpp
  src/formats/font_file.cpp
  src/formats/game_files.cpp
  src/formats/import.cpp
  src/formats/json.cpp
  src/formats/icn.cpp
  src/formats/mtp.cpp
  src/formats/palette.cpp
  src/formats/rdos.cpp
  src/formats/rules.cpp
  src/formats/tables.cpp
  src/formats/sha256.cpp
  src/formats/sprite_sheet.cpp
)
target_include_directories(chrformats PUBLIC src)

# (The game logic reads the map data through chrformats.)
add_library(chrgame STATIC
  src/game/asset_id.cpp
  src/game/figures.cpp
  src/game/map_view.cpp
  src/game/money.cpp
  src/game/movement.cpp
  src/game/cache.cpp
  src/game/place.cpp
  src/game/startup.cpp
  src/game/random.cpp
  src/game/sound.cpp
  src/game/music.cpp
  src/game/save.cpp
  src/game/text.cpp
  src/game/text_provider.cpp
  src/game/town.cpp
  src/game/terrain.cpp
  src/game/mission.cpp
  src/game/overhead.cpp
  src/game/townspeople.cpp
  src/game/combat.cpp
  src/game/fight.cpp
  src/game/encounter.cpp
  src/game/training_quiz.cpp
  src/game/mech_stats.cpp
)
target_include_directories(chrgame PUBLIC src)
target_link_libraries(chrgame PUBLIC chrformats)

# The shared setup kit (extern/re-setup-kit, a submodule; DESIGN.md there):
# finding the player's copy in a folder or an archive, and the launcher
# contract (--status, --setup). It reads archives through libarchive with zlib
# and liblzma: static in packages (tools/deps/build_archive.sh's prefix), else
# the system's; without it the setup reads folders only.
if(APPLE)
  set(CHR_DEPS_PLATFORM mac)
else()
  set(CHR_DEPS_PLATFORM linux)
endif()
find_path(CHR_LIBARCHIVE_INCLUDE archive.h
          HINTS "${CMAKE_SOURCE_DIR}/build-deps/${CHR_DEPS_PLATFORM}/prefix/include" "${CMAKE_SOURCE_DIR}/deps/archive/include"
                /opt/homebrew/opt/libarchive/include /usr/local/opt/libarchive/include)
find_library(CHR_LIBARCHIVE_LIB NAMES archive archive_static
             HINTS "${CMAKE_SOURCE_DIR}/build-deps/${CHR_DEPS_PLATFORM}/prefix/lib" "${CMAKE_SOURCE_DIR}/deps/archive/lib"
                   /opt/homebrew/opt/libarchive/lib /usr/local/opt/libarchive/lib)
if(CHR_LIBARCHIVE_INCLUDE AND CHR_LIBARCHIVE_LIB)
  set(REKIT_LIBARCHIVE_INCLUDE "${CHR_LIBARCHIVE_INCLUDE}" CACHE PATH "" FORCE)
  set(REKIT_LIBARCHIVE_LIB "${CHR_LIBARCHIVE_LIB}" CACHE FILEPATH "" FORCE)
endif()
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/extern/re-setup-kit/CMakeLists.txt")
  message(FATAL_ERROR "extern/re-setup-kit is missing: git submodule update --init")
endif()
add_subdirectory(extern/re-setup-kit "${CMAKE_BINARY_DIR}/re-setup-kit")
set(CHR_VERSION "dev" CACHE STRING "The program's version, as --status reports it (packages set it)")
set(CHR_VERSION_TAG "dev" CACHE STRING "The package's full label, in the launcher's version marker (packages set it)")

# The setup recipe (src/setup/setup.h): no SDL.
add_library(chrsetup STATIC src/setup/setup.cpp src/setup/versions.cpp)
target_include_directories(chrsetup PUBLIC src)
target_link_libraries(chrsetup PUBLIC chrformats rekit)

enable_testing()

add_executable(chrtests
  tests/test_main.cpp
  tests/test_amiga.cpp
  tests/test_bld.cpp
  tests/test_anm.cpp
  tests/test_fight.cpp
  tests/test_quiz.cpp
  tests/test_rules.cpp
  tests/test_tables.cpp
  tests/test_interface_text.cpp
  tests/test_json.cpp
  tests/test_cmp.cpp
  tests/test_disk_images.cpp
  tests/test_sound.cpp
  tests/test_maps.cpp
  tests/test_text.cpp
  tests/test_walk.cpp
  tests/test_colours.cpp
  # The effects as samples: plain C++, no SDL.
  src/platform/sound_wave.cpp
)
target_link_libraries(chrtests PRIVATE chrgame)
target_include_directories(chrtests PRIVATE src)
target_compile_definitions(chrtests PRIVATE CHR_DEFAULT_GAME_DIR="${CHR_GAME_DIR}"
  CHR_TEST_SCRATCH="${CMAKE_BINARY_DIR}/test-scratch"
  CHR_INTERFACE_TEXT="${CMAKE_SOURCE_DIR}/data/text/interface.json")
add_test(NAME chrtests COMMAND chrtests)

# The setup recipe on made-up folders: the player's folder's move, settings.ini,
# the verdicts, the install from a folder and an archive, --status's answer
# writing nothing. Its game parts need the player's files (CHR_GAME_DIR).
add_executable(setup_test tests/setup_test.cpp)
target_link_libraries(setup_test PRIVATE chrsetup)
add_test(NAME setup COMMAND setup_test "${CMAKE_BINARY_DIR}/setup_work" "${CHR_GAME_DIR}")

# The platform layer: SDL3 window, rendering and the asset provider.
if(TARGET SDL3::SDL3)
  set(SDL3_FOUND TRUE)
else()
  find_package(SDL3 CONFIG QUIET)
endif()
if(SDL3_FOUND)
  add_library(chrplatform STATIC
    src/platform/map_layer.cpp
    src/platform/original_assets.cpp
    src/platform/overhead_layer.cpp
    src/platform/panel_layer.cpp
    src/platform/quiz_layer.cpp
    src/platform/stats_layer.cpp
    src/platform/renderer.cpp
    src/platform/sdl_setup.cpp
    src/platform/sound_player.cpp
    src/platform/sound_wave.cpp
    src/platform/text_layer.cpp
  )
  target_include_directories(chrplatform PUBLIC src)
  target_link_libraries(chrplatform PUBLIC chrgame chrsetup rekit_sdl SDL3::SDL3)
  target_compile_definitions(chrplatform PRIVATE CHR_VERSION="${CHR_VERSION}")

  add_executable(chr src/main.cpp)
  target_link_libraries(chr PRIVATE chrplatform)
  # Windows packages: a GUI program, so no console window opens (SDL_main.h's WinMain).
  option(CHR_WINDOWS_GUI "Link chr as a Windows GUI program" OFF)
  if(WIN32 AND CHR_WINDOWS_GUI)
    set_target_properties(chr PROPERTIES WIN32_EXECUTABLE ON)
  endif()
  # The program's icon on Windows (tools/package/ICONS.md): the first icon resource is the one shown.
  if(WIN32)
    file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/chi.rc"
         CONTENT "1 ICON \"${CMAKE_SOURCE_DIR}/tools/package/chi.ico\"\n" @ONLY)
    target_sources(chr PRIVATE "${CMAKE_BINARY_DIR}/chi.rc")
  endif()
  # The repo's default interface texts, next to the program (docs/text.md).
  add_custom_command(TARGET chr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/data/text/interface.json"
            "$<TARGET_FILE_DIR:chr>/data/text/interface.json")
  target_compile_definitions(chr PRIVATE CHR_DEFAULT_GAME_DIR="${CHR_GAME_DIR}" CHR_VERSION_TAG="${CHR_VERSION_TAG}")
  # The program itself, walking known routes on map 1 with no window (SDL's
  # dummy video driver). They need the game files.
  if(EXISTS "${CHR_GAME_DIR}/MAP1.MTP")
    set(chr_headless "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;CHR_DATA_DIR=${CMAKE_BINARY_DIR}/chr-data")
    add_test(NAME walk_blocked_by_fence
      COMMAND chr --map 1 --script "down*3" --screenshot walk_blocked.bmp)
    set_tests_properties(walk_blocked_by_fence PROPERTIES ENVIRONMENT "${chr_headless}"
      PASS_REGULAR_EXPRESSION "blocked at 0c45,c01b")
    add_test(NAME walk_into_citadel
      COMMAND chr --map 1 --script "up*5 y" --screenshot walk_citadel.bmp)
    set_tests_properties(walk_into_citadel PROPERTIES ENVIRONMENT "${chr_headless}"
      PASS_REGULAR_EXPRESSION "entered place 1 \\(Citadel\\)")
    add_test(NAME walk_scrolls_west
      COMMAND chr --map 1 --script "left*10" --screenshot walk_west.bmp)
    set_tests_properties(walk_scrolls_west PROPERTIES ENVIRONMENT "${chr_headless}"
      PASS_REGULAR_EXPRESSION "moved at 0c3b,c019")
    # A scripted run, as the rig's, never opens an audio device
    # (docs/sound.md), with or without a screenshot: the player is made only
    # for a run with neither.
    add_test(NAME no_audio_when_scripted
      COMMAND chr --map 1 --script "up*5 y" --screenshot no_audio.bmp --audio-report)
    set_tests_properties(no_audio_when_scripted PROPERTIES ENVIRONMENT "${chr_headless}"
      PASS_REGULAR_EXPRESSION "audio opened 0" FAIL_REGULAR_EXPRESSION "audio opened 1")
    add_test(NAME no_audio_when_scripted_bare
      COMMAND chr --map 1 --script "up*5 y" --audio-report)
    set_tests_properties(no_audio_when_scripted_bare PROPERTIES ENVIRONMENT "${chr_headless}"
      PASS_REGULAR_EXPRESSION "audio opened 0" FAIL_REGULAR_EXPRESSION "audio opened 1")
  endif()
  # The rebuild against the original, pixel for pixel (tools/reference,
  # docs/reference.md): runs the original in DOSBox-X headless. Skips without
  # DOSBox-X, nasm or the game. SDL_VIDEO_DRIVER is SDL3's name (chr),
  # SDL_VIDEODRIVER SDL1's (DOSBox-X, which run.py also sets): no window.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_FOUND)
    # The launcher contract on the program (--status, --setup and the setup
    # screen, hidden on SDL's dummy driver), on made-up folders and a made-up
    # home; with the game, a copy set up from it.
    add_test(NAME status_setup
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tests/status_setup_test.py
        --program $<TARGET_FILE:chr> --work ${CMAKE_BINARY_DIR}/status_setup_work --game ${CHR_GAME_DIR})
    set_tests_properties(status_setup PROPERTIES TIMEOUT 300)
    add_test(NAME reference_compare
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/reference/check.py
        --chr $<TARGET_FILE:chr> --game-dir ${CHR_GAME_DIR}
        --data-dir ${CMAKE_BINARY_DIR}/chr-data --work ${CMAKE_BINARY_DIR}/reference)
    set_tests_properties(reference_compare PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS reference
      ENVIRONMENT "CHR_SCRATCH=${CMAKE_BINARY_DIR}/reference-scratch;SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    # The long cases too (a few minutes): `ctest -LE long` leaves it out.
    add_test(NAME reference_compare_full
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/reference/check.py --full
        --chr $<TARGET_FILE:chr> --game-dir ${CHR_GAME_DIR}
        --data-dir ${CMAKE_BINARY_DIR}/chr-data --work ${CMAKE_BINARY_DIR}/reference-full)
    set_tests_properties(reference_compare_full PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900
      LABELS "reference;long" RESOURCE_LOCK dosbox
      ENVIRONMENT "CHR_SCRATCH=${CMAKE_BINARY_DIR}/reference-scratch-full;SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    set_tests_properties(reference_compare PROPERTIES RESOURCE_LOCK dosbox)
    # The campaign tier, the long unpoked run (tens of minutes as it grows):
    # only with -DCHR_REFERENCE_CAMPAIGN=ON, or `check.py --campaign`.
    option(CHR_REFERENCE_CAMPAIGN "Add the reference campaign test (the long unpoked run)" OFF)
    if(CHR_REFERENCE_CAMPAIGN)
      add_test(NAME reference_compare_campaign
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/reference/check.py --campaign
          --chr $<TARGET_FILE:chr> --game-dir ${CHR_GAME_DIR}
          --data-dir ${CMAKE_BINARY_DIR}/chr-data --work ${CMAKE_BINARY_DIR}/reference-campaign)
      set_tests_properties(reference_compare_campaign PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 3600
        LABELS "reference;campaign" RESOURCE_LOCK dosbox
        ENVIRONMENT "CHR_SCRATCH=${CMAKE_BINARY_DIR}/reference-scratch-campaign;SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    endif()
  endif()
  if(WIN32)
    # SDL3.dll beside chr.exe.
    add_custom_command(TARGET chr POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:chr>)
  endif()
else()
  message(STATUS "SDL3 not found: chr is not built (macOS: brew install sdl3)")
endif()
