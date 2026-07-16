add_executable(GBFRModtools WIN32 src/main.cpp)
target_include_directories(GBFRModtools PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_compile_options(GBFRModtools PRIVATE /W4 /permissive- /Zc:__cplusplus)
target_compile_definitions(GBFRModtools PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(GBFRModtools PRIVATE gbfr_core gbfr_formats gbfr_render imgui comdlg32 shell32)
add_custom_target(gbfr_preview_shaders ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/assets/shaders"
          "$<TARGET_FILE_DIR:GBFRModtools>/shaders"
)
add_dependencies(GBFRModtools gbfr_preview_shaders)
