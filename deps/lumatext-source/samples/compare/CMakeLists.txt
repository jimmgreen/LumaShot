add_executable(lumatext_compare WIN32 main.cpp)
target_compile_definitions(lumatext_compare PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(lumatext_compare PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /EHsc /utf-8>)
target_link_libraries(lumatext_compare PRIVATE LumaText::Core d2d1 dwrite windowscodecs shell32 ole32)
lumatext_stage_runtime_dependencies(lumatext_compare)
