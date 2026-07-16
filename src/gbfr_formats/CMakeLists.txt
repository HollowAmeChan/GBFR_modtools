add_library(gbfr_formats STATIC src/model.cpp)
target_include_directories(gbfr_formats PUBLIC "${CMAKE_SOURCE_DIR}/include")
target_compile_options(gbfr_formats PRIVATE /W4 /permissive- /Zc:__cplusplus)
target_link_libraries(gbfr_formats PUBLIC gbfr_core pugixml::pugixml)
