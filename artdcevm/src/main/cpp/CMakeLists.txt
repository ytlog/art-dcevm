cmake_minimum_required(VERSION 3.22.1)
project(artdcevm LANGUAGES C CXX)
if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
  message(FATAL_ERROR "Supported ABI: arm64-v8a")
endif()
# Pair NDK r27 flexible page sizes with the alignment required by 16 KB devices.
add_link_options("LINKER:-z,common-page-size=16384")
add_library(artdcevm SHARED core/agent.cc platform/backend_factory.cc
  platform/jvmti/public_backend.cc evolution/checked_method_resolution.cc)
  target_sources(artdcevm PRIVATE
    third_party/xz_embedded/xz_dec_stream.c
    third_party/xz_embedded/xz_dec_lzma2.c
    third_party/xz_embedded/xz_crc32.c
    third_party/xz_embedded/xz_crc64.c)
  set_source_files_properties(
    third_party/xz_embedded/xz_dec_stream.c
    third_party/xz_embedded/xz_dec_lzma2.c
    third_party/xz_embedded/xz_crc32.c
    third_party/xz_embedded/xz_crc64.c
    PROPERTIES COMPILE_OPTIONS "-fvisibility=hidden")
  target_compile_definitions(artdcevm PRIVATE XZ_USE_CRC64)

  find_package(shadowhook REQUIRED CONFIG)
  target_sources(artdcevm PRIVATE support/elf_modules.cc support/elf_symbol_names.cc hooks/shadow_hook_engine.cc)
  target_link_libraries(artdcevm PRIVATE shadowhook::shadowhook)
  enable_language(ASM)
  target_sources(artdcevm PRIVATE platform/symbols/art_symbol_compat.cc
    evolution/dex_schema.cc evolution/migration_plan.cc
    platform/android13/android13_backend.cc platform/android13/jvmti_redefiner.cc
    platform/android13/hierarchy_batch_hooks.cc
    platform/android13/hierarchy_resolution_hooks.cc
    platform/android13/class_loader_hooks.cc
    platform/android13/instance_allocation_hooks.cc
    platform/common/object_replacement_map.cc
    platform/android13/structural_commit.cc
    platform/android13/selective_deopt.cc
    platform/android13/instrumentation_listener.cc
    platform/android13/thread_suspend_scope.cc
    platform/android13/device_compiler_diagnostics.cc
    platform/android13/compiled_frame_diagnostics.cc
    platform/android13/device_diagnostics.cc
    platform/common/runtime_service_calibration.cc
    platform/android15/runtime_services.cc
    platform/android13/runtime_receiver_probe.cc
    platform/common/runtime_receiver_probe.cc
    platform/android13/owned_jit_receiver_probe.cc
    platform/android13/device_profile.cc
    platform/android13/owned_calibration.cc
    platform/common/owned_context_consumer.cc
    platform/common/owned_context_calibration.cc
    platform/android17/runtime_receiver_probe.cc
    platform/android17/runtime_services.cc
    platform/common/heap_collect_observer.cc
    platform/common/owned_jit_receivers.cc
    platform/android17/method_table.cc
    platform/common/modern_method_services.cc
    platform/common/native_method_table.cc
    platform/common/method_identity_calibration.cc
    platform/common/native_cache_service.cc
    platform/common/native_source_stop.cc
    platform/common/whole_process_frames.cc
    platform/common/native_required_cleanup.cc
    platform/common/thread_state_calibration.cc
    platform/common/native_source_commit.cc
    platform/common/modern_source_commit.cc
    platform/common/source_commit.cc
    platform/common/owned_structural_transaction.cc
    platform/common/native_structural_reflection.cc
    platform/common/resolution_invalidation_plan.cc
    platform/common/native_member_provider.cc
    platform/common/bootstrap_dex_oracle.cc
    platform/common/production_bootstrap.cc
    platform/common/rooted_field_identities.cc
    platform/common/capability_backend.cc
    platform/common/native_owned_removal.cc
    platform/common/redefinition_gate.cc
    platform/common/native_owned_removal_probe.cc
    platform/common/contextual_field_id.cc
    platform/common/modern_method_handle_guards.cc
    platform/common/native_removal_policy.cc
    platform/common/native_schema_policy.cc
    platform/common/native_schema_guard_verifier.cc
    platform/common/native_schema_binding.cc
    platform/common/prepared_field_copy.cc
    platform/common/rooted_hierarchy_allocator.cc
    platform/common/source_handle_roots.cc
    platform/common/native_hierarchy_cache.cc
    platform/common/native_hierarchy_source_cache.cc
    platform/common/source_allocation_transaction.cc
    platform/common/native_hierarchy_graph.cc
    platform/common/native_hierarchy_allocator.cc
    platform/common/native_hierarchy_source_checks.cc
    platform/common/native_hierarchy_session.cc
    platform/common/native_source_class_loading.cc
    platform/common/native_hierarchy_private_ledger.cc
    platform/common/native_source_dex_registry.cc
    platform/common/native_class_lookup.cc
    platform/common/structural_methods.cc
    platform/common/native_method_evolution_policy.cc
    platform/common/native_abstract_instances.cc
    platform/common/native_scope_executor.cc
    platform/common/runnable_jni_bridge.cc
    platform/common/runnable_global_ref_bridge.cc
    platform/common/runnable_executor.cc
    platform/android13/runnable_jni_diagnostics.cc
    platform/jni_access/capability_selection.cc
    platform/jni_access/owned_probe.cc
    platform/common/owned_active_frame_probe.cc
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
    platform/common/modern_final_field_guard.cc
    platform/common/native_final_write_guard.cc
    platform/common/native_final_write_listener.cc
    platform/common/native_reference_decoder.cc
    platform/common/native_final_field_calibration.cc
    platform/common/native_final_field_service.cc
    platform/common/native_final_field_aliases.cc
    platform/common/modern_field_guards.cc
    platform/common/modern_retirement_cleanup.cc
    platform/common/modern_method_guards.cc
    platform/common/retired_method_registry.cc
    platform/android13/retired_method_registry_bridge.cc
    platform/android13/retired_member_guard.cc
    platform/common/retired_field_registry.cc
    platform/android13/final_field_guard_state.cc
    platform/android13/retired_field_registry_bridge.cc
    platform/common/schema_snapshot.cc platform/common/evolution_planner.cc
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
    platform/common/exclusive_owned_roots.cc
    platform/common/modern_field_storage.cc
    platform/common/modern_field_metadata.cc
    platform/common/native_interface_graph.cc
    platform/common/modern_field_probe.cc
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
