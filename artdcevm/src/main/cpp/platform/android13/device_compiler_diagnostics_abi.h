#pragma once
#include <cstddef>
#include <cstdint>
#include "platform/android13/mutex_abi.h"
namespace artdcevm::android13::compiler_diagnostics_abi {
// Borrowed platform libc++ v1 storage; no host STL constructors/destructors.
struct Vector { void* begin;void* end;void* capacity; };
struct Tree {void* begin;void* root;size_t size;};
struct HashSet {uint8_t empty_functors[4];size_t elements,buckets,expand;bool owns;void* data;double min_load,max_load;};
struct Options {
 uint32_t filter;size_t huge,large,num_methods,inline_max;
 uint32_t isa;void* features;Vector no_inline,dex_files;HashSet image_classes;
 void* verification;uint8_t compiler_type,image_type;bool multi_image,compile_test,baseline,debuggable;
};
struct Compiler {void* vtable;Options* options;};
struct Jit {void* vtable;void* cache;void* options;void* pool;};
struct ArenaVector {Vector values;void* allocator;};
struct ArenaMap {Tree values;void* allocator;};
struct BitVector {void* vtable;void* storage;uint32_t size;void* allocator;bool expandable;};
struct BitVectorArray {void* vtable;size_t columns,rows;BitVector data;};
struct Graph {
 void* allocator;void* arena_stack;void* handle_cache[7];ArenaVector blocks,reverse,linear;
 BitVectorArray reachability;void* entry;void* exit;uint16_t max_out,regs,in_regs;size_t temp_slots;
 bool bounds,try_catch,monitor,simd,loops,irreducible,critical,dead_safe,debuggable;
 int32_t instruction_id;void* dex;uint32_t method_index,invoke_type;bool ssa;
 uint32_t cha,isa;void* null_constant;ArenaMap ints,floats,longs,doubles;void* current_method;void* method;
};
struct Instrumentation {
 uint64_t force_id;bool stubs;uint32_t level;bool flags[11];Tree requested;
 Vector listeners[10];void* deoptimized_lock;
 struct UnorderedSet {void* buckets;size_t bucket_count;void* first;size_t size;float max_load;} deoptimized;
 size_t alloc_counter;bool alloc_instrumented;
};
using Mutex = mutex_abi::Mutex;
struct Compat {Tree disabled,reported;Mutex lock;};
// Starts exactly at Runtime::instrumentation_ (existing calibrated getter).
struct RuntimePolicy {
 Instrumentation instrumentation;void* main_group;void* system_group;void* system_loader;
 bool dump_gc;void* transactions;uint32_t verify;Vector abis;uint32_t target_sdk;Compat compat;
 bool implicit_null,implicit_so,implicit_suspend,no_sig_chain,force_bridge,bridge_loaded;
 bool native_debuggable,async_exceptions,non_standard_exits,java_debuggable;
};
struct Suspend {bool was_on;};
static_assert(sizeof(HashSet)==64 && offsetof(Options,debuggable)==181);
static_assert(sizeof(ArenaVector)==32 && sizeof(ArenaMap)==32 && sizeof(BitVectorArray)==64);
static_assert(offsetof(Graph,method)==456);
static_assert(sizeof(Instrumentation)==360 && sizeof(Mutex)==40);
static_assert(offsetof(RuntimePolicy,java_debuggable)==537);
inline constexpr uint32_t kIntrinsic=0x80000000,kPreCompiled=0x00800000,kDontCompile=0x02000000;
}
