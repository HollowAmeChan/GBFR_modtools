add_library(gbfr_render STATIC src/d3d11_context.cpp src/preview_renderer.cpp)
target_include_directories(gbfr_render PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_compile_options(gbfr_render PRIVATE /W4 /permissive- /Zc:__cplusplus)
target_link_libraries(gbfr_render PUBLIC gbfr_core gbfr_formats d3d11 dxgi d3dcompiler)
