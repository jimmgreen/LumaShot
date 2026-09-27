cmake_minimum_required(VERSION 3.25)

project(LumaText VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "LumaText 0.1 supports Windows x64 only")
endif()

include(FetchContent)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(LUMATEXT_BUILD_SHARED "Build the LumaText DLL" ON)
option(LUMATEXT_BUILD_STATIC "Build the static LumaText library" ON)
option(LUMATEXT_BUILD_SAMPLES "Build sample applications" ON)
option(LUMATEXT_BUILD_TESTS "Build tests" ON)
option(LUMATEXT_BUILD_REFERENCE_TOOLS "Build CI reference tools" OFF)
option(LUMATEXT_USE_SYSTEM_FREETYPE "Use an installed FreeType package" OFF)
set(LUMATEXT_UNICODE_TEST_DATA "" CACHE FILEPATH
  "Optional path to Unicode GraphemeBreakTest.txt")

if(NOT LUMATEXT_BUILD_SHARED AND NOT LUMATEXT_BUILD_STATIC)
  message(FATAL_ERROR "Enable at least one of LUMATEXT_BUILD_SHARED or LUMATEXT_BUILD_STATIC")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

FetchContent_Declare(
  harfbuzz
  URL https://github.com/harfbuzz/harfbuzz/releases/download/10.4.0/harfbuzz-10.4.0.tar.xz
  URL_HASH SHA256=480b6d25014169300669aa1fc39fb356c142d5028324ea52b3a27648b9beaad8
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR cmake-not-used)
FetchContent_MakeAvailable(harfbuzz)

add_library(lumatext_harfbuzz OBJECT
  "${harfbuzz_SOURCE_DIR}/src/harfbuzz.cc")
target_include_directories(lumatext_harfbuzz SYSTEM PUBLIC
  "${harfbuzz_SOURCE_DIR}/src")
target_compile_definitions(lumatext_harfbuzz PRIVATE HB_STATIC)
target_compile_options(lumatext_harfbuzz PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W0 /bigobj /EHsc /utf-8>)

if(LUMATEXT_USE_SYSTEM_FREETYPE)
  find_package(Freetype 2.13.3 REQUIRED)
  set(LUMATEXT_FREETYPE_LINK Freetype::Freetype)
else()
  if(NOT LUMATEXT_BUILD_STATIC)
    set(SKIP_INSTALL_ALL ON)
  endif()
  set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    freetype
    URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-3.tar.gz
    URL_HASH SHA256=bc5c898e4756d373e0d991bab053036c5eb2aa7c0d5c67e8662ddc6da40c4103
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(freetype)
  if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
  endif()
  set(LUMATEXT_FREETYPE_LINK
    $<BUILD_INTERFACE:freetype>
    $<INSTALL_INTERFACE:Freetype::Freetype>)
endif()

set(LUMATEXT_SOURCES
  src/api.cpp
  src/context.cpp
  src/d2d_renderer.cpp
  src/font_face.cpp
  src/font_bridge.cpp
  src/glyph_provider.cpp
  src/line_renderer.cpp
  src/rasterizer.cpp
  src/text_layout.cpp
  src/unicode.cpp)

function(lumatext_configure_target target)
  target_sources(${target} PRIVATE ${LUMATEXT_SOURCES})
  target_include_directories(${target}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(${target} PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_compile_options(${target} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /EHsc /utf-8>)
  target_link_libraries(${target} PRIVATE
    lumatext_harfbuzz ${LUMATEXT_FREETYPE_LINK}
    d2d1 dwrite dxgi ole32)
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME lumatext
    DEBUG_POSTFIX d)
endfunction()

if(LUMATEXT_BUILD_SHARED)
  add_library(lumatext_shared SHARED)
  lumatext_configure_target(lumatext_shared)
  target_compile_definitions(lumatext_shared PRIVATE LUMATEXT_BUILD_DLL)
  set_target_properties(lumatext_shared PROPERTIES EXPORT_NAME Shared)
endif()

if(LUMATEXT_BUILD_STATIC)
  add_library(lumatext_static STATIC)
  lumatext_configure_target(lumatext_static)
  target_compile_definitions(lumatext_static PUBLIC LUMATEXT_STATIC)
  set_target_properties(lumatext_static PROPERTIES
    OUTPUT_NAME lumatext_static
    EXPORT_NAME Static
    INTERFACE_LINK_LIBRARIES "Freetype::Freetype;d2d1;dwrite;dxgi;ole32")
endif()

if(TARGET lumatext_shared)
  set(LUMATEXT_DEFAULT_TARGET lumatext_shared)
  set(LUMATEXT_INSTALLED_DEFAULT_TARGET LumaText::Shared)
else()
  set(LUMATEXT_DEFAULT_TARGET lumatext_static)
  set(LUMATEXT_INSTALLED_DEFAULT_TARGET LumaText::Static)
endif()

add_library(lumatext_core INTERFACE)
target_link_libraries(lumatext_core INTERFACE
  $<BUILD_INTERFACE:${LUMATEXT_DEFAULT_TARGET}>
  $<INSTALL_INTERFACE:${LUMATEXT_INSTALLED_DEFAULT_TARGET}>)
set_target_properties(lumatext_core PROPERTIES EXPORT_NAME Core)
add_library(LumaText::Core ALIAS lumatext_core)

foreach(component IN ITEMS d2d d3d11 input)
  add_library(lumatext_${component} INTERFACE)
  target_link_libraries(lumatext_${component} INTERFACE lumatext_core)
endforeach()
set_target_properties(lumatext_d2d PROPERTIES EXPORT_NAME D2D)
set_target_properties(lumatext_d3d11 PROPERTIES EXPORT_NAME D3D11)
set_target_properties(lumatext_input PROPERTIES EXPORT_NAME Input)
add_library(LumaText::D2D ALIAS lumatext_d2d)
add_library(LumaText::D3D11 ALIAS lumatext_d3d11)
add_library(LumaText::Input ALIAS lumatext_input)

function(lumatext_stage_runtime_dependencies target)
  set(runtime_files)
  set(runtime_dependencies)
  if(TARGET lumatext_shared)
    list(APPEND runtime_files "$<TARGET_FILE:lumatext_shared>")
    list(APPEND runtime_dependencies lumatext_shared)
  endif()
  set(runtime_target "${target}_runtime")
  if(runtime_files)
    add_custom_target(${runtime_target} ALL
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${target}>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${runtime_files}
        "$<TARGET_FILE_DIR:${target}>"
      DEPENDS ${runtime_dependencies})
  else()
    add_custom_target(${runtime_target} ALL)
  endif()
  add_dependencies(${target} ${runtime_target})
endfunction()

if(LUMATEXT_BUILD_SAMPLES)
  add_subdirectory(samples/gallery)
  add_subdirectory(samples/compare)
endif()

if(LUMATEXT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(LUMATEXT_BUILD_REFERENCE_TOOLS)
  add_executable(lumatext_reference_win tools/windows_reference.cpp)
  target_include_directories(lumatext_reference_win PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    "${harfbuzz_SOURCE_DIR}/src"
    "${freetype_SOURCE_DIR}/include")
  target_compile_definitions(lumatext_reference_win PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  target_compile_options(lumatext_reference_win PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /EHsc /utf-8>)
  target_link_libraries(lumatext_reference_win PRIVATE
    LumaText::Core d2d1 windowscodecs ole32 bcrypt)
  lumatext_stage_runtime_dependencies(lumatext_reference_win)
endif()

set(LUMATEXT_INSTALL_TARGETS
  lumatext_core lumatext_d2d lumatext_d3d11 lumatext_input)
if(TARGET lumatext_shared)
  list(APPEND LUMATEXT_INSTALL_TARGETS lumatext_shared)
endif()
if(TARGET lumatext_static)
  list(APPEND LUMATEXT_INSTALL_TARGETS lumatext_static)
endif()

install(TARGETS ${LUMATEXT_INSTALL_TARGETS}
  EXPORT LumaTextTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT LumaTextTargets NAMESPACE LumaText::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LumaText)
configure_package_config_file(cmake/LumaTextConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/LumaTextConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LumaText)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/LumaTextConfigVersion.cmake
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/LumaTextConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/LumaTextConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LumaText)
install(FILES LICENSE LICENSES/FreeType.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/LumaText/licenses)
install(FILES LICENSES/HarfBuzz.txt
  DESTINATION ${CMAKE_INSTALL_DATADIR}/LumaText/licenses)
install(FILES LICENSES/Unicode.txt
  DESTINATION ${CMAKE_INSTALL_DATADIR}/LumaText/licenses)
