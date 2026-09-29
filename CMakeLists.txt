cmake_minimum_required(VERSION 3.20)
# Single source of truth for the Velo version: project(... VERSION x.y.z).
# It flows into the app (VELO_VERSION define), the macOS bundle metadata and
# the CI release pipeline (release tags must match, see .github/workflows).
project(Velo VERSION 0.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(MSVC)
  set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Zi")
else()
  set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia Svg Concurrent)

# FFmpeg: pkg-config for local/dev builds; CI release builds pass
# -DFFMPEG_ROOT=<prefix> pointing at a pinned shared-library build whose
# libs get bundled next to the app.
if(FFMPEG_ROOT)
  add_library(ffmpeg_libs INTERFACE)
  target_include_directories(ffmpeg_libs INTERFACE "${FFMPEG_ROOT}/include")
  foreach(_ff avformat avcodec avutil swscale swresample)
    find_library(FFMPEG_${_ff} NAMES ${_ff} lib${_ff}
      HINTS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(ffmpeg_libs INTERFACE "${FFMPEG_${_ff}}")
  endforeach()
  add_library(PkgConfig::FFMPEG ALIAS ffmpeg_libs)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswscale libswresample)
endif()

add_executable(velo
  resources.qrc
  src/main.cpp
  src/core/Keyframes.h
  src/core/Model.h          src/core/Model.cpp
  src/core/Document.h       src/core/Document.cpp
  src/media/MediaCache.h    src/media/MediaCache.cpp
  src/effects/Effects.h     src/effects/Effects.cpp
  src/engine/Compositor.h   src/engine/Compositor.cpp
  src/engine/AudioEngine.h  src/engine/AudioEngine.cpp
  src/engine/RenderWorker.h src/engine/RenderWorker.cpp
  src/engine/Exporter.h     src/engine/Exporter.cpp
  src/ui/Theme.h            src/ui/Theme.cpp
  src/ui/MainWindow.h       src/ui/MainWindow.cpp
  src/ui/MediaBin.h         src/ui/MediaBin.cpp
  src/ui/EffectsPanel.h     src/ui/EffectsPanel.cpp
  src/ui/PreviewWidget.h    src/ui/PreviewWidget.cpp
  src/ui/TimelineWidget.h   src/ui/TimelineWidget.cpp
  src/ui/PropertiesPanel.h  src/ui/PropertiesPanel.cpp
  src/ui/Dialogs.h          src/ui/Dialogs.cpp
)

target_compile_definitions(velo PRIVATE VELO_VERSION="${PROJECT_VERSION}")
target_include_directories(velo PRIVATE src)
target_link_libraries(velo PRIVATE
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia Qt6::Svg Qt6::Concurrent
  PkgConfig::FFMPEG)

if(WIN32)
  target_sources(velo PRIVATE dist/velo.rc)
  # GUI subsystem (no console window); --selftest still reports via exit code
  set_target_properties(velo PROPERTIES WIN32_EXECUTABLE ON)
endif()

if(APPLE)
  target_sources(velo PRIVATE dist/velo.icns)
  set_source_files_properties(dist/velo.icns PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(velo PROPERTIES
    OUTPUT_NAME Velo
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME Velo
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.notune.velo
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE velo.icns)
endif()

install(TARGETS velo RUNTIME DESTINATION bin BUNDLE DESTINATION .)
if(UNIX AND NOT APPLE)
  install(FILES dist/velo.desktop DESTINATION share/applications)
  install(FILES dist/velo.svg DESTINATION share/icons/hicolor/scalable/apps)
endif()
