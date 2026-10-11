#include "platform/common/native_receiver_original_ingress.h"
static void RunIngressTest(artdcevm::receiver_guard::EntrySource&,const artdcevm::runnable_jni::Context&,void*,jobject,void*);
#define ARTDCEVM_ORIGINAL_INGRESS_TEST
#include "native_receiver_entry_source_test.cc"
#include "platform/common/modern_method_guards.cc"
static void RunIngressTest(receiver_guard::EntrySource&entry,const runnable_jni::Context&context,void*member,jobject receiver,void*declaring){
 struct Inventory final:receiver_guard::OriginalIngress::TypedBounds {
  const runnable_jni::Context&expected;void*member;jobject receiver;bool entered=false,allow=true;unsigned opens=0,closes=0;
  Inventory(const runnable_jni::Context&c,void*m,jobject r):expected(c),member(m),receiver(r){}
  static bool Read(void*p,const void*address,size_t*out){auto&s=*static_cast<Inventory*>(p);if(!s.entered||!s.allow)return false;
   if(address==s.expected.self){*out=8;return true;}
   for(size_t i=0;i<2;++i){if(address==method_members[i]){*out=32;return true;}if(address==public_receivers[i]){*out=16;return true;}if(address==receiver_classes[i]){*out=160;return true;}}
   return false;
  }
  bool Enter(const abi::common::RunnableJniScope&s,void*m,jobject r,modern_field_metadata::ReadContext*out)noexcept override{
   ++opens;if(entered||!allow||s.self!=expected.self||s.env!=expected.env||s.vm!=expected.vm||m!=member||r!=receiver)return false;entered=true;*out={this,Read};return true;
  }
  void Leave()noexcept override{assert(entered);entered=false;++closes;}
 } inventory(context,member,receiver);
 // EntrySource is genuinely calibrated by the preceding original test. Class
 // source is intentionally absent: entry proof cannot manufacture relations.
 receiver_guard::ClassSource missing_classes;
 receiver_guard::OriginalIngress ingress(entry,missing_classes,inventory);
 auto leaves=ingress.Leaves();
 // Every native read leaf must belong to this exact ingress, including owners
 // that copy its address but substitute just one callback. No scope is opened.
 for(unsigned i=0;i<13;++i){auto foreign=leaves;
  switch(i){case 0:foreign.owner=&inventory;break;case 1:foreign.current=nullptr;break;
   case 2:foreign.member=nullptr;break;case 3:foreign.object_class=nullptr;break;
   case 4:foreign.normalize=nullptr;break;case 5:foreign.assignable=nullptr;break;
   case 6:foreign.special=nullptr;break;case 7:foreign.reference=nullptr;break;
   case 8:foreign.compressed=nullptr;break;case 9:foreign.raise=nullptr;break;
   case 10:foreign.field_id=nullptr;break;case 11:foreign=entry.Leaves();break;
   default:foreign.current=+[](void*,void*)noexcept{return true;};break;}
  receiver_guard::Guard foreign_guard(foreign);assert(!ingress.Attach(foreign_guard)&&!ingress.Publish());
 }
 receiver_guard::Guard guard(leaves),other_guard(leaves);
 assert(ingress.Attach(guard)&&ingress.Attach(guard)&&!ingress.Attach(other_guard)&&
   ingress.Publish()&&receiver_guard::Bind(guard)&&inventory.opens==0);
 retirement::RetiredMethodRegistry registry;retirement::state.registry=&registry;
 retirement::state.throw_exception=Throw;retirement::state.decode=[](void*,jmethodID id){return static_cast<void*>(id);};
 static unsigned originals=0;retirement::original_id_values=reinterpret_cast<void*>(+[](const abi::common::RunnableJniScope&,jobject,jmethodID,const jvalue*)->uint64_t{++originals;return 99;});
 abi::common::RunnableJniScope actual{context.self,context.env,context.vm};
 receiver_guard::Invocation call{receiver_guard::Path::JniMethod,context.self,member,public_receivers[0]};
 assert(!leaves.current(leaves.owner,context.self));auto previous=raises;
 // Exception construction can reenter the same original native wrapper.
 // The outer root inventory must be gone at the named throw leaf, and the
 // nested invocation must obtain/release a new window without JNI/allocation.
 struct Reentry {Inventory&inventory;receiver_guard::Access leaves;const runnable_jni::Context&context;
  const abi::common::RunnableJniScope&actual;void*member;jobject receiver;bool seen=false;}reentry{inventory,leaves,context,actual,member,receiver};
 raise_observer_data=&reentry;raise_observer=+[](void*raw){auto&r=*static_cast<Reentry*>(raw);
  raise_observer=nullptr; // The nested rejection uses the same leaf once more.
  assert(!r.inventory.entered&&r.inventory.closes==1&&!r.leaves.current(r.leaves.owner,r.context.self));
  const auto opens=r.inventory.opens,closes=r.inventory.closes;
  assert(retirement::IdValues<false>(r.actual,r.receiver,reinterpret_cast<jmethodID>(r.member),nullptr)==0);
  assert(r.inventory.opens==opens+1&&r.inventory.closes==closes+1&&!r.inventory.entered);
  assert(!r.leaves.current(r.leaves.owner,r.context.self));r.seen=true;
 };
 no_alloc=true;assert(guard.BeginProof({call,declaring,declaring,false}));
 assert(retirement::IdValues<false>(actual,receiver,reinterpret_cast<jmethodID>(member),nullptr)==0);
 assert(reentry.seen&&inventory.opens==2&&inventory.closes==2&&!inventory.entered&&originals==0&&raises==previous+2&&!leaves.current(leaves.owner,context.self));
 raise_observer_data=nullptr;
 assert(!guard.EndProof(false,false,true)&&!guard.Proven()&&!guard.Active());
 // A bytecode probe's outer ordinary JNI call must reach the actual bytecode
 // source. It does not borrow this unrelated JNI member/path inventory.
 auto other=call;other.path=receiver_guard::Path::BytecodeInvoke;assert(guard.BeginProof({other,declaring,declaring,false}));
 assert(retirement::IdValues<false>(actual,receiver,reinterpret_cast<jmethodID>(member),nullptr)==99&&inventory.opens==2&&inventory.closes==2);
 assert(!guard.EndProof(true,false,true));originals=0;
 // Missing typed root permission rejects at original ingress, preserving the
 // same original body count and releasing only inventories actually entered.
 inventory.allow=false;assert(guard.BeginProof({call,declaring,declaring,false}));
 assert(retirement::IdValues<false>(actual,receiver,reinterpret_cast<jmethodID>(member),nullptr)==0);
 assert(inventory.opens==3&&inventory.closes==2&&raises==previous+3&&originals==0);assert(!guard.EndProof(false,false,true));
 inventory.allow=true;actual.vm=nullptr;assert(guard.BeginProof({call,declaring,declaring,false}));
 assert(retirement::IdValues<false>(actual,receiver,reinterpret_cast<jmethodID>(member),nullptr)==0&&inventory.opens==3&&originals==0);assert(!guard.EndProof(false,false,true));
 // An unrelated consumer is never borrowed from this process ingress owner.
 receiver_guard::consumer.store(nullptr,std::memory_order_release);assert(retirement::IdValues<false>(actual,receiver,reinterpret_cast<jmethodID>(member),nullptr)==99&&originals==1);
 assert(ingress.RetireForTesting());
 no_alloc=false;
 std::puts("Original Runnable JNI wrapper + real calibrated EntrySource + typed rooted inventory: synchronous RAII leave, expired/absent roots and unproved Class relations refuse before original; no Guard readiness/activation or allocation granted PASS");
}
