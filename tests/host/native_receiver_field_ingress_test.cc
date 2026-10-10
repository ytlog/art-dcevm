#include "platform/common/native_receiver_root_bounds.h"
static void RunFieldIngress(artdcevm::receiver_guard::EntrySource&,const artdcevm::modern_field_metadata::FieldMetadata&,const artdcevm::runnable_jni::Context&,void*,jobject,void*);
#define ARTDCEVM_ORIGINAL_INGRESS_TEST
#define RunIngressTest(source,context,member,receiver,declaring) RunFieldIngress(source,metadata,context,member,receiver,declaring)
#define DecodeStrongGlobal FixtureDecodeStrongGlobal
#include "native_receiver_entry_source_test.cc"
#undef DecodeStrongGlobal
#undef RunIngressTest
// The fixture's ordinary Class calibration oracle remains separate from this
// callback's actual source-owned decoder, just as production executor does.
namespace artdcevm::runnable_executor {
bool DecodeStrongGlobal(const runnable_jni::Context&c,jobject root,void**out){
 if(runnable_jni::Bridge::BorrowOriginalScope(c)){*out=Resolve(root);return *out!=nullptr;}
 return FixtureDecodeStrongGlobal(c,root,out);
}
}
#include "platform/common/modern_field_guards.cc"
namespace {
runnable_jni::Bridge actual_bridge;JNIEnv*bridge_env=nullptr;void*source_field=nullptr;void*source_mirror=nullptr;
unsigned field_bodies=0,rooted_routes=0;bool omit_original_scope=false;
void* FieldOriginal(void*mirror){assert(mirror==source_mirror);return source_field;}
void* CarrierOriginal(const abi::common::RunnableJniScope&s,jobject){assert(s.self==actual_thread&&s.env==bridge_env&&s.vm==&vm);return method_members[0];}
jmethodID EnterOriginal(JNIEnv*e,jobject carrier){abi::common::RunnableJniScope actual{actual_thread,e,&vm};return reinterpret_cast<jmethodID>(actual_bridge.OnFromReflectedMethod(actual,carrier));}
jint FieldRead(JNIEnv*,jobject,jfieldID){++field_bodies;return 77;}
void FieldWrite(JNIEnv*,jobject,jfieldID,jint){++field_bodies;}
jint ReflectReadOriginal(JNIEnv*,jobject,jobject){++field_bodies;return 77;}
void ReflectWriteOriginal(JNIEnv*,jobject,jobject,jint){++field_bodies;}
struct Operation {runnable_jni::Operation call;void*data;};
bool ForwardOriginal(const runnable_jni::Context&context,void*p){auto&r=*static_cast<Operation*>(p);assert(runnable_jni::Bridge::BorrowOriginalScope(context));
 auto copied=context;assert(!runnable_jni::Bridge::BorrowOriginalScope(copied));
 return r.call(omit_original_scope?copied:context,r.data);
}
}
namespace artdcevm::runnable_executor {
runnable_jni::Result With(JNIEnv*,jobject,runnable_jni::Operation,void*){std::abort();}
runnable_jni::Result WithOriginalScope(JNIEnv*e,jobject carrier,runnable_jni::Operation op,void*p){++rooted_routes;Operation operation{op,p};return actual_bridge.Invoke(e,carrier,ForwardOriginal,&operation);}
}
static void RunFieldIngress(receiver_guard::EntrySource&entry,const modern_field_metadata::FieldMetadata&metadata,const runnable_jni::Context&context,void*,jobject receiver,void*declaring){
 using namespace retirement;
 source_field=static_cast<Mirror*>(public_fields[0])->field;
 source_mirror=Allocate(field_mirror::kDeclaration.field_size);
 bridge_env=context.env;
 JNINativeInterface_ table=*context.env->functions;table.FromReflectedMethod=EnterOriginal;
 auto*original_table=context.env->functions;context.env->functions=&table;
 assert(runnable_jni::Bridge::Bind(reinterpret_cast<void*>(CarrierOriginal),&actual_bridge));
 RetiredFieldRegistry registry;state.registry=&registry;state.anchor=receiver;state.manager=&manager;state.throw_exception=::Throw;
 original_field=reinterpret_cast<void*>(FieldOriginal);
 JniField<0,jint,false>::get=reinterpret_cast<void*>(FieldRead);JniField<0,jint,false>::set=reinterpret_cast<void*>(FieldWrite);
 ReflectionField<jint>::get=reinterpret_cast<void*>(ReflectReadOriginal);ReflectionField<jint>::set=reinterpret_cast<void*>(ReflectWriteOriginal);
 receiver_guard::OriginalRootBounds bounds(entry,metadata);receiver_guard::ClassSource missing_classes;
 receiver_guard::OriginalIngress ingress(entry,missing_classes,bounds);receiver_guard::Guard guard(ingress.Leaves());
 assert(ingress.Attach(guard)&&ingress.Publish()&&receiver_guard::Bind(guard));
 const auto roots=global_count;
 for(auto path:{receiver_guard::Path::JniRead,receiver_guard::Path::JniWrite,receiver_guard::Path::ReflectRead,receiver_guard::Path::ReflectWrite}){
  auto before=raises;const auto routes=rooted_routes;
  receiver_guard::Invocation call{path,context.self,source_field,public_receivers[0],nullptr,nullptr,true};
  assert(guard.BeginProof({call,declaring,declaring,false}));
  if(path==receiver_guard::Path::JniRead)assert((JniField<0,jint,false>::Get(context.env,receiver,reinterpret_cast<jfieldID>(source_field))==0));
  else if(path==receiver_guard::Path::JniWrite)JniField<0,jint,false>::Set(context.env,receiver,reinterpret_cast<jfieldID>(source_field),77);
  else if(path==receiver_guard::Path::ReflectRead)assert(ReflectionField<jint>::Get(context.env,reinterpret_cast<jobject>(source_mirror),receiver)==0);
  else ReflectionField<jint>::Set(context.env,reinterpret_cast<jobject>(source_mirror),receiver,77);
  assert(!guard.EndProof(false,false,true)&&raises==before+1&&!field_bodies&&global_count==roots&&rooted_routes==routes+1);
 }
 // Exact original Context is required. A matching copy from the same Runnable
 // bridge is still rejected before SeedOriginalFieldRoots or any original body.
 omit_original_scope=true;
 receiver_guard::Invocation call{receiver_guard::Path::JniRead,context.self,source_field,public_receivers[0],nullptr,nullptr,true};
 assert(guard.BeginProof({call,declaring,declaring,false}));auto raised=raises;
 assert((JniField<0,jint,false>::Get(context.env,receiver,reinterpret_cast<jfieldID>(source_field))==0));
 assert(raises==raised+1&&!guard.EndProof(false,false,true)&&!field_bodies&&global_count==roots);omit_original_scope=false;
 // Indexed decode happens before borrowing managed roots. Once an Invocation
 // scope exists the same potentially waiting decoder must never be called.
 index_calibration=true;call.member=source_field;assert(guard.BeginProof({call,declaring,declaring,false}));
 const auto decodes_before=field_decodes;
 assert((JniField<0,jint,false>::Get(context.env,receiver,reinterpret_cast<jfieldID>(1))==0));
 assert(field_decodes==decodes_before+1&&!guard.EndProof(false,false,true)&&!field_bodies&&global_count==roots);index_calibration=false;
 abi::common::RunnableJniScope actual{context.self,context.env,context.vm};modern_field_metadata::ReadContext read;
 auto*copied_field=Allocate(sizeof(field_array::Field));std::memcpy(copied_field,source_field,sizeof(field_array::Field));
 // Same declaring root/flags/index do not make a copied ArtField Class-owned.
 assert(!bounds.EnterField(actual,copied_field,receiver,&read));
 auto*static_field=static_cast<Mirror*>(public_fields[1])->field;
 assert(bounds.EnterField(actual,static_field,nullptr,&read));bounds.Leave();
 no_alloc=true;assert(bounds.EnterField(actual,source_field,receiver,&read));
 {receiver_guard::EntrySource::InvocationScope scope(entry,context,read);assert(scope.Valid());void*field=nullptr;auto prior=field_decodes;
  assert(!entry.DecodeOriginalFieldId(actual,reinterpret_cast<jfieldID>(1),&field)&&field_decodes==prior);
  uint32_t flags=0;void*owner=nullptr;assert(entry.Leaves().member(&entry,source_field,true,&flags,&owner)&&owner==declaring);
 }
 bounds.Leave();no_alloc=false;
 receiver_guard::consumer.store(nullptr,std::memory_order_release);assert(ingress.RetireForTesting());
 context.env->functions=original_table;
 std::puts("Real reflected named Bridge + sole JNI field get/set/reflection get/set + concrete OriginalRootBounds: exact original Context source, indexed-before-borrow, expired scope rejection/root balance; unproved Class relations refuse before body and never activate Guard PASS");
}
