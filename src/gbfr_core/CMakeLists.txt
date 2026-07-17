add_library(gbfr_core STATIC
  src/log.cpp
  src/workspace.cpp
)
target_include_directories(gbfr_core PUBLIC "${GBFR_ROOT_DIR}/include")
target_compile_definitions(gbfr_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(gbfr_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
target_link_libraries(gbfr_core PUBLIC nlohmann_json::nlohmann_json bcrypt)
