add_executable(lumatext_gallery WIN32 main.cpp)
target_compile_definitions(lumatext_gallery PRIVATE
  NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(lumatext_gallery PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /EHsc /utf-8>)
target_link_libraries(lumatext_gallery PRIVATE LumaText::Core d2d1 dwrite)
lumatext_stage_runtime_dependencies(lumatext_gallery)
