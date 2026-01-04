# SBO tests
add_executable(test_signal-sbo signal-sbo.cpp)
if(SIGSLOT_RUN_TESTS)
    add_dependencies(tests test_signal-sbo)
endif()
sigslot_set_properties(test_signal-sbo PRIVATE)
target_link_libraries(test_signal-sbo PRIVATE Pal::Sigslot Catch2::Catch2WithMain)
target_compile_definitions(test_signal-sbo PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_SCL_SECURE_NO_WARNINGS>)

catch_discover_tests(test_signal-sbo)
