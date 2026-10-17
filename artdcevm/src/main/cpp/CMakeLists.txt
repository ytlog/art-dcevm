cmake_minimum_required(VERSION 3.22.1)
project(artdcevm LANGUAGES CXX)
if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
  message(FATAL_ERROR "Supported ABI: arm64-v8a")
endif()
# Pair NDK r27 flexible page sizes with the alignment required by 16 KB devices.
add_link_options("LINKER:-z,common-page-size=16384")

# Official ShadowHook 2.0.1 from Maven, exposed through prefab.
find_package(shadowhook REQUIRED CONFIG)

add_library(artdcevm SHARED
  core/agent.cc
  evolution/dex_schema.cc
  platform/common/hierarchy_snapshot.cc
  platform/common/schema_snapshot.cc
  platform/common/structural_policy.cc
  platform/common/class_layout.cc
  platform/common/runtime_thread.cc
  platform/common/transaction_context.cc
  platform/common/member_retirement.cc
  platform/common/member_mapping.cc
  platform/common/verification_redirect.cc
  platform/common/interface_dispatch.cc
  platform/common/interface_snapshot.cc
  platform/common/interface_redefinition.cc
  platform/common/structural_redefine_engine.cc)
target_compile_features(artdcevm PRIVATE cxx_std_17)
option(ARTDCEVM_INTERFACE_ABORT_TESTS "Build app-owned interface abort tests" OFF)
if(ARTDCEVM_INTERFACE_ABORT_TESTS)
  if(PROJECT_IS_TOP_LEVEL OR NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR "Interface abort tests require the app Debug subproject")
  endif()
  target_compile_definitions(artdcevm PRIVATE ARTDCEVM_INTERFACE_ABORT_TESTS)
endif()
target_include_directories(artdcevm PRIVATE . ../../../../third_party/jvmti)
target_link_libraries(artdcevm PRIVATE shadowhook::shadowhook log)
