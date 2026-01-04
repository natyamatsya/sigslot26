# Google Benchmark support for sigslot
option(SIGSLOT_ENABLE_BENCHMARK "Enable Google Benchmark support" OFF)

if(SIGSLOT_ENABLE_BENCHMARK)
    # Find Google Benchmark
    find_package(benchmark QUIET)
    
    if(NOT benchmark_FOUND)
        # Use FetchContent if not found
        include(FetchContent)
        FetchContent_Declare(
            googlebenchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG v1.8.3
        )
        # Disable GoogleTest to avoid dependency issues
        set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable testing in Google Benchmark")
        set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "Disable GTest tests")
        FetchContent_MakeAvailable(googlebenchmark)
    endif()
    
    # Create benchmark library
    add_library(sigslot_benchmark INTERFACE)
    target_link_libraries(sigslot_benchmark INTERFACE 
        sigslot
        benchmark::benchmark
    )
    
    # Enable LTO for benchmarks to get maximum optimization
    if(MSVC)
        target_compile_options(sigslot_benchmark INTERFACE /GL)
        target_link_options(sigslot_benchmark INTERFACE /LTCG)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(sigslot_benchmark INTERFACE -flto)
        target_link_options(sigslot_benchmark INTERFACE -flto)
    endif()
    
    # Add benchmark targets
    add_subdirectory(benchmarks)
    
    # Add SBO analysis tool
    add_subdirectory(analysis)
endif()
