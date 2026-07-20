add_executable(digitalforge_benchmark
    BenchmarkMain.cpp
)
target_link_libraries(digitalforge_benchmark PRIVATE digitalforge_core)
