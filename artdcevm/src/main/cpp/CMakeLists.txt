cmake_minimum_required(VERSION 3.22.1)
project(artdcevm LANGUAGES C CXX)
# Pair NDK r27 flexible page sizes with the alignment required by 16 KB devices.
add_link_options("LINKER:-z,common-page-size=16384")
add_library(artdcevm SHARED core/agent.cc platform/backend_factory.cc
  platform/jvmti/public_backend.cc evolution/checked_method_resolution.cc)
if(ANDROID_ABI STREQUAL "arm64-v8a")
  enable_language(ASM)
  find_package(shadowhook REQUIRED CONFIG)
  target_compile_definitions(artdcevm PRIVATE ARTDCEVM_ARM64=1)
  target_sources(artdcevm PRIVATE support/elf_modules.cc support/elf_symbol_names.cc hooks/shadow_hook_engine.cc
    platform/symbols/art_symbol_compat.cc
    evolution/dex_schema.cc evolution/migration_plan.cc
    platform/android13/android13_backend.cc platform/android13/jvmti_redefiner.cc
    platform/android13/hierarchy_batch_hooks.cc
    platform/android13/hierarchy_resolution_hooks.cc
    platform/android13/class_loader_hooks.cc
    platform/android13/instance_allocation_hooks.cc
    platform/android13/object_replacement_map.cc
    platform/android13/structural_commit.cc
    platform/android13/selective_deopt.cc
    platform/android13/instrumentation_listener.cc
    platform/android13/thread_suspend_scope.cc
    platform/android13/device_compiler_diagnostics.cc
    platform/android13/compiled_frame_diagnostics.cc
    platform/android13/device_diagnostics.cc
    platform/android13/runtime_receiver_probe.cc
    platform/android13/owned_jit_receiver_probe.cc
    platform/android13/device_profile.cc
    platform/android13/owned_calibration.cc
    platform/common/owned_context_consumer.cc
    platform/common/owned_context_calibration.cc
    platform/common/method_identity_calibration.cc
    platform/common/source_commit.cc
    platform/common/runnable_jni_bridge.cc
    platform/common/runnable_global_ref_bridge.cc
    platform/common/runnable_executor.cc
    platform/android13/runnable_jni_diagnostics.cc
    platform/jni_access/capability_selection.cc
    platform/jni_access/owned_probe.cc
    platform/arch/arm64/owned_context_long_jump.S
    platform/common/method_cache_lookup.cc
    platform/android13/checked_method_provider.cc
    platform/android13/method_lookup.cc
    platform/android13/method_invoke.cc
    platform/android13/method_access.cc
    platform/android13/dex_cache_method_reader.cc
    platform/android13/allocator_membership.cc
    platform/android13/reflection_bindings.cc
    platform/android13/failure_calibration.cc
    platform/android13/transaction_metadata.cc
    platform/android13/device_dex_input.cc
    platform/android13/evolution_transaction.cc
    platform/android13/member_access_guards.cc
    platform/android13/hierarchy_allocation.cc
    platform/android13/structural_member_mapping.cc
    platform/android13/reflected_field_visit.cc
    platform/android13/structural_reflective.cc
    platform/android13/stack_access.cc
    platform/android13/async_deopt_hooks.cc
    platform/android13/frame_recovery.cc
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
    platform/common/field_copy.cc
    platform/android13/field_copy_binding.cc
    platform/android13/field_copy_metadata.cc
    platform/android13/field_copy_storage.cc
    platform/android13/heap_reference.cc
    platform/android13/method_validation.cc platform/android13/class_validation.cc
    platform/android13/modifier_evolution_policy.cc
    platform/android13/validation_hooks.cc platform/android13/evolution_runtime.cc
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
  if(ARTDCEVM_FORCE_CHECKED_HANDLES_TEST)
    target_compile_definitions(artdcevm PRIVATE ARTDCEVM_FORCE_CHECKED_HANDLES_TEST=1)
  endif()
  if(ARTDCEVM_FORCE_ALLOCATOR_MEMBERSHIP_TEST)
    target_compile_definitions(artdcevm PRIVATE ARTDCEVM_FORCE_ALLOCATOR_MEMBERSHIP_TEST=1)
  endif()
  if(ARTDCEVM_FORCE_REFLECTION_BINDINGS_TEST)
    target_compile_definitions(artdcevm PRIVATE ARTDCEVM_FORCE_REFLECTION_BINDINGS_TEST=1)
  endif()
endif()
