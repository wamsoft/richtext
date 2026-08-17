cmake_minimum_required(VERSION 3.31)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
    add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
    add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")
    project(richtext)
endif()

if(NOT TARGET thorvg)
    # ThorVG configuration - static library, SW engine only
    option(TVG_BUILD_SHARED "Build shared library" OFF)
    option(TVG_THREADS "Enable multi-threading task scheduler" ON)
    option(TVG_PARTIAL "Enable Partial Rendering" ON)
    option(TVG_SIMD "Enable CPU Vectorization (SIMD)" ON)
    option(TVG_LOG "Enable log message" OFF)
    option(TVG_FILE_IO "Enable File IO calls" OFF)

    # Enable SW engine only
    option(TVG_ENGINE_SW "Enable Software Rasterizer Engine" ON)
    option(TVG_ENGINE_GL "Enable OpenGL Rasterizer Engine" OFF)
    option(TVG_ENGINE_WG "Enable WebGPU Rasterizer Engine" OFF)

    # Loaders - enable what we need
    option(TVG_LOADER_SVG "Enable SVG Loader" OFF)
    option(TVG_LOADER_PNG "Enable PNG Loader" OFF)
    option(TVG_LOADER_JPG "Enable JPG Loader" OFF)
    option(TVG_LOADER_LOTTIE "Enable Lottie Loader" OFF)
    option(TVG_LOADER_TTF "Enable TTF Loader" OFF)
    option(TVG_LOADER_WEBP "Enable WebP Loader" OFF)

    # Disable savers and tools
    option(TVG_SAVER_GIF "Enable GIF Saver" OFF)
    option(TVG_TOOL_SVG2PNG "Build svg2png tool" OFF)
    option(TVG_TOOL_LOTTIE2GIF "Build lottie2gif tool" OFF)

    # Disable bindings and tests
    option(TVG_BINDINGS_CAPI "Enable C API bindings" OFF)
    option(TVG_BUILD_TESTS "Build unit tests" OFF)

    add_subdirectory(ext/thorvg)
endif()

if(NOT TARGET minikin)
    add_subdirectory(ext/minikin)
endif()

#------------------------------------------------------------------------------
# glyphware（統一フォントエンジン）
#
# 既定バックエンド GlyphwareFontBackend の実体。親プロジェクト（krkrz umbrella）
# が既に glyphware target を提供している場合はそれを共有し、単体ビルドでは
# GLYPHWARE_DIR のソースツリーを取り込む。
#------------------------------------------------------------------------------
set(GLYPHWARE_DIR "" CACHE PATH "Path to the glyphware source tree")

if(NOT TARGET glyphware AND GLYPHWARE_DIR)
    if(NOT EXISTS "${GLYPHWARE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "GLYPHWARE_DIR does not point to a glyphware tree: ${GLYPHWARE_DIR}")
    endif()
    # glyphware は harfbuzz::harfbuzz を要求するが、minikin の ext/harfbuzz が
    # 作るのは bare `harfbuzz` と `HarfBuzz::HarfBuzz` なのでエイリアスを補う
    # （thorvg 側も同じ回避をしている）
    if(TARGET harfbuzz AND NOT TARGET harfbuzz::harfbuzz)
        add_library(harfbuzz::harfbuzz ALIAS harfbuzz)
    endif()
    set(GLYPHWARE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLYPHWARE_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(${GLYPHWARE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/glyphware)
endif()

#------------------------------------------------------------------------------
# richtext 共通ライブラリ（プラグインとサンプルで共有）
#------------------------------------------------------------------------------

set(RICHTEXT_LIB_SOURCES
    src/FontFace.cpp
    src/FontManager.cpp
    src/GlyphwareBackend.cpp
    src/TextStyle.cpp
    src/Appearance.cpp
    src/TextLayout.cpp
    src/ParagraphLayout.cpp
    src/GlyphRenderer.cpp
    src/TextRenderer.cpp
    src/StyledLayout.cpp
    src/TagParser.cpp
    src/TextureAtlas.cpp
    src/TimingInfo.cpp
)

add_library(richtext_lib STATIC ${RICHTEXT_LIB_SOURCES})
# thorvg の include path は target_link_libraries(thorvg) で INTERFACE 経由で
# 伝播するので明示する必要はない (umbrella 経由で生成される場合も同じ)。
target_include_directories(richtext_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/minikin/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/minikin/libs
)
target_link_libraries(richtext_lib PUBLIC thorvg minikin)

# glyphware が使える場合は既定バックエンドを有効化する。無い場合、
# richtext は FontManager::setFontBackend() でホストからバックエンドを
# 注入されないと動作しない。
if(TARGET glyphware)
    target_link_libraries(richtext_lib PUBLIC glyphware)
    target_compile_definitions(richtext_lib PUBLIC RICHTEXT_USE_GLYPHWARE)
else()
    message(STATUS "richtext: glyphware not available "
                   "(set GLYPHWARE_DIR); a font backend must be injected at runtime")
endif()

#------------------------------------------------------------------------------
# サンプル実行ファイル（トップレベルプロジェクトの場合のみビルド）
#------------------------------------------------------------------------------

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_executable(sample_render sample/sample_render.cpp)
    target_link_libraries(sample_render PRIVATE richtext_lib)

    add_executable(sample_sequential sample/sample_sequential.cpp)
    target_link_libraries(sample_sequential PRIVATE richtext_lib)

    add_executable(sample_texture sample/sample_texture.cpp)
    target_link_libraries(sample_texture PRIVATE richtext_lib)

    add_executable(sample_timing sample/sample_timing.cpp)
    target_link_libraries(sample_timing PRIVATE richtext_lib)

    add_executable(debug_bidi sample/debug_bidi.cpp)
    target_link_libraries(debug_bidi PRIVATE richtext_lib)

    #--------------------------------------------------------------------------
    # glyphware 統合 PoC（検証用・既定 OFF）
    #   cmake --preset x64-windows -DRICHTEXT_GLYPHWARE_POC=ON \
    #         -DGLYPHWARE_DIR=d:/work/kirikiri/glyphware
    #--------------------------------------------------------------------------
    option(RICHTEXT_GLYPHWARE_POC "Build the glyphware-backed MinikinFont PoC" OFF)

    if(RICHTEXT_GLYPHWARE_POC)
        if(NOT TARGET glyphware)
            message(FATAL_ERROR "RICHTEXT_GLYPHWARE_POC needs glyphware (set GLYPHWARE_DIR)")
        endif()
        add_executable(glyphware_poc sample/glyphware_poc.cpp)
        target_link_libraries(glyphware_poc PRIVATE richtext_lib glyphware)
    endif()
endif()

