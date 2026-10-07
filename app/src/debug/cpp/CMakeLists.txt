cmake_minimum_required(VERSION 3.22.1)
project(artdcevm_tests LANGUAGES C CXX)
# Pair NDK r27 flexible page sizes with the alignment required by 16 KB devices.
add_link_options("LINKER:-z,common-page-size=16384")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_library(artdcevm_test_support SHARED testing/native_test_bridge.cc
    testing/special_members_probe.cc testing/storage_probe.cc testing/native_implementation_probe.cc
    testing/hierarchy_probe.cc testing/static_visibility_probe.cc)
  target_compile_features(artdcevm_test_support PRIVATE cxx_std_17)
  target_include_directories(artdcevm_test_support PRIVATE ../../../../third_party/jvmti)
  # Independent agents exercise only the public JVMTI function table.
  add_library(artdcevm_client_test SHARED testing/standard_jvmti_client.cc)
  target_include_directories(artdcevm_client_test PRIVATE ../../../../third_party/jvmti)
  add_library(artdcevm_restricted_test SHARED testing/standard_jvmti_client.cc)
  target_compile_definitions(artdcevm_restricted_test PRIVATE ARTDCEVM_RESTRICTED_CLIENT=1)
  target_include_directories(artdcevm_restricted_test PRIVATE ../../../../third_party/jvmti)
  add_library(artdcevm_second_client_test SHARED testing/standard_jvmti_client.cc)
  target_compile_definitions(artdcevm_second_client_test PRIVATE ARTDCEVM_SECOND_CLIENT=1)
  target_include_directories(artdcevm_second_client_test PRIVATE ../../../../third_party/jvmti)
endif()
