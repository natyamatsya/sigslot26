# Benchmark targets

add_executable(signal-benchmarks signal-benchmarks.cpp)
target_link_libraries(signal-benchmarks PRIVATE sigslot_benchmark)

add_executable(threaded-benchmarks threaded-benchmarks.cpp)
target_link_libraries(threaded-benchmarks PRIVATE sigslot_benchmark)

add_executable(sbo-benchmarks sbo-benchmarks.cpp)
target_link_libraries(sbo-benchmarks PRIVATE sigslot_benchmark)

add_executable(slot-variant-benchmarks slot-variant-benchmarks.cpp)
target_link_libraries(slot-variant-benchmarks PRIVATE sigslot_benchmark)
