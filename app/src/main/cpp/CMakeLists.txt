cmake_minimum_required(VERSION 3.22.1)
project(artdcevm LANGUAGES C CXX)
if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
  message(FATAL_ERROR "The current device backend is arm64-v8a only")
endif()
add_subdirectory(../../../../third_party/shadowhook/src ${CMAKE_CURRENT_BINARY_DIR}/shadowhook)
# Upstream targets NDK 23; use a stable warning set with this project's NDK 27.
# Keep -Werror, and leave the vendored source and upstream build file unchanged.
foreach(dependency shadowhook shadowhook_nothing)
  get_target_property(warnings ${dependency} COMPILE_OPTIONS)
  list(REMOVE_ITEM warnings -Weverything)
  set_property(TARGET ${dependency} PROPERTY COMPILE_OPTIONS ${warnings} -Wall -Wextra)
endforeach()
# The upstream export map also names ARM32-only assembly stubs in an ARM64 build.
target_link_options(shadowhook PRIVATE -Wl,--undefined-version)
add_library(artdcevm SHARED core/agent.cc platform/backend_factory.cc
  support/elf_modules.cc hooks/shadow_hook_engine.cc testing/native_test_bridge.cc
  testing/special_members_probe.cc testing/storage_probe.cc testing/native_implementation_probe.cc testing/hierarchy_probe.cc
  testing/static_visibility_probe.cc
  evolution/dex_schema.cc evolution/migration_plan.cc
  platform/android13/android13_backend.cc platform/android13/jvmti_redefiner.cc
  platform/android13/schema_snapshot.cc platform/android13/evolution_planner.cc
  platform/android13/validation_hooks.cc platform/android13/source_engine_loader.cc
  platform/android13/retired_method_hooks.cc platform/android13/retired_field_hooks.cc)
target_compile_features(artdcevm PRIVATE cxx_std_17)
target_include_directories(artdcevm PRIVATE . ../../../../third_party/jvmti ../../../../third_party/shadowhook/src/include)
target_link_libraries(artdcevm PRIVATE log dl shadowhook)
# A separate Agent exercises only the public JVMTI function table.
add_library(artdcevm_client_test SHARED testing/standard_jvmti_client.cc)
target_include_directories(artdcevm_client_test PRIVATE ../../../../third_party/jvmti)
add_library(artdcevm_restricted_test SHARED testing/standard_jvmti_client.cc)
target_compile_definitions(artdcevm_restricted_test PRIVATE ARTDCEVM_RESTRICTED_CLIENT=1)
target_include_directories(artdcevm_restricted_test PRIVATE ../../../../third_party/jvmti)
