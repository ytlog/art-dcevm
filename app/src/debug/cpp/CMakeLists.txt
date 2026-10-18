cmake_minimum_required(VERSION 3.22.1)
project(artdcevm_tests LANGUAGES CXX)
# Pair NDK r27 flexible page sizes with the alignment required by 16 KB devices.
add_link_options("LINKER:-z,common-page-size=16384")
# An independent user agent that only uses the public JVMTI function table.
add_library(artdcevm_client_test SHARED testing/standard_jvmti_client.cc)
target_compile_features(artdcevm_client_test PRIVATE cxx_std_17)
target_include_directories(artdcevm_client_test PRIVATE ../../../../third_party/jvmti)
# JNI loops plus opt-in ART 13/JVMTI probes, confined to the Debug APK.
add_library(artdcevm_bench SHARED testing/hook_overhead_benchmark.cc
            testing/jit_instrumentation_probe.cc)
target_compile_features(artdcevm_bench PRIVATE cxx_std_17)
target_include_directories(artdcevm_bench PRIVATE ../../../../third_party/jvmti
                           ../../../../artdcevm/src/main/cpp)
find_package(shadowhook REQUIRED CONFIG)
target_link_libraries(artdcevm_bench PRIVATE shadowhook::shadowhook)

# The lab compiles the shared engine sources with an explicit Debug-only flag.
# Library-owned Debug/Release builds keep the CMake option OFF.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../../artdcevm/src/main/cpp"
                 "${CMAKE_CURRENT_BINARY_DIR}/test-engine")

if(ARTDCEVM_INTERFACE_ABORT_TESTS)
  target_sources(artdcevm PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/testing/fault_injection.cc")
endif()
