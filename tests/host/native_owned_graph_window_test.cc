#include <cstdlib>
#include <new>
static bool forbid_final_allocation=false;
void* operator new(std::size_t bytes){if(forbid_final_allocation)throw std::bad_alloc();if(auto p=std::malloc(bytes))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void* operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete[](void* p)noexcept{::operator delete(p);}
#include "platform/common/rooted_schema_plan.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define ARTDCEVM_BATCH_BOUNDARY_NAME original_batch_boundary
#include "native_owned_batch_test.cc"
#undef ARTDCEVM_BATCH_BOUNDARY_NAME
#pragma clang diagnostic pop
namespace graph_window {
using namespace artdcevm::native_owned_removal;
std::map<jobject,std::vector<jobject>> managed;
std::vector<jclass> staged;
int driver=0,failures=0,graph_checks=0;bool original_ok=true,collection_ok=true,field_ok=true,late_child=false,schema_mode=false,collected_provider_ok=true;
jvmtiError recorded=JVMTI_ERROR_NONE;std::string injected_rejection;
source_commit::Plan finalized_plan;std::unique_ptr<source_commit::Services> finalized_service;
bool Original(void* d,transaction_root::Holder* h){assert(d==&driver&&h==&batch_boundary::holder&&!stopped);if(!original_ok)recorded=JVMTI_ERROR_FAILS_VERIFICATION;return original_ok;}
void Failure(void* d,jvmtiError code,const void* klass,const void* message){assert(d==&driver&&klass&&message&&!stopped);auto& text=*static_cast<const android13::BorrowedPlatformString*>(message);assert(text.length&&text.data);++failures;recorded=code;}
void* RootBarrier(void* handle){assert(!stopped);return reinterpret_cast<void*>(uintptr_t(Read32(handle)));}
jobject Add(void*,void* self,abi::arm64::android11::ReleaseObjectPointer value){assert(!stopped&&self==current);return reinterpret_cast<jobject>(value.reference);}
jsize Length(JNIEnv*,jarray array){Jni();return managed.at(array).size();}
jobject Element(JNIEnv*,jobjectArray array,jsize index){Jni();return managed.at(array).at(index);}
jint Vm(JNIEnv*,JavaVM** out){Jni();*out=reinterpret_cast<JavaVM*>(&runtime);return JNI_OK;}
jclass ForbiddenFind(JNIEnv*,const char*){assert(false&&"Paused graph window cannot load classes");return nullptr;}
struct ReadonlyServices final:batch_boundary::CommitServices {
 explicit ReadonlyServices(const source_commit::Plan& p):CommitServices(p){}
 bool Validate(const source_commit::Plan& p)const override {assert(!stopped&&&p==plan&&p.old_classes.size()==2&&p.class_root_origins.size()==2);return collected_provider_ok;}
};
std::unique_ptr<source_commit::Services> PrepareReadonly(const source_commit::Plan& p,void*,std::string*){assert(!stopped);++batch_boundary::prepared;return std::make_unique<ReadonlyServices>(p);}
bool Check(JNIEnv* env,jvmtiEnv*,const std::vector<jclass>& actual,const RootedGraph& graph,GraphWindow window,void*,std::string* error){
 if(!schema_mode)assert(graph.partitions.empty());else {assert(graph.mirror&&graph.partitions.size()==2);for(size_t n=0;n<graph.partitions.size();++n){const auto& p=graph.partitions[n];assert(p.holder_index==n&&p.class_count==1&&p.old_classes&&p.new_classes);assert(p.new_classes==batch_boundary::partitions[n].plan.new_classes_root);assert(p.instance_count==(window==GraphWindow::Collected?1u:0u));}}assert(window==(graph_checks==0?GraphWindow::Verified:GraphWindow::Collected));
 ++graph_checks;if(!injected_rejection.empty()){*error=injected_rejection;return false;}if(actual.size()!=staged.size()||!field_ok){*error="Unstaged child or changed field proof";return false;}
 for(auto klass:actual){size_t matches=0;for(auto candidate:staged)matches+=env->IsSameObject(klass,candidate);if(matches!=1){*error="Class namespace mismatch";return false;}}
 if(schema_mode&&window==GraphWindow::Collected){source_commit::Plan plan;plan.mirror=graph.mirror;plan.new_classes_root=graph.partitions[0].new_classes;
  for(size_t n=0;n<graph.partitions.size();++n){const auto& part=batch_boundary::partitions[n].plan;plan.old_classes.push_back(part.old_classes[0]);plan.new_classes.push_back(part.new_classes[0]);plan.class_root_origins.push_back({graph.partitions[n].new_classes,0});}
  if(!rooted_schema_plan::ValidateCollected(plan,PrepareReadonly,nullptr,error))return false;
  for(size_t n=0;n<graph.partitions.size();++n){const auto& part=batch_boundary::partitions[n].plan;plan.old_instances.push_back(part.old_instances[0]);plan.new_instances.push_back(part.new_instances[0]);}
  finalized_plan=std::move(plan);finalized_service=std::make_unique<batch_boundary::CommitServices>(finalized_plan);
 }
 return true;
}
std::unique_ptr<source_commit::Services> Finalize(const void* holder,const source_commit::Plan** plan,void*) noexcept{
 assert(stopped&&holder==&batch_boundary::holder&&finalized_service);*plan=&finalized_plan;return std::move(finalized_service);
}
bool Collect(void* d,transaction_root::Holder* h){assert(d==&driver&&h==&batch_boundary::holder&&!stopped);if(late_child)field_ok=false;return true;}
int leaf_mode=0,verification_calls=0,collection_calls=0;
bool OriginalVerification(void* owner,const transaction_root::Iterator* it){assert(!stopped&&it&&it->holder==&batch_boundary::holder&&owner==&batch_boundary::owners[size_t(it->index)]);++verification_calls;if(!original_ok)recorded=JVMTI_ERROR_FAILS_VERIFICATION;return original_ok;}
bool OriginalCollection(void* owner,transaction_root::Iterator* it){assert(!stopped&&it&&it->holder==&batch_boundary::holder&&owner==&batch_boundary::owners[size_t(it->index)]);++collection_calls;if(late_child)field_ok=false;if(!collection_ok)recorded=JVMTI_ERROR_OUT_OF_MEMORY;return collection_ok;}
bool Leaves(){
 for(size_t n=0;n<batch_boundary::owners.size();++n){
  if((leaf_mode==1&&n==0)||leaf_mode==2)continue;
  transaction_root::Iterator it{static_cast<int32_t>(leaf_mode==3?0:n),&batch_boundary::holder};
  if(!VerificationGraph(&batch_boundary::owners[n],&it))return false;
 }
 transaction_root::Holder other=batch_boundary::holder;
 for(size_t n=0;n<batch_boundary::owners.size();++n){transaction_root::Iterator it{static_cast<int32_t>(n),&batch_boundary::holder};
  if(leaf_mode==4)it.index=1;
  if(leaf_mode==5)it.holder=&other;
  if(!CollectionGraph(&batch_boundary::owners[n],&it))return false;
 }return true;
}
bool WrappedVerification(void* d,transaction_root::Holder* h){assert(d==&driver&&h==&batch_boundary::holder);for(size_t n=0;n<batch_boundary::owners.size();++n){transaction_root::Iterator it{static_cast<int32_t>(n),h};if(!VerificationGraph(&batch_boundary::owners[n],&it))return false;}return true;}
bool WrappedCollection(void* d,transaction_root::Holder* h){assert(d==&driver&&h==&batch_boundary::holder);for(size_t n=0;n<batch_boundary::owners.size();++n){transaction_root::Iterator it{static_cast<int32_t>(n),h};if(!CollectionGraph(&batch_boundary::owners[n],&it))return false;}return true;}
bool RefuseThenForbid(){const bool result=Leaves();assert(!result);forbid_final_allocation=true;return result;}
bool Before(){return VerifiedGraph(&driver,&batch_boundary::holder)&&CollectGraph(&driver,&batch_boundary::holder);}
jvmtiError Result(){return recorded;}
void Build(const std::vector<Definition>& definitions){
 batch_boundary::Build(definitions);managed.clear();staged.clear();failures=graph_checks=0;recorded=JVMTI_ERROR_NONE;field_ok=original_ok=collection_ok=true;late_child=false;verification_calls=collection_calls=0;
 auto& holder=batch_boundary::holder;auto* handle=static_cast<uint32_t*>(heap.New(4));auto* array=heap.New(16);*handle=uint32_t(uintptr_t(array));holder.array_handle=handle;
 static transaction_root::Owners vector{};vector={reinterpret_cast<const unsigned char*>(batch_boundary::owners.data()),reinterpret_cast<const unsigned char*>(batch_boundary::owners.data()+definitions.size()),reinterpret_cast<const unsigned char*>(batch_boundary::owners.data()+definitions.size())};holder.redefinitions=&vector;
 holder.initialized=holder.actually_structural=holder.initial_structural;
 auto& slots=managed[reinterpret_cast<jobject>(array)];slots.resize(definitions.size()*abi::arm64::android11::kRootSlots);
 for(size_t n=0;n<definitions.size();++n){batch_boundary::owners[n].driver=&driver;
  auto old=static_cast<jclass>(batch_boundary::partitions[n].plan.old_classes[0]);staged.push_back(old);
  auto objects=reinterpret_cast<jobject>(heap.New(16));managed[objects]={old};slots[n*13+11]=objects;
  if(schema_mode){const auto& plan=batch_boundary::partitions[n].plan;
   slots[n*13+4]=reinterpret_cast<jobject>(batch_boundary::partitions[0].plan.mirror);
   auto next=reinterpret_cast<jobject>(plan.new_classes_root);managed[next]={reinterpret_cast<jobject>(plan.new_classes[0])};slots[n*13+12]=next;
   for(size_t slot=9;slot<=10;++slot){auto instances=reinterpret_cast<jobject>(heap.New(16));managed[instances]={reinterpret_cast<jobject>((slot==9?plan.old_instances:plan.new_instances)[0])};slots[n*13+slot]=instances;}
  }}
 for(const auto& pair:managed){if(pair.first==reinterpret_cast<jobject>(array))continue;Put32(pair.first,8,uint32_t(pair.second.size()));if(pair.second.size()==1)Put32(pair.first,12,uint32_t(uintptr_t(pair.second[0])));}
}
}
namespace lifecycle_test {
using namespace graph_window;using namespace artdcevm::native_owned_removal;
jvmtiEnv* ti=nullptr;jint count=0;const jvmtiClassDefinition* definitions=nullptr;
int opened=0,closed=0,refunds=0,starts=0,ends=0,commits=0,mode=0;bool view=false;
bool Begin(void* d,void*,std::string*){assert(d==&driver&&!stopped);++starts;if(mode==6)throw std::bad_alloc();return mode!=7;}
void End(void* d,bool okay,void*) noexcept {assert(d==&driver&&!view);++ends;assert(okay?commits==1:refunds==1);}
bool Before(void* owner,const transaction_root::Iterator* it,void*,std::string* error){assert(!view&&owner==&batch_boundary::owners[it->index]);view=true;++opened;if(mode==1){*error="Exact verification view refused";return false;}if(mode==2)throw std::bad_alloc();if(mode==4)throw 42;return true;}
void After(void*,const transaction_root::Iterator*,bool result,void*) noexcept {assert(view);view=false;++closed;if(mode==1||mode==2||mode==4)assert(!result);}
void RefundOwner(void*) noexcept {assert((!view||mode==5)&&!stopped);++refunds;assert(refunds==1);}
void CommitOwner(const source_commit::Plan& p,void*) noexcept {assert(!view&&stopped&&&p==&finalized_plan&&batch_boundary::begins==0);++commits;}
bool Verify(void* owner,const transaction_root::Iterator* it){assert(view);if(mode==5){ObserveSourceFailure(&driver,JVMTI_ERROR_FAILS_VERIFICATION);assert(view&&refunds==1);recorded=JVMTI_ERROR_FAILS_VERIFICATION;return false;}return OriginalVerification(owner,it);}
jvmtiError OriginalRun(void* d){assert(d==&driver);if(mode==3){ObserveSourceFailure(reinterpret_cast<void*>(1),JVMTI_ERROR_INTERNAL);assert(refunds==0);ObserveSourceFailure(d,JVMTI_ERROR_INVALID_CLASS);return JVMTI_ERROR_INVALID_CLASS;}return batch_boundary::Entry(ti,count,definitions);}
jvmtiError Entry(jvmtiEnv* t,jint n,const jvmtiClassDefinition* defs){ti=t;count=n;definitions=defs;return SourceRun(&driver);}
jvmtiError JNICALL Extensions(jvmtiEnv* t,jint* n,jvmtiExtensionFunctionInfo** info){auto code=batch_boundary::Extensions(t,n,info);(*info)->func=reinterpret_cast<jvmtiExtensionFunction>(Entry);return code;}
void Reset(int value){opened=closed=refunds=starts=ends=commits=0;view=false;mode=value;}
}
int main(int argc,char** argv){
 assert(original_batch_boundary(argc,argv)==0);
 using namespace batch_boundary;using namespace graph_window;
 Hooks hooks;std::string error;modern_field_metadata::FieldMetadata metadata;
 std::array<modern_field_metadata::FieldOracle,4> oracles{};for(size_t n=0;n<4;n++)oracles[n]={reinterpret_cast<jobject>(&mirrors[n]),names[n],types[n],n==0?8u:1u};
 assert(metadata.Bind(hooks,&error));Put32(current,0,0);assert(metadata.Calibrate({current,&env,&runtime},reinterpret_cast<jobject>(old_class),"LFixture;",oracles.data(),4,&error));
 std::ifstream file(argv[1],std::ios::binary);std::vector<unsigned char> dex((std::istreambuf_iterator<char>(file)),{});evolution::ClassSchema schema;assert(evolution::ReadDexDeclarations(dex,&schema,&error));
 std::vector<Definition> definitions;for(size_t n=0;n<2;n++){auto klass=reinterpret_cast<jclass>(heap.New(16));definitions.push_back({klass,dex});live[klass]=schema;live[klass].methods.push_back({"removed","()I",1,0});loaders[klass]=reinterpret_cast<jobject>(heap.New(16));}
 jvmtiInterface_1 functions{};functions.GetClassLoader=Loader;functions.GetExtensionFunctions=Extensions;functions.Deallocate=Free;jvmtiEnv ti{&functions};
 table.GetArrayLength=Length;table.GetObjectArrayElement=Element;table.GetJavaVM=Vm;table.FindClass=ForbiddenFind;
 verification_leaf_original=OriginalVerification;collection_leaf_original=OriginalCollection;verified_original=Original;collect_original=Collect;failure=graph_window::Failure;root_barrier=RootBarrier;add_global=Add;current_thread=Current;window_ready=true;before_commit=Before;before_commit_result=Result;
 Provider provider{Ready,Prepare,&metadata,nullptr,true,Preflight,graph_window::Check};
 Reset();graph_window::Build(definitions);assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NONE&&graph_checks==2&&begins==1&&failures==0);
 for(int mode=0;mode<5;++mode){Reset();graph_window::Build(definitions);
  auto root=reinterpret_cast<jobject>(uintptr_t(*holder.array_handle));auto first=managed[root][11];
  if(mode==0)managed[first].push_back(reinterpret_cast<jobject>(heap.New(16))); // new child after public stage
  if(mode==1)managed[first].push_back(managed[first][0]); // duplicate native graph root
  if(mode==2)managed[root][11]=nullptr;
  if(mode==3)field_ok=false;
  if(mode==4)staged[0]=reinterpret_cast<jclass>(heap.New(16)); // equal counts / different Class identity
  assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NOT_AVAILABLE&&recorded==JVMTI_ERROR_NOT_AVAILABLE&&failures==1&&begins==0&&prepared==0);
 }
 Reset();graph_window::Build(definitions);proof_ok=false;assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NOT_AVAILABLE&&entries==0&&graph_checks==0);
 Reset();graph_window::Build(definitions);late_child=true;assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NOT_AVAILABLE&&recorded==JVMTI_ERROR_NOT_AVAILABLE&&graph_checks==2&&begins==0&&prepared==0);
 Reset();graph_window::Build(definitions);original_ok=false;assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_FAILS_VERIFICATION&&recorded==JVMTI_ERROR_FAILS_VERIFICATION&&failures==0&&begins==0&&graph_checks==0);
 // Identical declarations still enter complete schema mode (body replacement),
 // capturing actual paired Holder arrays at both refundable source windows.
 schema_mode=true;provider.schema_evolution=true;provider.finalize=Finalize;
 operations.reserve(100);enter_final=[]{forbid_final_allocation=true;};leave_final=[]{forbid_final_allocation=false;};for(auto& definition:definitions)live[definition.klass]=schema;
 Reset();graph_window::Build(definitions);assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NONE&&graph_checks==2&&begins==1&&prepared==1);
 collected_provider_ok=false;Reset();graph_window::Build(definitions);assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NOT_AVAILABLE&&graph_checks==2&&prepared==1&&begins==0&&replacements==0&&original_commits==0);collected_provider_ok=true;
 for(int mode=0;mode<3;++mode){Reset();graph_window::Build(definitions);auto root=reinterpret_cast<jobject>(uintptr_t(*holder.array_handle));
  if(mode==0)managed[managed[root][12]].clear();
  if(mode==1)managed[managed[root][10]].clear();
  if(mode==2)managed[root][4]=nullptr;
  assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NOT_AVAILABLE&&begins==0&&prepared==0&&failures==1);
 }
 // The device can inline both thin driver loops: only real named Class
 // verification/collection leaves run, with every original still authoritative.
 before_commit=Leaves;leaf_mode=0;
 Reset();graph_window::Build(definitions);assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NONE&&graph_checks==2&&verification_calls==2&&collection_calls==2&&begins==1);
 for(int mode=1;mode<=5;++mode){leaf_mode=mode;Reset();graph_window::Build(definitions);
  assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NOT_AVAILABLE&&failures==1&&begins==0&&replacements==0&&original_commits==0&&collection_calls==0);assert(error.find("Owned source error 98")!=std::string::npos&&error.find("graphSealed=0")!=std::string::npos);
 }
 leaf_mode=0;Reset();graph_window::Build(definitions);original_ok=false;
 assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_FAILS_VERIFICATION&&verification_calls==1&&collection_calls==0&&begins==0&&failures==0);assert(error.find("Original CheckVerification returned false")!=std::string::npos);
 Reset();graph_window::Build(definitions);collection_ok=false;
 assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_OUT_OF_MEMORY&&verification_calls==2&&collection_calls==1&&begins==0&&failures==0&&graph_checks==1);assert(error.find("Original CollectAndCreateNewInstances returned false")!=std::string::npos);
 Reset();graph_window::Build(definitions);collected_provider_ok=false;
 assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NOT_AVAILABLE&&graph_checks==2&&collection_calls==2&&begins==0&&replacements==0);assert(error.find("Complete native schema preparation was rejected before publication")!=std::string::npos&&error.find("Collected graph preparation")!=std::string::npos&&error.find("graphSealed=0")!=std::string::npos);collected_provider_ok=true;
 std::string early;assert(owned_failure_diagnostic::Unavailable(&early,"candidate graph/member staging")==JVMTI_ERROR_NOT_AVAILABLE&&early.find("before original source entry")!=std::string::npos);
 early="Precise original preparation error";assert(owned_failure_diagnostic::Unavailable(&early,"abstract-instance calibration")==JVMTI_ERROR_NOT_AVAILABLE&&early=="Precise original preparation error");
 std::string early_oom;forbid_final_allocation=true;assert(owned_failure_diagnostic::Unavailable(&early_oom,"interpreter-cache preparation")==JVMTI_ERROR_NOT_AVAILABLE);forbid_final_allocation=false;assert(early_oom.empty());
 // The owned reason is copied while the temporary provider text still exists.
 injected_rejection="Precise disposable provider rejection "+std::string(5000,'Z');
 Reset();graph_window::Build(definitions);
 assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NOT_AVAILABLE&&error.find("Precise disposable provider rejection")!=std::string::npos&&error.size()<1024&&begins==0);
 injected_rejection.clear();
 // No allocation/ JNI is needed to remember the first bounded reason. Copying
 // into a pre-reserved caller buffer also remains allocation-free.
 Request diagnostic{&hooks,&env,&provider,{},false,nullptr,{}};std::string reason(5000,'X'),copied;copied.reserve(1024);
 active=&diagnostic;const int calls=jni_calls;forbid_final_allocation=true;
 Phase("fixed bounded capture");RememberFailure(reason.c_str());RememberFailure("must not replace first reason");CopyFailure(diagnostic,JVMTI_ERROR_NOT_AVAILABLE,&copied);
 forbid_final_allocation=false;active=nullptr;
 assert(std::strlen(diagnostic.rejection)==767&&copied.find(std::string(767,'X'))!=std::string::npos&&jni_calls==calls);
 Request unicode_diagnostic{&hooks,&env,&provider,{},false,nullptr,{}};std::string unicode;for(size_t n=0;n<600;++n)unicode+="\xc3\xa9";
 active=&unicode_diagnostic;forbid_final_allocation=true;RememberFailure(unicode.c_str());forbid_final_allocation=false;active=nullptr;
 assert(std::strlen(unicode_diagnostic.rejection)==766&&unicode_diagnostic.rejection[766]==0);

 // Failure reporting OOM cannot escape or alter the original source code.
 before_commit=RefuseThenForbid;leaf_mode=2;std::string tiny;
 Reset();graph_window::Build(definitions);const auto unchanged_code=RunBatch(hooks,&env,&ti,definitions,provider,&tiny);
 forbid_final_allocation=false;assert(unchanged_code==JVMTI_ERROR_NOT_AVAILABLE&&tiny.empty()&&failures==1&&begins==0);
 before_commit=Leaves;leaf_mode=0;
 Reset();graph_window::Build(definitions);assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NONE&&error.empty());
 // Non-inlined wrappers containing both leaf proxies must seal exactly once.
 before_commit=Before;verified_original=WrappedVerification;collect_original=WrappedCollection;
 Reset();graph_window::Build(definitions);assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NONE&&graph_checks==2&&verification_calls==2&&collection_calls==2&&begins==1);
 verified_original=Original;collect_original=Collect;
 // A real named Run invocation encloses all leaves. No symbol-presence
 // shortcut: lifecycle refuses when the actual Run observer was not entered.
 functions.GetExtensionFunctions=lifecycle_test::Extensions;run_original=lifecycle_test::OriginalRun;run_ready=true;
 provider.begin_run=lifecycle_test::Begin;provider.end_run=lifecycle_test::End;
 provider.before_verification=lifecycle_test::Before;provider.after_verification=lifecycle_test::After;
 provider.refund=lifecycle_test::RefundOwner;provider.commit=lifecycle_test::CommitOwner;
 verification_leaf_original=lifecycle_test::Verify;before_commit=Leaves;
 for(int mode=0;mode<8;++mode){lifecycle_test::Reset(mode);batch_boundary::Reset();graph_window::Build(definitions);
  const auto code=RunBatch(hooks,&env,&ti,definitions,provider,&error);
  assert(lifecycle_test::starts==1&&lifecycle_test::ends==1&&!lifecycle_test::view);
  if(mode==0)assert(code==JVMTI_ERROR_NONE&&lifecycle_test::opened==2&&lifecycle_test::closed==2&&lifecycle_test::commits==1&&lifecycle_test::refunds==0);
  if(mode==1)assert(code==JVMTI_ERROR_NOT_AVAILABLE&&lifecycle_test::opened==1&&lifecycle_test::closed==1&&lifecycle_test::refunds==1&&begins==0);
  if(mode==2)assert(code==JVMTI_ERROR_OUT_OF_MEMORY&&lifecycle_test::opened==1&&lifecycle_test::closed==1&&lifecycle_test::refunds==1&&begins==0);
  if(mode==6)assert(code==JVMTI_ERROR_OUT_OF_MEMORY&&lifecycle_test::opened==0&&lifecycle_test::refunds==1&&entries==0);
  if(mode==7)assert(code==JVMTI_ERROR_NOT_AVAILABLE&&lifecycle_test::opened==0&&lifecycle_test::refunds==1&&entries==0);
  if(mode==5)assert(code==JVMTI_ERROR_FAILS_VERIFICATION&&lifecycle_test::closed==1&&lifecycle_test::refunds==1&&begins==0);
  if(mode==4)assert(code==JVMTI_ERROR_NOT_AVAILABLE&&lifecycle_test::closed==1&&lifecycle_test::refunds==1&&begins==0);
  if(mode==3)assert(code==JVMTI_ERROR_INVALID_CLASS&&lifecycle_test::opened==0&&lifecycle_test::refunds==1&&begins==0);
 }
 lifecycle_test::Reset(0);batch_boundary::Reset();graph_window::Build(definitions);original_ok=false;
 assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_FAILS_VERIFICATION&&lifecycle_test::closed==1&&lifecycle_test::refunds==1&&begins==0);
 lifecycle_test::Reset(0);batch_boundary::Reset();graph_window::Build(definitions);collection_ok=false;
 assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_OUT_OF_MEMORY&&lifecycle_test::closed==2&&lifecycle_test::refunds==1&&begins==0);
 // Partial factory contracts fail before source entry; callback-free providers
 // retain their established source behavior.
 lifecycle_test::Reset(0);batch_boundary::Reset();graph_window::Build(definitions);provider.end_run=nullptr;
 assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NOT_AVAILABLE&&lifecycle_test::starts==0&&entries==0);
 provider.end_run=lifecycle_test::End;functions.GetExtensionFunctions=batch_boundary::Extensions;
 lifecycle_test::Reset(0);batch_boundary::Reset();graph_window::Build(definitions);
 assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NOT_AVAILABLE&&lifecycle_test::starts==0&&lifecycle_test::opened==0&&begins==0&&collection_calls==0);
 provider.end_run=nullptr;
 provider.begin_run=nullptr;provider.before_verification=nullptr;provider.after_verification=nullptr;provider.refund=nullptr;provider.commit=nullptr;
 functions.GetExtensionFunctions=batch_boundary::Extensions;verification_leaf_original=OriginalVerification;
 puts("Actual Run lifecycle: paired view closes before single live refund; original leaf and RecordFailure codes retained; foreign driver ignored; bad_alloc refundable; validated commit precedes service BeginCommit under allocator failure guard PASS");
 // Production's actual rooted-plan reader re-decodes the same strong global
 // arrays after simulated moving GC and preserves the actual origin partition.
 Reset();graph_window::Build(definitions);auto holder_root=reinterpret_cast<jobject>(uintptr_t(*holder.array_handle));RootedGraph graph;
 graph.mirror=static_cast<jclass>(managed[holder_root][4]);
 for(size_t n=0;n<2;++n)graph.partitions.push_back({static_cast<jobjectArray>(managed[holder_root][n*13+11]),static_cast<jobjectArray>(managed[holder_root][n*13+12]),static_cast<jobjectArray>(managed[holder_root][n*13+9]),static_cast<jobjectArray>(managed[holder_root][n*13+10]),n,1,1});
 std::map<jobject,void*> decoded;for(const auto& pair:managed)decoded[pair.first]=pair.first;decoded[graph.mirror]=graph.mirror;
 rooted_schema_plan::Reader reader{&decoded,
  [](void* state,jobject root,void** out){auto& map=*static_cast<std::map<jobject,void*>*>(state);auto it=map.find(root);if(it==map.end())return false;*out=it->second;return true;},
  [](void*,const void* value,size_t bytes){size_t extent=0;return method_identity::ReadableMappingExtent(value,&extent)&&bytes<=extent;},
  [](void*,void* array,size_t offset)->void*{return reinterpret_cast<void*>(uintptr_t(Read32(array,offset)));}};
 source_commit::Plan old_plan,new_plan;assert(rooted_schema_plan::Read(graph,true,reader,&old_plan,&error));
 auto moved_array=heap.New(16),moved_class=heap.New(16);Put32(moved_array,8,1);Put32(moved_array,12,uint32_t(uintptr_t(moved_class)));decoded[graph.partitions[1].new_classes]=moved_array;
 assert(rooted_schema_plan::Read(graph,true,reader,&new_plan,&error)&&new_plan.new_classes[1]==moved_class&&new_plan.class_root_origins[1].array==moved_array&&new_plan.class_root_origins[1].index==0&&old_plan.new_classes[1]!=new_plan.new_classes[1]);
 forbid_final_allocation=true;assert(rooted_schema_plan::Matches(graph,reader,new_plan));new_plan.class_root_origins[1].index=1;assert(!rooted_schema_plan::Matches(graph,reader,new_plan));new_plan.class_root_origins[1].index=0;forbid_final_allocation=false;
 auto unchanged=new_plan.new_classes;Put32(moved_array,8,2);assert(!rooted_schema_plan::Read(graph,true,reader,&new_plan,&error)&&new_plan.new_classes==unchanged);
 assert(!rooted_schema_plan::Read(graph,false,reader,&new_plan,&error));
 before_commit=nullptr;before_commit_result=nullptr;enter_final=leave_final=nullptr;
 puts("Owned source fixed diagnostics: exact disposable provider reason, source phase/counts, original errors, bounded truncation, first-error isolation, noalloc/noJNI capture, diagnostic OOM preserves result and next success clears PASS");
 puts("Actual leaf boundaries: inlined/non-inlined driver coverage, every original bool, exact Holder/index/owner sequence, missing/skipped/duplicate verification refusal before collection, Collected rejection before Update PASS");
 puts("Actual graph window TU: schema paired Class/instance roots, moving-pointer reseal, exact origins, readonly Collected provider refusal before Update, preallocated final Update with allocator forbidden, raced/duplicate/missing child refusal and original failure result PASS");
}
