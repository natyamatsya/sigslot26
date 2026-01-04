
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

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/async/CMakeLists.txt)
    add_subdirectory(async)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/qt/CMakeLists.txt)
    add_subdirectory(qt)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/signal-performance/CMakeLists.txt)
    add_subdirectory(signal-performance)
endif()
add_subdirectory(signal-sbo)

# Option to repeat async tests (useful for catching flaky race conditions in CI)
# This defines a compile-time constant used by the test framework
set(SIGSLOT_ASYNC_TEST_REPEAT 1 CACHE STRING "Number of times to repeat async tests (default: 1)")

# Option to configure shared thread pool size for execution tests
set(SIGSLOT_TEST_POOL_SIZE 4 CACHE STRING "Thread pool size for execution tests (default: 4)")

macro(pal_create_test target ut)
    add_executable(${target} "${ut}")
    if(SIGSLOT_RUN_TESTS)
        add_dependencies(tests ${target})
    endif()
    sigslot_set_properties(${target} PRIVATE)
    target_link_libraries(${target} PRIVATE Pal::Sigslot Catch2::Catch2WithMain)
    target_compile_definitions(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_SCL_SECURE_NO_WARNINGS>)
    
    # Discover Catch2 tests with timeout and repetition for threading tests
    # TSan on macOS significantly slows down execution (up to 10x slower)
    if(target MATCHES "async|threaded|signal-inline")
        # Pass repeat count and pool size as compile definitions
        target_compile_definitions(${target} PRIVATE 
            SIGSLOT_TEST_REPEAT=${SIGSLOT_ASYNC_TEST_REPEAT}
            SIGSLOT_TEST_POOL_SIZE=${SIGSLOT_TEST_POOL_SIZE})
        catch_discover_tests(${target} PROPERTIES TIMEOUT 60)
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
