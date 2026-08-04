add_executable(gbfr_tests test_smoke.cpp)
target_link_libraries(gbfr_tests PRIVATE gbfr_core gbfr_formats gbfr_render)
target_compile_options(gbfr_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
target_compile_definitions(gbfr_tests PRIVATE GBFR_ROOT_DIR=L"${GBFR_ROOT_DIR}")
add_test(NAME gbfr_smoke COMMAND gbfr_tests)

add_executable(gbfr_material_tests test_material.cpp)
target_link_libraries(gbfr_material_tests PRIVATE gbfr_core gbfr_formats)
target_compile_options(gbfr_material_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
target_compile_definitions(gbfr_material_tests PRIVATE GBFR_ROOT_DIR=L"${GBFR_ROOT_DIR}")
add_test(NAME gbfr_material COMMAND gbfr_material_tests)

add_executable(gbfr_material_variants_tests test_material_variants.cpp)
target_link_libraries(gbfr_material_variants_tests PRIVATE gbfr_core gbfr_formats)
target_compile_options(gbfr_material_variants_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
add_test(NAME gbfr_material_variants COMMAND gbfr_material_variants_tests)

add_executable(gbfr_cloth_tests test_cloth.cpp)
target_link_libraries(gbfr_cloth_tests PRIVATE gbfr_core)
target_compile_options(gbfr_cloth_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
target_compile_definitions(gbfr_cloth_tests PRIVATE GBFR_ROOT_DIR=L"${GBFR_ROOT_DIR}")
add_test(NAME gbfr_cloth COMMAND gbfr_cloth_tests)

add_executable(gbfr_sop_tests test_sop.cpp)
target_link_libraries(gbfr_sop_tests PRIVATE gbfr_core gbfr_formats)
target_compile_options(gbfr_sop_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
add_test(NAME gbfr_sop COMMAND gbfr_sop_tests)

add_executable(gbfr_texture_tests test_texture.cpp)
target_link_libraries(gbfr_texture_tests PRIVATE gbfr_core gbfr_render)
target_compile_options(gbfr_texture_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
add_custom_command(TARGET gbfr_texture_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${GBFR_ROOT_DIR}/assets/shaders/preview.hlsl"
        "$<TARGET_FILE_DIR:gbfr_texture_tests>/preview.hlsl")
add_test(NAME gbfr_texture COMMAND gbfr_texture_tests)
