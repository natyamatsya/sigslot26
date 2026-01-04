cmake_minimum_required(VERSION 3.30)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Disable C++20 modules scanning - stdexec headers aren't compatible with clang-scan-deps
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

project(
    pal_sigslot
    VERSION 1.2.0
    LANGUAGES CXX
)

include(SigslotUtils)

### options
option(SIGSLOT_COMPILE_EXAMPLES "Compile optional exemples" ON)
option(SIGSLOT_COMPILE_TESTS "Compile tests" ON)
option(SIGSLOT_RUN_TESTS "Compile and run tests" OFF)
option(SIGSLOT_REDUCE_COMPILE_TIME "Attempt at reducing code size and compilation time" OFF)
option(SIGSLOT_ENABLE_STDEXEC "Enable std::execution support via NVIDIA stdexec" ON)
option(SIGSLOT_ENABLE_BENCHMARK "Enable Google Benchmark support" OFF)
option(SIGSLOT_USE_intrusive-ptr "Use intrusive reference counting instead of shared_ptr (experimental)" OFF)
option(SIGSLOT_USE_SLOT_VARIANT "Use variant-based slot storage to eliminate virtual dispatch (experimental)" OFF)
set(SIGSLOT_SLOT_ALLOCATOR "AUTO" CACHE STRING "Slot allocator: AUTO, DEFAULT, PMR, or ARENA")
set_property(CACHE SIGSLOT_SLOT_ALLOCATOR PROPERTY STRINGS AUTO DEFAULT PMR ARENA)

# Detect fast allocators (jemalloc, tcmalloc, mimalloc)
include(FindFastAllocator)

# Auto-configure slot allocator based on detection
if(SIGSLOT_SLOT_ALLOCATOR STREQUAL "AUTO")
    if(SIGSLOT_HAS_FAST_ALLOCATOR)
        set(SIGSLOT_SLOT_ALLOCATOR "DEFAULT")
        message(STATUS "Slot allocator: DEFAULT (using ${SIGSLOT_FAST_ALLOCATOR_NAME})")
    else()
        # Use DEFAULT even without fast allocator - ARENA has threading issues
        set(SIGSLOT_SLOT_ALLOCATOR "DEFAULT")
        message(STATUS "No fast allocator detected - slot pool will be enabled for better performance")
    endif()
else()
    message(STATUS "Slot allocator: ${SIGSLOT_SLOT_ALLOCATOR} (user-specified)")
endif()

# Set compile definition based on allocator choice
if(SIGSLOT_SLOT_ALLOCATOR STREQUAL "ARENA")
    set(SIGSLOT_USE_SLOT_POOL 2)
elseif(SIGSLOT_SLOT_ALLOCATOR STREQUAL "PMR")
    set(SIGSLOT_USE_SLOT_POOL 1)
else()
    set(SIGSLOT_USE_SLOT_POOL 0)
endif()

find_package(Threads REQUIRED)

### Fetch NVIDIA stdexec for std::execution support (optional)
if(SIGSLOT_ENABLE_STDEXEC)
    include(FetchContent)
    FetchContent_Declare(
        stdexec
        GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git
        GIT_TAG main
        GIT_SHALLOW TRUE
    )
    set(STDEXEC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(STDEXEC_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(stdexec)
endif()

### Fetch Catch2 for testing
if(SIGSLOT_COMPILE_TESTS OR SIGSLOT_RUN_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.2
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

### sigslot library
add_library(sigslot INTERFACE)
add_library(Pal::Sigslot ALIAS sigslot)
target_compile_features(sigslot INTERFACE cxx_std_23)
set_target_properties(sigslot PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(
    sigslot INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
)
target_compile_definitions(sigslot INTERFACE 
    $<$<BOOL:${SIGSLOT_REDUCE_COMPILE_TIME}>:SIGSLOT_REDUCE_COMPILE_TIME>
    $<$<BOOL:${SIGSLOT_ENABLE_STDEXEC}>:SIGSLOT_HAVE_STDEXEC>
    $<$<NOT:$<EQUAL:${SIGSLOT_USE_SLOT_POOL},0>>:SIGSLOT_USE_SLOT_POOL=${SIGSLOT_USE_SLOT_POOL}>
    $<$<BOOL:${SIGSLOT_USE_intrusive-ptr}>:SIGSLOT_USE_INTRUSIVE_PTR>
    $<$<BOOL:${SIGSLOT_USE_SLOT_VARIANT}>:SIGSLOT_USE_SLOT_VARIANT>
)
if(SIGSLOT_ENABLE_STDEXEC)
    target_link_libraries(sigslot INTERFACE STDEXEC::stdexec)
endif()
target_link_options(
    sigslot
    INTERFACE
    # We deactivate ICF on windows compilers because it interferes with signal
    # disconnection by making identical functions not unique.
    $<$<CXX_COMPILER_ID:MSVC>:/OPT:NOICF>
    $<$<BOOL:${SIGSLOT_COMPILER_CLANGCL}>:-Xlinker;/OPT:NOICF>
)
target_link_libraries(sigslot INTERFACE Threads::Threads)
set_target_properties(sigslot PROPERTIES EXPORT_NAME Sigslot)

#installation
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PalSigslotConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/PalSigslotConfig.cmake
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/PalSigslot"
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/PalSigslotConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    TARGETS sigslot
    EXPORT PalSigslotTargets
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    EXPORT PalSigslotTargets
    FILE PalSigslotTargets.cmake
    NAMESPACE Pal::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/PalSigslot"
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PalSigslotConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/PalSigslotConfigVersion.cmake
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/PalSigslot"
)
export(
    TARGETS sigslot
    NAMESPACE Pal::
    FILE ${CMAKE_CURRENT_BINARY_DIR}/PalSigslotTargets.cmake
)

# Install the Config and ConfigVersion files
install(
    FILES ${version_file} ${config_file}
    DESTINATION ${cmake_dir}
    COMPONENT ${projectname_target}Export
)

# dependencies for examples and tests
if(SIGSLOT_COMPILE_TESTS OR SIGSLOT_COMPILE_EXAMPLES)
    find_package(
        Boost
        COMPONENTS system
        QUIET
    ) # test of boost bridge with smart pointers
    # Qt 6.5+ for Qt adapter support
    find_package(Qt6 6.5 COMPONENTS Core QUIET)
    
    # Optional QCoro support for Qt coroutines
    option(SIGSLOT_ENABLE_QCORO "Enable QCoro integration for Qt coroutines" OFF)
    if(SIGSLOT_ENABLE_QCORO AND Qt6_FOUND)
        find_package(QCoro6 QUIET)
        if(QCoro6_FOUND)
            message(STATUS "QCoro found - Qt coroutine support enabled")
        else()
            message(WARNING "SIGSLOT_ENABLE_QCORO=ON but QCoro6 not found")
        endif()
    endif()
endif()

if(SIGSLOT_COMPILE_EXAMPLES)
    add_subdirectory(example)
endif()

if(SIGSLOT_COMPILE_TESTS OR SIGSLOT_RUN_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

### Documentation with Doxygen
find_package(Doxygen QUIET)

if(DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME "sigslot26")
    set(DOXYGEN_PROJECT_NUMBER "${PROJECT_VERSION}")
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/doc/api")
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_PRIVATE NO)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_SOURCE_DIR}/doc/index.md")
    set(DOXYGEN_EXCLUDE_PATTERNS "*/test/*" "*/example/*" "*/build/*")
    
    doxygen_add_docs(docs
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/doc/index.md"
        COMMENT "Generating API documentation with Doxygen"
    )
    message(STATUS "Doxygen found: ${DOXYGEN_EXECUTABLE}")
    message(STATUS "  Run 'cmake --build build --target docs' to generate documentation")
else()
    message(STATUS "Doxygen not found - documentation target disabled")
    message(STATUS "  Install Doxygen: brew install doxygen")
endif()

### Google Benchmark support
if(SIGSLOT_ENABLE_BENCHMARK)
    add_subdirectory(benchmark)
endif()
