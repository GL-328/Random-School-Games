# Application target: SDL3 (static) + hub + games + platform layer.

if(NOT EXISTS "${MGH_SDL_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "SDL3 sources not found in ${MGH_SDL_DIR}. Run scripts/fetch_deps.sh first.")
endif()

# ---- SDL3 configuration: static library, only what we use.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
# Subsystems the hub never uses.
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER_GPU OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_RENDER_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
if(ANDROID)
  set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
  set(SDL_OPENGLES ON CACHE BOOL "" FORCE)
  set(SDL_ANDROID_JAR OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory("${MGH_SDL_DIR}" "${CMAKE_BINARY_DIR}/SDL3" EXCLUDE_FROM_ALL SYSTEM)

set(MGH_APP_SOURCES
  src/core/App.cpp
  src/audio/Audio.cpp
  src/ui/Font.cpp
  src/ui/Renderer2D.cpp
  src/ui/Icons.cpp
  src/ui/Ui.cpp
  src/ui/Hub.cpp
  src/games/GameRegistry.cpp
  src/games/chess/ChessGame.cpp
  src/games/air_hockey/AirHockeyGame.cpp
  src/games/snake/SnakeGame.cpp)

add_library(mgh_app STATIC ${MGH_APP_SOURCES})
target_include_directories(mgh_app PUBLIC src third_party/stb)
target_link_libraries(mgh_app PUBLIC mgh_logic SDL3::SDL3-static)
target_compile_options(mgh_app PRIVATE ${MGH_WARNINGS})
target_compile_definitions(mgh_app PUBLIC
  MGH_VERSION_STR="${PROJECT_VERSION}"
  MGH_VERSION_CODE=${MGH_VERSION_CODE}
  MGH_UPDATE_REPO_STR="${MGH_UPDATE_REPO}")

if(ANDROID)
  # Android: everything goes into libmain.so, loaded by HubActivity (SDLActivity).
  add_library(main SHARED src/main.cpp src/platform/android/AndroidPlatform.cpp)
  target_link_libraries(main PRIVATE mgh_app android log)
  # Export only what Java needs (JNI entry points + SDL_main) so the linker can
  # strip every unused function out of SDL and our own code.
  target_link_options(main PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/android_exports.map"
    "-Wl,--gc-sections" "-Wl,--icf=all" "-Wl,--as-needed" "-Wl,-z,max-page-size=16384"
    "$<$<NOT:$<CONFIG:Debug>>:-s>")
  set_target_properties(main PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/android_exports.map")
else()
  add_executable(multigamehub src/main.cpp src/platform/desktop/DesktopPlatform.cpp)
  target_link_libraries(multigamehub PRIVATE mgh_app)
  if(NOT APPLE)
    target_link_options(multigamehub PRIVATE "-Wl,--gc-sections")
  endif()
  # Copy runtime assets next to the executable.
  add_custom_command(TARGET multigamehub POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:multigamehub>/assets")
endif()
