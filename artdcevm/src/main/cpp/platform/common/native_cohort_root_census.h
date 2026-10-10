#pragma once
#include "platform/common/native_typed_heap_walk.h"
#include "platform/common/source_handle_roots.h"
#include "platform/abi/common/jni_current_thread.h"
#include <array>

namespace artdcevm::native_cohort_root_census {
// A synchronous strong-root + incoming-reference census only. Weak JNI roots,
// native pointers, active frames and object existence without incoming edges
// are NOT covered. Success grants neither no-escape nor narrowing permission.
class Census final {
 public:
  static constexpr size_t kMaximumMembers=64;
  enum class Failure {None,Stage,Owner,RootRead,RootStream,MissingSlot,
                      ForeignRoot,ThreadIdentity,HeapStream,ForeignIncoming};
  struct Result {
    Failure failure=Failure::Stage;
    native_typed_heap_walk::RootReport roots{};
    size_t owned_slots=0,incoming_edges=0,internal_edges=0;
    uint32_t observed_thread_id=0;
    bool completed=false;
  };
  // Only stable exact owned slots are retained. No managed value or map is
  // retained across the ordinary-to-stopped boundary. Pool and its source
  // service must outlive this synchronous consumer. Current is the original
  // named Thread::Current leaf, not a Boolean ownership callback.
  bool Stage(HookEngine& hooks,const source_handle_roots::Pool& pool,
             const source_handle_roots::Service& service,
             const runnable_jni::Context& ordinary,void*(*current)(),
             const void*const* slots,size_t count) noexcept {
    staged_=false;count_=0;
    if(!ordinary.self||!ordinary.env||!ordinary.vm||!current||
       current()!=ordinary.self||!pool.UsesService(service)||!service.Calibrated()||!pool.ReadyForSourceAccess()||!slots||
       !count||count>kMaximumMembers)return false;
    const auto named_current=reinterpret_cast<void*(*)()>(
        hooks.FindSymbol("libart.so",abi::common::kCurrentThread));
    if(!named_current||named_current!=current)return false;
    // Public environment->VM identity is checked before borrowing any managed
    // value. Source BorrowHandle then independently checks environment/self.
    JavaVM* actual_vm=nullptr;
    if(ordinary.env->GetJavaVM(&actual_vm)!=JNI_OK||actual_vm!=ordinary.vm||
       current()!=ordinary.self||!pool.ReadyForSourceAccess())return false;
    auto barrier=reinterpret_cast<void*(*)(void*)>(
        hooks.FindSymbol("libart.so","artReadBarrierForRootSlow"));
    if(!barrier)return false;
    for(size_t i=0;i<count;++i){void* value=nullptr;void* source_value=nullptr;
      if(!slots[i]||!pool.Read(slots[i],&value)||!value||
         !service.BorrowHandle(ordinary,slots[i],&source_value)||source_value!=value)return false;
      for(size_t j=0;j<i;++j)if(slots[j]==slots[i])return false;
      slots_[i]=slots[i];
    }
    pool_=&pool;ordinary_=ordinary;current_=current;barrier_=barrier;
    count_=count;staged_=true;return true;
  }
  Result Observe(const native_source_stop::Session& stop,
                 const modern_field_metadata::ReadContext& context,
                 const native_typed_heap_walk::RootProvenance& roots,
                 const native_typed_heap_walk::Service& heap)const noexcept {
    Result result;if(!staged_)return result;
    Episode episode(*this,stop,context,result);
    if(!episode.FinalRootWindow()){result.failure=Failure::Owner;return result;}
    // Preflight every retained slot against the same real stop and fresh view.
    for(size_t i=0;i<count_;++i){void* value=nullptr;
      if(!episode.ReadStoppedRoot(slots_[i],&value)||!value){result.failure=Failure::RootRead;return result;}}
    result.failure=Failure::None;
    result.roots=roots.Observe(stop,ordinary_.self,context,episode);
    if(!result.roots.completed){if(result.failure==Failure::None)result.failure=Failure::RootStream;return result;}
    for(size_t i=0;i<count_;++i)if(!episode.seen_[i]){result.failure=Failure::MissingSlot;return result;}
    // Walk's original completion/failure counter is required independently;
    // observed edge counts alone never establish a complete heap traversal.
    if(!heap.Calibrated()||!heap.Walk(ordinary_.self,episode)){
      if(result.failure==Failure::None)result.failure=Failure::HeapStream;return result;}
    if(!episode.FinalRootWindow()){result.failure=Failure::Owner;return result;}
    result.completed=result.failure==Failure::None;return result;
  }
 private:
  class Episode final:public source_handle_roots::StoppedReader,
                      public native_typed_heap_walk::RootSink,
                      public native_typed_heap_walk::Sink {
   public:
    Episode(const Census& c,const native_source_stop::Session& s,
            const modern_field_metadata::ReadContext& m,Result& r):c_(c),s_(s),m_(m),r_(r){}
    bool FinalRootWindow()const noexcept override {
      return c_.current_&&c_.current_()==c_.ordinary_.self&&
             s_.IsStopped(c_.ordinary_.self)&&m_.owner&&m_.extent;
    }
    bool ReadStoppedRoot(const void* slot,void** out)const noexcept override {
      if(!out||!FinalRootWindow())return false;
      size_t extent=0;void* raw=nullptr;
      if(!m_.extent(m_.owner,slot,&extent)||extent<sizeof(uint32_t)||
         !c_.pool_->ReadStopped(*this,slot,&raw))return false;
      *out=c_.barrier_(const_cast<void*>(slot));
      return FinalRootWindow();
    }
    bool Root(const native_typed_heap_walk::RootRecord& root)noexcept override {
      if(!FinalRootWindow())return Fail(Failure::Owner);
      size_t owned=c_.count_;for(size_t i=0;i<c_.count_;++i)if(c_.slots_[i]==root.slot){owned=i;break;}
      if(owned<c_.count_){void* current=nullptr;
        if(!ReadStoppedRoot(root.slot,&current)||!current||current!=root.value)return Fail(Failure::RootRead);
        if(root.type!=4||root.representation!=native_typed_heap_walk::RootRepresentation::Compressed)return Fail(Failure::ForeignRoot);
        // This ART ID is observed on the independently owned source slots. It
        // is not kernel GetTid, nor a separately calibrated self->ID decoder.
        if(!root.thread_id||(r_.observed_thread_id&&r_.observed_thread_id!=root.thread_id))return Fail(Failure::ThreadIdentity);
        r_.observed_thread_id=root.thread_id;if(!seen_[owned]){seen_[owned]=true;++r_.owned_slots;}return true;
      }
      bool member=false;if(!Contains(root.value,&member))return false;
      return !member||Fail(Failure::ForeignRoot);
    }
    bool Reference(void* owner,void* value,uint32_t)noexcept override {
      if(!FinalRootWindow())return Fail(Failure::Owner);
      // Root provenance is supplied by the independently completed precise
      // Runtime stream, not by VerifyReference's provenance-free null owner.
      if(!owner)return true;
      bool member=false;if(!Contains(value,&member))return false;if(!member)return true;
      ++r_.incoming_edges;bool internal=false;if(!Contains(owner,&internal))return false;
      if(!internal)return Fail(Failure::ForeignIncoming);++r_.internal_edges;return true;
    }
    std::array<bool,kMaximumMembers> seen_{};
   private:
    bool Contains(void* value,bool* member)noexcept {
      *member=false;for(size_t i=0;i<c_.count_;++i){void* current=nullptr;
        if(!ReadStoppedRoot(c_.slots_[i],&current)||!current)return Fail(Failure::RootRead);
        if(value==current)*member=true;
      }return true;
    }
    bool Fail(Failure f)noexcept{if(r_.failure==Failure::None)r_.failure=f;return false;}
    const Census&c_;const native_source_stop::Session&s_;
    const modern_field_metadata::ReadContext&m_;Result&r_;
  };
  const source_handle_roots::Pool* pool_=nullptr;
  runnable_jni::Context ordinary_{};void*(*current_)()=nullptr;
  void*(*barrier_)(void*)=nullptr;
  std::array<const void*,kMaximumMembers> slots_{};
  size_t count_=0;bool staged_=false;
};
}
