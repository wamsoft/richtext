cmake_minimum_required(VERSION 3.31)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
    add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
    add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")
    project(richtext)
endif()

if(NOT TARGET thorvg)
    # ThorVG configuration - static library, SW engine only
    set(TVG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(TVG_THREADS ON CACHE BOOL "" FORCE)
    set(TVG_PARTIAL ON CACHE BOOL "" FORCE)
    set(TVG_SIMD ON CACHE BOOL "" FORCE)
    set(TVG_LOG OFF CACHE BOOL "" FORCE)
    set(TVG_FILE_IO OFF CACHE BOOL "" FORCE)

    # Enable SW engine only
    set(TVG_ENGINE_SW ON CACHE BOOL "" FORCE)
    set(TVG_ENGINE_GL OFF CACHE BOOL "" FORCE)
    set(TVG_ENGINE_WG OFF CACHE BOOL "" FORCE)

    # Loaders - enable what we need
    set(TVG_LOADER_SVG OFF CACHE BOOL "" FORCE)
    set(TVG_LOADER_PNG OFF CACHE BOOL "" FORCE)
    set(TVG_LOADER_JPG OFF CACHE BOOL "" FORCE)
    set(TVG_LOADER_LOTTIE OFF CACHE BOOL "" FORCE)
    set(TVG_LOADER_TTF OFF CACHE BOOL "" FORCE)
    set(TVG_LOADER_WEBP OFF CACHE BOOL "" FORCE)

    # Disable savers and tools
    set(TVG_SAVER_GIF OFF CACHE BOOL "" FORCE)
    set(TVG_TOOL_SVG2PNG OFF CACHE BOOL "" FORCE)
    set(TVG_TOOL_LOTTIE2GIF OFF CACHE BOOL "" FORCE)

    # Disable bindings and tests
    set(TVG_BINDINGS_CAPI OFF CACHE BOOL "" FORCE)
    set(TVG_BUILD_TESTS OFF CACHE BOOL "" FORCE)

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

