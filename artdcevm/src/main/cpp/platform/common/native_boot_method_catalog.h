#pragma once
#include "platform/common/runnable_executor.h"
#include <jvmti.h>
#include "platform/common/source_handle_roots.h"
#include "platform/common/readable_mapping_scope.h"
#include <cstdint>
#include <algorithm>
#include <string>
#include <vector>
namespace artdcevm::native_boot_method_catalog {
// Public ART JVMTI GetBytecodes returns DEX code units, not JVM bytecode.
// This classification is evidence about this body only, never a recovery grant.
enum class Body {kPrimitiveLocal,kUnknown};
// Optional exact PC must identify an instruction start, never an operand unit.
Body Classify(const unsigned char*,size_t,const char* descriptor,size_t exact_pc=SIZE_MAX) noexcept;
struct Method {
 void* native_method=nullptr;
 size_t declaring_root=0;
 jint modifiers=0,max_locals=0;
 std::string name,declaring_signature,descriptor;
 std::vector<unsigned char> dex;
 Body body=Body::kUnknown;
};
struct Codec {
 void* manager=nullptr;
 void* (*decode)(void*,jmethodID)=nullptr;
 abi::common::FromReflectedMethod leaf=nullptr;
};
// Strong JNI Class roots stay live until Clear. Native ArtMethod identities are
// accepted only when the source codec agrees with the independent reflected leaf.
// No raw movable Class escapes an ordinary callback. Consumers must import roots
// into their actual SourcePool before stopping and bind the exact source PC.
class Catalog {
 public:
 bool Prepare(JNIEnv*,jvmtiEnv*,const Codec&,std::string*);
 void Clear(JNIEnv*) noexcept;
 const Method* Find(void*method)const noexcept{
  if(!complete_||!method)return nullptr;
  const auto key=reinterpret_cast<uintptr_t>(method);
  auto it=std::lower_bound(methods_.begin(),methods_.end(),key,[](const Method&m,uintptr_t p){return reinterpret_cast<uintptr_t>(m.native_method)<p;});
  return it!=methods_.end()&&it->native_method==method?&*it:nullptr;
 }
 const std::vector<jclass>& Roots()const noexcept{return roots_;}
 const std::vector<Method>& Methods()const noexcept{return methods_;}
 bool Complete()const noexcept{return complete_;}
 private:
 std::vector<jclass> roots_;
 std::vector<Method> methods_;
 bool complete_=false;
};
// Per-source-run GC slots. Construction is ordinary; lookup borrows only the
// actual stopped reader and validates the fresh declaring Class through it.
class Rooted {
 public:
 bool Import(const Catalog&catalog,const runnable_jni::Context&context,
             const source_handle_roots::Service&roots,source_handle_roots::Pool&pool,
             const void*reference_owner,void*(*declaring)(const void*,void*)noexcept,std::string*error){
  if(catalog_||!catalog.Complete()||!reference_owner||!declaring)return false;
  slots_.reserve(catalog.Roots().size());
  // This entire import is one original Runnable, no-JNI/no-safepoint interval.
  // Native scope allocation never allocates a managed object. The fresh view
  // only bounds Thread/JNIEnv reads: every root still checks actual Current,
  // Runnable state/environment, named decode and the original owned slot write.
  // New native scopes are owned allocations, not targets of this mapping view.
  method_identity::ScopedReadableMappings mappings;
  if(!mappings.Ready()){if(error)*error="Boot root import mapping snapshot is incomplete";return false;}
  for(auto strong:catalog.Roots()){
   void*klass=nullptr;
   if(!roots.BorrowOwnedStrongGlobal(context,strong,&klass)||!klass)return false;
   // No JNI/allocation of a managed object between this borrow and owned slot.
   void*slot=pool.Add(klass,error);if(!slot)return false;slots_.push_back(slot);
  }
  catalog_=&catalog;pool_=&pool;reference_owner_=reference_owner;declaring_=declaring;return true;
 }
 const Method* Lookup(void*method,const source_handle_roots::StoppedReader&reader,
    const modern_field_metadata::ReadContext&context)const noexcept{
  if(!catalog_||!pool_||!reader.FinalRootWindow()||!context.owner||!context.extent)return nullptr;
  const Method*m=catalog_->Find(method);if(!m||m->declaring_root>=slots_.size())return nullptr;
  void*klass=nullptr;size_t extent=0;
  if(!pool_->ReadStopped(reader,slots_[m->declaring_root],&klass)||!klass||!context.extent(context.owner,method,&extent)||extent<4)return nullptr;
  // Fixed source ArtMethod starts with GcRoot<Class>; the calibrated metadata
  // reference reader decodes the same source root representation/read barrier.
  return declaring_(reference_owner_,method)==klass?m:nullptr;
 }
 private:
 const Catalog*catalog_=nullptr;const source_handle_roots::Pool*pool_=nullptr;
 const void*reference_owner_=nullptr;void*(*declaring_)(const void*,void*)noexcept=nullptr;
 std::vector<void*>slots_;
};
}
