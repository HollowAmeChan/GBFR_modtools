cmake_minimum_required(VERSION 3.28)
project(GBFRModtools VERSION 0.1.0 LANGUAGES CXX)

get_filename_component(GBFR_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8>")

option(GBFR_BUILD_TESTS "Build GBFR tests" ON)
option(GBFR_ENABLE_ASSIMP "Build the optional Assimp exchange adapter" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${GBFR_ROOT_DIR}/out/bin/${CMAKE_BUILD_TYPE}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${GBFR_ROOT_DIR}/out/lib/${CMAKE_BUILD_TYPE}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${GBFR_ROOT_DIR}/out/lib/${CMAKE_BUILD_TYPE}")
set(FETCHCONTENT_BASE_DIR "${GBFR_ROOT_DIR}/.deps" CACHE PATH "Dependency cache")
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

include(FetchContent)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG b61e56346a92cfcaf1f43a545ca37b0b32239654
  GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(imgui)

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

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
FetchContent_Declare(pugixml GIT_REPOSITORY https://github.com/zeux/pugixml.git GIT_TAG ee86beb30e4973f5feffe3ce63bfa4fbadf72f38 GIT_PROGRESS TRUE)
FetchContent_MakeAvailable(json pugixml)

if(GBFR_ENABLE_ASSIMP)
  set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(assimp GIT_REPOSITORY https://github.com/assimp/assimp.git GIT_TAG c35200e38ea8f058812b83de2ef32c6093b0ece2 GIT_PROGRESS TRUE)
  FetchContent_MakeAvailable(assimp)
endif()

add_subdirectory("${GBFR_ROOT_DIR}/src/gbfr_core" "${CMAKE_BINARY_DIR}/src/gbfr_core")
add_subdirectory("${GBFR_ROOT_DIR}/src/gbfr_formats" "${CMAKE_BINARY_DIR}/src/gbfr_formats")
add_subdirectory("${GBFR_ROOT_DIR}/src/gbfr_render" "${CMAKE_BINARY_DIR}/src/gbfr_render")
add_subdirectory("${GBFR_ROOT_DIR}/src/gbfr_editor" "${CMAKE_BINARY_DIR}/src/gbfr_editor")

if(GBFR_BUILD_TESTS)
  enable_testing()
  add_subdirectory("${GBFR_ROOT_DIR}/tests" "${CMAKE_BINARY_DIR}/tests")
endif()
