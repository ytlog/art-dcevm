#include "platform/android13/handle_root_scope_abi.h"
#include "handle_scope-inl.h"
#include "thread.h"
#include "thread-current-inl.h"
#include "scoped_thread_state_change-inl.h"
#include <cstring>
#include <type_traits>
using namespace artdcevm::android13::handle_root_abi;
static_assert(art::Thread::TopHandleScopeOffset<art::PointerSize::k64>().SizeValue()==kTopHandleScopeOffset);
static_assert(art::Thread::ExceptionOffset<art::PointerSize::k64>().SizeValue()==kExceptionOffset);
static_assert(art::HandleScope::LinkOffset(art::PointerSize::k64)==offsetof(ScopeHeader,link));
static_assert(art::HandleScope::NumberOfReferencesOffset(art::PointerSize::k64)==offsetof(ScopeHeader,count));
static_assert(art::HandleScope::ReferencesOffset(art::PointerSize::k64)==kReferencesOffset);
static_assert(sizeof(art::StackReference<art::mirror::Object>)==4);
static_assert(sizeof(art::Handle<art::mirror::Class>)==sizeof(void*));
static_assert(std::is_trivially_copyable_v<art::Handle<art::mirror::Class>>);
extern "C" int ArtDcevmSourceProbeHandleRootScope(void* thread,void* scope,uint32_t count) {
  if(art::kIsDebugBuild || art::kObjPtrPoisoning || art::kPoisonHeapReferences) return 1;
  auto* self=static_cast<art::Thread*>(thread);
  if(self!=art::Thread::Current()) return 2;
  void* top; void* exception;
  std::memcpy(&top,static_cast<unsigned char*>(thread)+kTopHandleScopeOffset,sizeof(top));
  std::memcpy(&exception,static_cast<unsigned char*>(thread)+kExceptionOffset,sizeof(exception));
  if(top!=self->GetTopHandleScope() || exception!=self->GetException()) return 3;
  if(!scope) return 0;
  if(top!=scope) return 4;
  auto* roots=static_cast<art::HandleScope*>(scope);
  if(roots->IsVariableSized() || roots->NumberOfReferences()!=count) return 5;
  for(uint32_t i=0;i<count;++i)
    if(roots->GetReference(i)!=nullptr || roots->GetHandle(i).GetReference()!=
       reinterpret_cast<art::StackReference<art::mirror::Object>*>(static_cast<unsigned char*>(scope)+kReferencesOffset+size_t{i}*4)) return 6;
  return 0;
}
extern "C" int ArtDcevmSourceCalibrateHandleRootScopes(int(*calibrate)(void*)) {
  auto* self=art::Thread::Current();
  if(!self || !calibrate) return 1;
  art::ScopedObjectAccess soa(self);
  return calibrate(self);
}
