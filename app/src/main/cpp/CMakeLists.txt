cmake_minimum_required(VERSION 3.22.1)
project(artdcevm LANGUAGES C CXX)
add_library(artdcevm SHARED core/agent.cc platform/backend_factory.cc
  platform/jvmti/public_backend.cc)
if(ANDROID_ABI STREQUAL "arm64-v8a")
  find_package(shadowhook REQUIRED CONFIG)
  target_compile_definitions(artdcevm PRIVATE ARTDCEVM_ARM64=1)
  target_sources(artdcevm PRIVATE support/elf_modules.cc hooks/shadow_hook_engine.cc
    evolution/dex_schema.cc evolution/migration_plan.cc
    platform/android13/android13_backend.cc platform/android13/jvmti_redefiner.cc
    platform/android13/schema_snapshot.cc platform/android13/evolution_planner.cc
    platform/android13/validation_hooks.cc platform/android13/source_engine_loader.cc
    platform/android13/retired_method_hooks.cc platform/android13/retired_field_hooks.cc)
  target_link_libraries(artdcevm PRIVATE shadowhook::shadowhook)
elseif(NOT ANDROID_ABI STREQUAL "x86_64")
  message(FATAL_ERROR "Supported ABIs: arm64-v8a and x86_64")
endif()
target_compile_features(artdcevm PRIVATE cxx_std_17)
target_include_directories(artdcevm PRIVATE . ../../../../third_party/jvmti)
target_link_libraries(artdcevm PRIVATE log dl)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(artdcevm PRIVATE ARTDCEVM_TESTING=1)
  add_library(artdcevm_test_support SHARED testing/native_test_bridge.cc
    testing/special_members_probe.cc testing/storage_probe.cc testing/native_implementation_probe.cc
    testing/hierarchy_probe.cc testing/static_visibility_probe.cc)
  target_compile_features(artdcevm_test_support PRIVATE cxx_std_17)
  target_include_directories(artdcevm_test_support PRIVATE ../../../../third_party/jvmti)
  # Independent Agents exercise only the public JVMTI function table.
  add_library(artdcevm_client_test SHARED testing/standard_jvmti_client.cc)
  target_include_directories(artdcevm_client_test PRIVATE ../../../../third_party/jvmti)
  add_library(artdcevm_restricted_test SHARED testing/standard_jvmti_client.cc)
  target_compile_definitions(artdcevm_restricted_test PRIVATE ARTDCEVM_RESTRICTED_CLIENT=1)
  target_include_directories(artdcevm_restricted_test PRIVATE ../../../../third_party/jvmti)
  add_library(artdcevm_second_client_test SHARED testing/standard_jvmti_client.cc)
  target_compile_definitions(artdcevm_second_client_test PRIVATE ARTDCEVM_SECOND_CLIENT=1)
  target_include_directories(artdcevm_second_client_test PRIVATE ../../../../third_party/jvmti)
endif()
