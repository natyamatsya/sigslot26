
if(SIGSLOT_RUN_TESTS)
    add_custom_target(tests COMMENT "Build all the unit tests.")
    add_custom_command(
        TARGET tests
        POST_BUILD
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    )
endif()

# Include Catch2 test discovery
include(CTest)
include(Catch)

macro(pal_create_test target ut)
    add_executable(${target} "${ut}")
    if(SIGSLOT_RUN_TESTS)
        add_dependencies(tests ${target})
    endif()
    sigslot_set_properties(${target} PRIVATE)
    target_link_libraries(${target} PRIVATE Pal::Sigslot Catch2::Catch2WithMain)
    target_compile_definitions(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_SCL_SECURE_NO_WARNINGS>)
    
    # Discover Catch2 tests with timeout for async tests
    # TSan on macOS significantly slows down execution
    if(target MATCHES "async")
        catch_discover_tests(${target} PROPERTIES TIMEOUT 120)
    else()
        catch_discover_tests(${target})
    endif()
endmacro()

file(
    GLOB_RECURSE UNIT_TESTS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    "*.cpp"
)
foreach(ut IN LISTS UNIT_TESTS)
    string(REPLACE ".cpp" "" target ${ut})
    string(REPLACE "/" "-" target ${target})

    if(target MATCHES "boost")
        if(TARGET Boost::system)
            message(STATUS "build ${target}")
            pal_create_test(${target} "${ut}")
            target_link_libraries(${target} PRIVATE Boost::system)
        endif()
    elseif(target MATCHES "qcoro")
        # QCoro tests require both Qt6 and QCoro6
        if(TARGET Qt6::Core AND TARGET QCoro6::Core)
            message(STATUS "build ${target}")
            pal_create_test(${target} "${ut}")
            target_link_libraries(${target} PRIVATE Qt6::Core QCoro6::Core)
            target_compile_definitions(${target} PRIVATE SIGSLOT_HAVE_QCORO)
            set_target_properties(${target} PROPERTIES AUTOMOC ON)
        endif()
    elseif(target MATCHES "qt")
        if(TARGET Qt6::Core)
            message(STATUS "build ${target}")
            pal_create_test(${target} "${ut}")
            target_link_libraries(${target} PRIVATE Qt6::Core)
            set_target_properties(${target} PROPERTIES AUTOMOC ON)
        endif()
    elseif(target MATCHES "bench")
        if(NOT SIGSLOT_SANITIZE_THREADS)
            message(STATUS "build ${target}")
            pal_create_test(${target} "${ut}")
        endif()
    else()
        message(STATUS "build ${target}")
        pal_create_test(${target} "${ut}")
    endif()
endforeach()
