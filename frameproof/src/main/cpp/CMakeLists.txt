cmake_minimum_required(VERSION 3.22.1)
project(owned_frameproof LANGUAGES C CXX)
get_filename_component(REPOSITORY "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
add_subdirectory("${REPOSITORY}/artdcevm/src/main/cpp" "${CMAKE_CURRENT_BINARY_DIR}/shared-artdcevm")
# JNI fixture exports only; this does not set ART runtime/graph debug policy.
target_compile_definitions(artdcevm PRIVATE ARTDCEVM_TESTING=1)
target_sources(artdcevm PRIVATE "${CMAKE_CURRENT_LIST_DIR}/frame_bootstrap.cc")
target_include_directories(artdcevm PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
