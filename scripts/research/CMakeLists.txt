add_executable(gbfr_shader_reflect shader_reflect.cpp)
target_compile_options(gbfr_shader_reflect PRIVATE /W4 /permissive- /Zc:__cplusplus)
target_compile_definitions(gbfr_shader_reflect PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(gbfr_shader_reflect PRIVATE nlohmann_json::nlohmann_json d3dcompiler dxguid)

add_executable(gbfr_shader_disasm shader_disasm.cpp)
target_compile_options(gbfr_shader_disasm PRIVATE /W4 /permissive- /Zc:__cplusplus)
target_compile_definitions(gbfr_shader_disasm PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(gbfr_shader_disasm PRIVATE d3dcompiler)

add_executable(gbfr_msgpack_to_json msgpack_to_json.cpp)
target_compile_options(gbfr_msgpack_to_json PRIVATE /W4 /permissive- /Zc:__cplusplus)
target_compile_definitions(gbfr_msgpack_to_json PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(gbfr_msgpack_to_json PRIVATE nlohmann_json::nlohmann_json)
