cmake_minimum_required(VERSION 3.22.1)
project(artdcevm LANGUAGES C CXX)
add_library(artdcevm SHARED core/agent.cc platform/backend_factory.cc
  platform/jvmti/public_backend.cc)
if(ANDROID_ABI STREQUAL "arm64-v8a")
  find_package(shadowhook REQUIRED CONFIG)
  target_compile_definitions(artdcevm PRIVATE ARTDCEVM_ARM64=1)
  target_sources(artdcevm PRIVATE support/elf_modules.cc hooks/shadow_hook_engine.cc
    platform/symbols/art_symbol_compat.cc
    evolution/dex_schema.cc evolution/migration_plan.cc
    platform/android13/android13_backend.cc platform/android13/jvmti_redefiner.cc
    platform/android13/hierarchy_batch_hooks.cc
    platform/android13/hierarchy_resolution_hooks.cc
    platform/android13/class_loader_hooks.cc
    platform/android13/instance_allocation_hooks.cc
    platform/android13/async_deopt_hooks.cc
    platform/android13/pending_suspend_deopt.cc
    platform/android13/suspend_deopt_hooks.cc
    platform/android13/retired_method_registry.cc
    platform/android13/retired_method_registry_bridge.cc
    platform/android13/retired_member_guard.cc
    platform/android13/retired_field_registry.cc
    platform/android13/final_field_guard_state.cc
    platform/android13/retired_field_registry_bridge.cc
    platform/android13/schema_snapshot.cc platform/android13/evolution_planner.cc
    platform/android13/field_evolution_policy.cc platform/android13/field_validation.cc
    platform/android13/redefinition_failure.cc platform/android13/redefinition_input.cc
    platform/android13/jni_method_targets.cc
    platform/android13/runtime_access.cc
    platform/android13/native_object_access.cc
    platform/android13/handle_root_scope.cc
    platform/android13/hierarchy_validation.cc
    platform/android13/structural_method_updates.cc
    platform/android13/loaded_class_guards.cc
    platform/android13/allocation_validation.cc
    platform/android13/member_decode.cc
    platform/android13/validation_metadata.cc
    platform/android13/field_copy.cc
    platform/android13/field_copy_binding.cc
    platform/android13/field_copy_metadata.cc
    platform/android13/field_copy_storage.cc
    platform/android13/heap_reference.cc
    platform/android13/method_validation.cc platform/android13/class_validation.cc
    platform/android13/modifier_evolution_policy.cc
    platform/android13/validation_hooks.cc platform/android13/source_engine_loader.cc
    platform/android13/nterp_dispatch.cc
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
endif()
