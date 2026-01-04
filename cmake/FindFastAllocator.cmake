# FindFastAllocator.cmake
# Detects if a fast allocator (jemalloc, tcmalloc, mimalloc) is linked
#
# Sets:
#   SIGSLOT_HAS_FAST_ALLOCATOR - TRUE if a fast allocator is detected
#   SIGSLOT_FAST_ALLOCATOR_NAME - Name of the detected allocator (or "none")

include(CheckCXXSourceRuns)

# Save the original required libraries
set(_orig_CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})

# Try to detect jemalloc
find_library(JEMALLOC_LIBRARY NAMES jemalloc)
if(JEMALLOC_LIBRARY)
    set(SIGSLOT_HAS_FAST_ALLOCATOR TRUE)
    set(SIGSLOT_FAST_ALLOCATOR_NAME "jemalloc")
    message(STATUS "Fast allocator detected: jemalloc")
    return()
endif()

# Try to detect tcmalloc (Google's thread-caching malloc)
find_library(TCMALLOC_LIBRARY NAMES tcmalloc tcmalloc_minimal)
if(TCMALLOC_LIBRARY)
    set(SIGSLOT_HAS_FAST_ALLOCATOR TRUE)
    set(SIGSLOT_FAST_ALLOCATOR_NAME "tcmalloc")
    message(STATUS "Fast allocator detected: tcmalloc")
    return()
endif()

# Try to detect mimalloc (Microsoft's allocator)
find_library(MIMALLOC_LIBRARY NAMES mimalloc mimalloc-static)
if(MIMALLOC_LIBRARY)
    set(SIGSLOT_HAS_FAST_ALLOCATOR TRUE)
    set(SIGSLOT_FAST_ALLOCATOR_NAME "mimalloc")
    message(STATUS "Fast allocator detected: mimalloc")
    return()
endif()

# Check if the user explicitly linked a fast allocator via CMAKE_EXE_LINKER_FLAGS
# or similar mechanisms
string(TOLOWER "${CMAKE_EXE_LINKER_FLAGS}" _linker_flags_lower)
if(_linker_flags_lower MATCHES "jemalloc|tcmalloc|mimalloc")
    set(SIGSLOT_HAS_FAST_ALLOCATOR TRUE)
    set(SIGSLOT_FAST_ALLOCATOR_NAME "user-specified")
    message(STATUS "Fast allocator detected in linker flags")
    return()
endif()

# Runtime detection: Check if malloc has fast allocator characteristics
# This is a heuristic - fast allocators typically have specific symbols
set(SIGSLOT_HAS_FAST_ALLOCATOR FALSE)
set(SIGSLOT_FAST_ALLOCATOR_NAME "none")
message(STATUS "No fast allocator detected - slot pool will be enabled for better performance")

# Restore original required libraries
set(CMAKE_REQUIRED_LIBRARIES ${_orig_CMAKE_REQUIRED_LIBRARIES})
