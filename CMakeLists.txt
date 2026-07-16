cmake_minimum_required(VERSION 3.28)
project(GBFRModtools VERSION 0.1.0 LANGUAGES CXX)

option(GBFR_BUILD_TESTS "Build GBFR tests" ON)
option(GBFR_ENABLE_ASSIMP "Build the optional Assimp exchange adapter" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/out/bin/${CMAKE_BUILD_TYPE}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/out/lib/${CMAKE_BUILD_TYPE}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/out/lib/${CMAKE_BUILD_TYPE}")
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH "Dependency cache")

include(FetchContent)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.8-docking
  GIT_SHALLOW TRUE
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()

add_library(imgui STATIC
  "${imgui_SOURCE_DIR}/imgui.cpp"
  "${imgui_SOURCE_DIR}/imgui_draw.cpp"
  "${imgui_SOURCE_DIR}/imgui_tables.cpp"
  "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
)
target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_DOCKING)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_Declare(pugixml GIT_REPOSITORY https://github.com/zeux/pugixml.git GIT_TAG v1.15 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(json pugixml)

if(GBFR_ENABLE_ASSIMP)
  set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(assimp GIT_REPOSITORY https://github.com/assimp/assimp.git GIT_TAG v5.4.3 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(assimp)
endif()

add_subdirectory(src/gbfr_core)
add_subdirectory(src/gbfr_formats)
add_subdirectory(src/gbfr_render)
add_subdirectory(src/gbfr_editor)

if(GBFR_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
