cmake_minimum_required(VERSION 3.22)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MGH_VERSION)
string(STRIP "${MGH_VERSION}" MGH_VERSION)
project(MultiGameHub VERSION ${MGH_VERSION} LANGUAGES C CXX)

# versionCode = major*10000 + minor*100 + patch (must always increase for Android updates)
math(EXPR MGH_VERSION_CODE "${PROJECT_VERSION_MAJOR}*10000 + ${PROJECT_VERSION_MINOR}*100 + ${PROJECT_VERSION_PATCH}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MGH_BUILD_APP "Build the SDL3 application" ON)
option(MGH_BUILD_TESTS "Build unit tests" ON)
set(MGH_UPDATE_REPO "GL-328/Random-School-Games" CACHE STRING "GitHub owner/repo whose Releases provide updates")
set(MGH_SDL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL" CACHE PATH "SDL3 source directory")

if(ANDROID)
  set(MGH_BUILD_TESTS OFF)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

# Size-oriented code generation: every function/data in its own section so the
# linker can garbage-collect anything unused.
add_compile_options(-ffunction-sections -fdata-sections)
# Warnings for our own code only (third-party code keeps its defaults).
set(MGH_WARNINGS "")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  set(MGH_WARNINGS -Wall -Wextra)
endif()

# ---------------------------------------------------------------- third party
set(MINIZ_SOURCES
  third_party/miniz/miniz.c third_party/miniz/miniz_tdef.c
  third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_zip.c)
add_library(mgh_miniz STATIC ${MINIZ_SOURCES})
target_include_directories(mgh_miniz PUBLIC third_party/miniz)
target_compile_definitions(mgh_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES MINIZ_NO_TIME)
if(NOT MGH_BUILD_TESTS)
  # The app only reads archives; tests also create them.
  target_compile_definitions(mgh_miniz PUBLIC MINIZ_NO_ARCHIVE_WRITING_APIS)
endif()

# ---------------------------------------------------------------- pure logic (no SDL)
add_library(mgh_logic STATIC
  src/core/Json.cpp
  src/core/Settings.cpp
  src/core/FileUtil.cpp
  src/games/chess/ChessRules.cpp
  src/games/air_hockey/AirHockeyPhysics.cpp
  src/games/snake/SnakeLogic.cpp
  src/updates/Sha256.cpp
  src/updates/Manifest.cpp
  src/updates/PackExtractor.cpp
  src/updates/UpdateManager.cpp
  src/storage/StorageManager.cpp)
target_include_directories(mgh_logic PUBLIC src)
target_link_libraries(mgh_logic PUBLIC mgh_miniz)
target_compile_options(mgh_logic PRIVATE ${MGH_WARNINGS})

# ---------------------------------------------------------------- tests
if(MGH_BUILD_TESTS)
  enable_testing()
  add_executable(mgh_tests
    tests/main.cpp tests/test_chess.cpp tests/test_air_hockey.cpp
    tests/test_snake.cpp tests/test_updates.cpp)
  target_link_libraries(mgh_tests PRIVATE mgh_logic)
  add_test(NAME unit_tests COMMAND mgh_tests)
  # Release tooling: validates manifest.json with the app's own parser.
  add_executable(manifest_check tools/manifest_check.cpp)
  target_link_libraries(manifest_check PRIVATE mgh_logic)
endif()

# ---------------------------------------------------------------- application
if(MGH_BUILD_APP)
  include(cmake/App.cmake)
endif()
