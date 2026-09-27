cmake_minimum_required(VERSION 3.25)
project(LumaTextPackageSmoke LANGUAGES CXX)

find_package(LumaText 0.1 CONFIG REQUIRED)

foreach(component IN ITEMS Core D2D D3D11 Input Shared Static)
  if(NOT TARGET LumaText::${component})
    message(FATAL_ERROR "Missing installed target LumaText::${component}")
  endif()
endforeach()

add_executable(lumatext_package_static_smoke main.cpp)
target_compile_features(lumatext_package_static_smoke PRIVATE cxx_std_20)
target_link_libraries(lumatext_package_static_smoke PRIVATE LumaText::Static)

add_executable(lumatext_package_shared_smoke main.cpp)
target_compile_features(lumatext_package_shared_smoke PRIVATE cxx_std_20)
target_link_libraries(lumatext_package_shared_smoke PRIVATE LumaText::Shared)
