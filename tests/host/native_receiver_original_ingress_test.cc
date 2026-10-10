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
 auto leaves=ingress.Leaves();receiver_guard::Guard guard(leaves);assert(ingress.Attach(guard)&&ingress.Publish()&&receiver_guard::Bind(guard));
 retirement::RetiredMethodRegistry registry;retirement::state.registry=&registry;
 retirement::state.throw_exception=Throw;retirement::state.decode=[](void*,jmethodID id){return static_cast<void*>(id);};
 static unsigned originals=0;retirement::original_id_values=reinterpret_cast<void*>(+[](const abi::common::RunnableJniScope&,jobject,jmethodID,const jvalue*)->uint64_t{++originals;return 99;});
 abi::common::RunnableJniScope actual{context.self,context.env,context.vm};
 receiver_guard::Invocation call{receiver_guard::Path::JniMethod,context.self,member,public_receivers[0]};
 assert(!leaves.current(leaves.owner,context.self));auto previous=raises;
 no_alloc=true;assert(guard.BeginProof({call,declaring,declaring,false}));
 assert(retirement::IdValues<false>(actual,receiver,reinterpret_cast<jmethodID>(member),nullptr)==0);
 assert(inventory.opens==1&&inventory.closes==1&&!inventory.entered&&originals==0&&raises==previous+1&&!leaves.current(leaves.owner,context.self));
 assert(!guard.EndProof(false,false,true)&&!guard.Proven()&&!guard.Active());
 // A bytecode probe's outer ordinary JNI call must reach the actual bytecode
 // source. It does not borrow this unrelated JNI member/path inventory.
 auto other=call;other.path=receiver_guard::Path::BytecodeInvoke;assert(guard.BeginProof({other,declaring,declaring,false}));
 assert(retirement::IdValues<false>(actual,receiver,reinterpret_cast<jmethodID>(member),nullptr)==99&&inventory.opens==1&&inventory.closes==1);
 assert(!guard.EndProof(true,false,true));originals=0;
 // Missing typed root permission rejects at original ingress, preserving the
 // same original body count and releasing only inventories actually entered.
 inventory.allow=false;assert(guard.BeginProof({call,declaring,declaring,false}));
 assert(retirement::IdValues<false>(actual,receiver,reinterpret_cast<jmethodID>(member),nullptr)==0);
 assert(inventory.opens==2&&inventory.closes==1&&raises==previous+2&&originals==0);assert(!guard.EndProof(false,false,true));
 inventory.allow=true;actual.vm=nullptr;assert(guard.BeginProof({call,declaring,declaring,false}));
 assert(retirement::IdValues<false>(actual,receiver,reinterpret_cast<jmethodID>(member),nullptr)==0&&inventory.opens==2&&originals==0);assert(!guard.EndProof(false,false,true));
 // An unrelated consumer is never borrowed from this process ingress owner.
 receiver_guard::consumer.store(nullptr,std::memory_order_release);assert(retirement::IdValues<false>(actual,receiver,reinterpret_cast<jmethodID>(member),nullptr)==99&&originals==1);
 assert(ingress.RetireForTesting());
 no_alloc=false;
 std::puts("Original Runnable JNI wrapper + real calibrated EntrySource + typed rooted inventory: synchronous RAII leave, expired/absent roots and unproved Class relations refuse before original; no Guard readiness/activation or allocation granted PASS");
}
