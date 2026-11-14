# SPDX-License-Identifier: GPL-3.0-only
# ZERO Setup: the standalone installer / uninstaller program (not part of the launcher build).
# Build with installer/build.sh (dev = shared Qt into build-dev/, release = static Qt into build-release/).
cmake_minimum_required(VERSION 3.21)
project(ZeroSetup LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(ZERO_SETUP_STATIC "Link Qt and the GCC runtime statically (release installer: one exe, no DLLs)" OFF)
option(ZERO_SETUP_BUILD_UI "Build the setup program itself (off: only the core, the pack tool and tests)" ON)
option(ZERO_SETUP_TESTS "Build the unit tests" OFF)
set(ZERO_SETUP_LAUNCHER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Repository root (for the pixel font and the icon)")
get_filename_component(ZERO_SETUP_LAUNCHER_ROOT "${ZERO_SETUP_LAUNCHER_ROOT}" ABSOLUTE)
set(ZERO_SETUP_VERSION "0.0.0" CACHE STRING "Version stamped into the exe's version resource (the payload carries the launcher version)")

if(ZERO_SETUP_STATIC)
    # the MSYS2 static Qt links its third-party libraries (zlib, libpng, freetype ...) by whatever find_library returns:
    # make that the static .a, never the .dll.a import library, so the exe needs no DLL besides Windows' own
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
endif()
if(ZERO_SETUP_BUILD_UI)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
else()
    find_package(Qt6 REQUIRED COMPONENTS Core)
endif()
if(ZERO_SETUP_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()
if(ZERO_SETUP_STATIC AND TARGET harfbuzz::harfbuzz)
    # harfbuzz's own CMake config points at libharfbuzz-0.dll: use the static archive instead
    get_filename_component(_zs_prefix "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_zs_prefix "${_zs_prefix}/.." ABSOLUTE)
    foreach(_zs_prop IMPORTED_LOCATION IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_NOCONFIG IMPORTED_LOCATION_DEBUG)
        set_property(TARGET harfbuzz::harfbuzz PROPERTY ${_zs_prop} "${_zs_prefix}/lib/libharfbuzz.a")
    endforeach()
    set_property(TARGET harfbuzz::harfbuzz PROPERTY IMPORTED_IMPLIB "")
    set_property(TARGET harfbuzz::harfbuzz PROPERTY IMPORTED_IMPLIB_RELEASE "")
    set_property(TARGET harfbuzz::harfbuzz PROPERTY IMPORTED_IMPLIB_NOCONFIG "")
endif()

if(ZERO_SETUP_STATIC)
    add_link_options(-static -static-libgcc -static-libstdc++ -Wl,--gc-sections)
    add_compile_options(-Os -ffunction-sections -fdata-sections)
endif()

# ---------------------------------------------------------------- core (no GUI): payload, install engine, footprint
add_library(zerosetup_core STATIC
    src/core/Brand.h
    src/core/CmdLine.cpp src/core/CmdLine.h
    src/core/Footprint.cpp src/core/Footprint.h
    src/core/Installer.cpp src/core/Installer.h
    src/core/Jobs.cpp src/core/Jobs.h
    src/core/MemorySystem.cpp src/core/MemorySystem.h
    src/core/Payload.cpp src/core/Payload.h
    src/core/SafePath.cpp src/core/SafePath.h
    src/core/System.h
    src/core/Uninstaller.cpp src/core/Uninstaller.h
    src/core/Upgrade.cpp src/core/Upgrade.h
    src/core/WinSystem.cpp src/core/WinSystem.h
)
target_include_directories(zerosetup_core PUBLIC src/core)
target_link_libraries(zerosetup_core PUBLIC Qt6::Core)
target_link_libraries(zerosetup_core PRIVATE ole32 shell32 advapi32 uuid user32)
target_compile_options(zerosetup_core PRIVATE -Wall -Wextra)

# ---------------------------------------------------------------- pack tool
add_executable(zero-setup-pack src/pack/pack_main.cpp)
target_link_libraries(zero-setup-pack PRIVATE zerosetup_core)
set_target_properties(zero-setup-pack PROPERTIES LINK_FLAGS_RELEASE "-s")

if(ZERO_SETUP_BUILD_UI)
# ---------------------------------------------------------------- the setup program (stub: payload gets appended by the pack tool)
configure_file(resources/setup.rc.in "${CMAKE_CURRENT_BINARY_DIR}/setup.rc" @ONLY)
set(SETUP_UI_SOURCES
    src/ui/Motion.cpp src/ui/Motion.h
    src/ui/PixelWidgets.cpp src/ui/PixelWidgets.h
    src/ui/SetupCanvas.cpp src/ui/SetupCanvas.h
    src/ui/SetupWindow.cpp src/ui/SetupWindow.h
    src/ui/Worker.cpp src/ui/Worker.h
    src/SetupTokens.h
    src/MarkData.h
    # the launcher's own pixel font (needs only Qt)
    "${ZERO_SETUP_LAUNCHER_ROOT}/launcher/ui/zero/ZeroPixelFont.cpp"
    "${ZERO_SETUP_LAUNCHER_ROOT}/launcher/ui/zero/ZeroPixelFont.h"
)
# ZeroPixelFont.cpp relies on the launcher's precompiled header for <QIODevice>: include it here instead of editing that file
set_source_files_properties("${ZERO_SETUP_LAUNCHER_ROOT}/launcher/ui/zero/ZeroPixelFont.cpp" PROPERTIES COMPILE_OPTIONS "-include;QIODevice")
add_executable(zero-setup WIN32 src/main.cpp ${SETUP_UI_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/setup.rc")
target_include_directories(zero-setup PRIVATE src src/ui "${ZERO_SETUP_LAUNCHER_ROOT}/launcher/ui/zero")
target_link_libraries(zero-setup PRIVATE zerosetup_core Qt6::Widgets Qt6::Gui)
target_compile_options(zero-setup PRIVATE -Wall -Wextra)
if(ZERO_SETUP_STATIC)
    target_compile_definitions(zero-setup PRIVATE ZERO_SETUP_STATIC=1)
    # Only the Windows platform plugin: no image-format / style / icon-engine plugins (the UI paints everything itself,
    # and each of them would drag more third-party libraries into the exe)
    set_property(TARGET zero-setup PROPERTY QT_DEFAULT_PLUGINS 0)
    target_link_libraries(zero-setup PRIVATE Qt6::QWindowsIntegrationPlugin)
    # system libraries the static third-party archives need (listed last so every archive can resolve them)
    set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} -lbrotlidec -lbrotlicommon -lbz2 -lgraphite2 -lrpcrt4 -lws2_32 -lcrypt32 -lbcrypt -ldwmapi")
endif()
# The pack tool and the setup are small enough that stripping is the only size work needed.
set_target_properties(zero-setup PROPERTIES LINK_FLAGS_RELEASE "-s")
endif()

# ---------------------------------------------------------------- tests
if(ZERO_SETUP_TESTS)
    enable_testing()
    add_executable(ZeroSetupTests tests/SetupTests.cpp)
    target_link_libraries(ZeroSetupTests PRIVATE zerosetup_core Qt6::Test)
    add_test(NAME ZeroSetupTests COMMAND ZeroSetupTests)
endif()
