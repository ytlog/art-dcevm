cmake_minimum_required(VERSION 3.22.1)
project(artdcevm_tests LANGUAGES CXX)
# Pair NDK r27 flexible page sizes with the alignment required by 16 KB devices.
add_link_options("LINKER:-z,common-page-size=16384")
# An independent user agent that only uses the public JVMTI function table.
add_library(artdcevm_client_test SHARED testing/standard_jvmti_client.cc)
target_compile_features(artdcevm_client_test PRIVATE cxx_std_17)
target_include_directories(artdcevm_client_test PRIVATE ../../../../third_party/jvmti)
