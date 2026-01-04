# SBO Analysis Tool
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
option(SIGSLOT_ENABLE_SBO_ANALYSIS "Enable SBO binary layout analysis tool" ON)

if(SIGSLOT_ENABLE_SBO_ANALYSIS)
    # Create the analysis executable
    add_executable(analyze-sbo-layout analyze-sbo-layout.cpp)
    
    # Set C++ standard
    set_target_properties(analyze-sbo-layout PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
    )
    
    # Provide nlohmann/json for JSON output
    set(NLOHMANN_JSON_VERSION v3.11.2)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json_single
        URL https://github.com/nlohmann/json/releases/download/${NLOHMANN_JSON_VERSION}/json.tar.xz
        SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/nlohmann_json_single-src
    )
    FetchContent_GetProperties(nlohmann_json_single)
    if(NOT nlohmann_json_single_POPULATED)
        FetchContent_Populate(nlohmann_json_single)
        add_library(nlohmann_json INTERFACE)
        target_include_directories(nlohmann_json INTERFACE
            ${nlohmann_json_single_SOURCE_DIR}/single_include)
    endif()

    target_link_libraries(analyze-sbo-layout PRIVATE nlohmann_json Pal::Sigslot)
    target_compile_definitions(analyze-sbo-layout PRIVATE HAS_NLOHMANN_JSON)
    
    # Add to parent target if available
    if(TARGET sigslot_benchmark)
        add_dependencies(sigslot_benchmark analyze-sbo-layout)
    endif()
    
    # Installation
    install(TARGETS analyze-sbo-layout DESTINATION ${CMAKE_INSTALL_BINDIR})
    
    # Slot Variant Analysis Tool (Phase 6.1)
    add_executable(analyze-slot-variant analyze-slot-variant.cpp)
    set_target_properties(analyze-slot-variant PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
    )
    target_link_libraries(analyze-slot-variant PRIVATE Pal::Sigslot)
    install(TARGETS analyze-slot-variant DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
