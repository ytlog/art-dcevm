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
  hooks/shadow_hook.cc
  platform/common/schema_snapshot.cc
  platform/common/structural_policy.cc
  platform/common/structural_redefine_engine.cc)
target_compile_features(artdcevm PRIVATE cxx_std_17)
target_include_directories(artdcevm PRIVATE . ../../../../third_party/jvmti)
target_link_libraries(artdcevm PRIVATE shadowhook::shadowhook log)
