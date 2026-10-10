#pragma once
#include "platform/common/native_receiver_entry_source.h"
#include "platform/common/native_receiver_class_source.h"
#include <optional>
#include <cstdlib>
namespace artdcevm::receiver_guard {
// One process owner; registration grants no Guard proof or activation. Bounds
// are supplied only for the original borrowed ScopedObjectAccess argument.
class OriginalIngress final {
 public:
 class TypedBounds {
  public:
  virtual ~TypedBounds()=default;
  // Enter retains a live, thread-affine typed-root inventory for this original
  // callback. A map/state bit cannot substitute for its actual rooted objects.
  // The inventory must cover source Thread, decoded receiver/member/Class and
  // reached Class/IfTable/Dex identities using independently calibrated types.
  // No JNI/safepoint/OS mapping lookup while a borrowed pointer is in use.
  virtual bool Enter(const abi::common::RunnableJniScope&,void*member,jobject receiver,
      modern_field_metadata::ReadContext*)noexcept=0;
  virtual void Leave()noexcept=0;
 };
 OriginalIngress(EntrySource&e,const ClassSource&c,TypedBounds&b):entry_(e),classes_(c),bounds_(b){}
 // Construct Guard with Leaves() before attaching it; no circular lifetime or
 // partially constructed Guard is borrowed. This owner must outlive dispatch.
 bool Attach(Guard&g)noexcept{if(guard_)return guard_==&g;guard_=&g;return true;}
 bool Publish()noexcept{if(!guard_)return false;OriginalIngress*none=nullptr;return published_.compare_exchange_strong(none,this,std::memory_order_acq_rel)||none==this;}
#ifdef ARTDCEVM_TESTING
 // Single-thread host fixture only, after its consumer has been retired. No
 // production detach exists for a published process-lifetime ingress owner.
 bool RetireForTesting()noexcept{if(active_||consumer.load(std::memory_order_acquire)||!guard_||guard_->Active())return false;auto*expected=this;return published_.compare_exchange_strong(expected,nullptr,std::memory_order_acq_rel);}
#endif
 Access Leaves()noexcept{Access a;a.owner=this;a.current=Current;a.member=Member;a.object_class=ObjectClass;a.reference=Reference;a.compressed=Compressed;a.field_id=FieldId;a.raise=Raise;a.normalize=Normalize;a.assignable=Assignable;a.special=Special;return a;}
 // Called by the sole existing original wrapper. A foreign/unpublished owner
 // leaves the existing dispatch unchanged; an owned unavailable borrow must
 // be raised through the wrapper's actual source ThrowNewException leaf.
 static bool Check(const abi::common::RunnableJniScope&source,void*member,jobject receiver,Path path,bool*unavailable)noexcept{
  if(!unavailable)return false;*unavailable=false;auto*owner=published_.load(std::memory_order_acquire);
  if(!owner||consumer.load(std::memory_order_acquire)!=owner->guard_||!owner->guard_->RequiresIngress(path,source.self,member))return CheckReference(path,source.self,member,receiver);
  Scope scope(*owner,source,member,receiver);if(!scope.Valid()){*unavailable=true;return true;}
  return owner->guard_->CheckReference(path,source.self,member,receiver);
 }
 private:
 class Scope final {
  public:
  Scope(OriginalIngress&o,const abi::common::RunnableJniScope&s,void*m,jobject r)noexcept:owner(o),context{s.self,s.env,s.vm}{
   if(active_||!s.self||!s.env||!s.vm||!o.entry_.Proven())return;
   entered=o.bounds_.Enter(s,m,r,&bounds);if(!entered)return;
   if(!bounds.owner||!bounds.extent)return;
   entry.emplace(o.entry_,context,bounds);if(!entry->Valid())return;
   classes.emplace(o.classes_,bounds);relations.emplace(classes->Access());active_=this;valid=true;
  }
  ~Scope(){if(valid){if(active_!=this)std::abort();active_=nullptr;}relations.reset();classes.reset();entry.reset();if(entered)owner.bounds_.Leave();}
  bool Valid()const noexcept{return valid&&active_==this&&owner.entry_.Leaves().current(&owner.entry_,context.self);}
  OriginalIngress&owner;runnable_jni::Context context;modern_field_metadata::ReadContext bounds{};
  std::optional<EntrySource::InvocationScope>entry;std::optional<ClassSource::ReadWindow>classes;std::optional<Relations>relations;
  bool entered=false,valid=false;
 };
 static Scope*Borrow(void*p)noexcept{auto*s=active_;return s&&&s->owner==p&&s->Valid()?s:nullptr;}
 static bool Current(void*p,void*t)noexcept{auto*s=Borrow(p);return s&&s->context.self==t;}
 static bool Member(void*p,void*m,bool f,uint32_t*flags,void**klass)noexcept{auto*s=Borrow(p);return s&&s->owner.entry_.Leaves().member(&s->owner.entry_,m,f,flags,klass);}
 static bool ObjectClass(void*p,void*r,void**k)noexcept{auto*s=Borrow(p);return s&&s->owner.entry_.Leaves().object_class(&s->owner.entry_,r,k);}
 static bool Reference(void*p,void*t,void*r,void**out)noexcept{auto*s=Borrow(p);return s&&s->owner.entry_.Leaves().reference(&s->owner.entry_,t,r,out);}
 static bool Compressed(void*p,void*t,uint32_t r,void**out)noexcept{auto*s=Borrow(p);return s&&s->owner.entry_.Leaves().compressed(&s->owner.entry_,t,r,out);}
 static bool FieldId(void*p,void*t,void*id,void**out)noexcept{auto*s=Borrow(p);return s&&s->owner.entry_.Leaves().field_id(&s->owner.entry_,t,id,out);}
 static void Raise(void*p,void*t,const char*type,const char*text)noexcept{auto*s=Borrow(p);if(!s)std::abort();s->owner.entry_.Leaves().raise(&s->owner.entry_,t,type,text);}
 static bool Normalize(void*p,void*t,void*d,void*r,void**out)noexcept{auto*s=Borrow(p);return s&&s->context.self==t&&s->relations->Normalize(d,r,out);}
 static bool Assignable(void*p,void*d,void*r,bool*out)noexcept{auto*s=Borrow(p);return s&&s->relations->Assignable(d,r,out);}
 static bool Special(void*p,void*d,void*c,bool*out)noexcept{auto*s=Borrow(p);return s&&s->relations->Special(d,c,out);}
 Guard*guard_=nullptr;EntrySource&entry_;const ClassSource&classes_;TypedBounds&bounds_;
 inline static thread_local Scope*active_=nullptr;
 inline static std::atomic<OriginalIngress*>published_{nullptr};
};
}
