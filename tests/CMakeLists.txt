add_executable(gbfr_tests test_smoke.cpp)
target_link_libraries(gbfr_tests PRIVATE gbfr_core gbfr_formats)
target_compile_options(gbfr_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
add_test(NAME gbfr_smoke COMMAND gbfr_tests)
