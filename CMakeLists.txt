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
# richtext 共通ライブラリ（プラグインとサンプルで共有）
#------------------------------------------------------------------------------

set(RICHTEXT_LIB_SOURCES
    src/FontFace.cpp
    src/FontManager.cpp
    src/TextStyle.cpp
    src/Appearance.cpp
    src/TextLayout.cpp
    src/ParagraphLayout.cpp
    src/GlyphRenderer.cpp
    src/TextRenderer.cpp
    src/StyledLayout.cpp
    src/TagParser.cpp
    src/TextureAtlas.cpp
)

add_library(richtext_lib STATIC ${RICHTEXT_LIB_SOURCES})
target_include_directories(richtext_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/thorvg/inc
    ${CMAKE_CURRENT_BINARY_DIR}/ext/thorvg
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/minikin/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/minikin/libs
)
target_link_libraries(richtext_lib PUBLIC thorvg minikin)

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

    add_executable(debug_bidi sample/debug_bidi.cpp)
    target_link_libraries(debug_bidi PRIVATE richtext_lib)
endif()

