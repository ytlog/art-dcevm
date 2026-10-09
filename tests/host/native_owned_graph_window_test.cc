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
int driver=0,failures=0,graph_checks=0;bool original_ok=true,field_ok=true,late_child=false,schema_mode=false,collected_provider_ok=true;
jvmtiError recorded=JVMTI_ERROR_NONE;
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
 ++graph_checks;if(actual.size()!=staged.size()||!field_ok){*error="Unstaged child or changed field proof";return false;}
 for(auto klass:actual){size_t matches=0;for(auto candidate:staged)matches+=env->IsSameObject(klass,candidate);if(matches!=1){*error="Class namespace mismatch";return false;}}
 if(schema_mode&&window==GraphWindow::Collected){source_commit::Plan plan;plan.mirror=graph.mirror;plan.new_classes_root=graph.partitions[0].new_classes;
  for(size_t n=0;n<graph.partitions.size();++n){const auto& part=batch_boundary::partitions[n].plan;plan.old_classes.push_back(part.old_classes[0]);plan.new_classes.push_back(part.new_classes[0]);plan.class_root_origins.push_back({graph.partitions[n].new_classes,0});}
  if(!rooted_schema_plan::ValidateCollected(plan,PrepareReadonly,nullptr,error))return false;
 }
 return true;
}
bool Collect(void* d,transaction_root::Holder* h){assert(d==&driver&&h==&batch_boundary::holder&&!stopped);if(late_child)field_ok=false;return true;}
bool Before(){return VerifiedGraph(&driver,&batch_boundary::holder)&&CollectGraph(&driver,&batch_boundary::holder);}
jvmtiError Result(){return recorded;}
void Build(const std::vector<Definition>& definitions){
 batch_boundary::Build(definitions);managed.clear();staged.clear();failures=graph_checks=0;recorded=JVMTI_ERROR_NONE;field_ok=original_ok=true;late_child=false;
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
 verified_original=Original;collect_original=Collect;failure=graph_window::Failure;root_barrier=RootBarrier;add_global=Add;current_thread=Current;window_ready=true;before_commit=Before;before_commit_result=Result;
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
 schema_mode=true;provider.schema_evolution=true;for(auto& definition:definitions)live[definition.klass]=schema;
 Reset();graph_window::Build(definitions);assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NONE&&graph_checks==2&&begins==1&&prepared==2);
 collected_provider_ok=false;Reset();graph_window::Build(definitions);assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NOT_AVAILABLE&&graph_checks==2&&prepared==1&&begins==0&&replacements==0&&original_commits==0);collected_provider_ok=true;
 for(int mode=0;mode<3;++mode){Reset();graph_window::Build(definitions);auto root=reinterpret_cast<jobject>(uintptr_t(*holder.array_handle));
  if(mode==0)managed[managed[root][12]].clear();
  if(mode==1)managed[managed[root][10]].clear();
  if(mode==2)managed[root][4]=nullptr;
  assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NOT_AVAILABLE&&begins==0&&prepared==0&&failures==1);
 }
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
 auto unchanged=new_plan.new_classes;Put32(moved_array,8,2);assert(!rooted_schema_plan::Read(graph,true,reader,&new_plan,&error)&&new_plan.new_classes==unchanged);
 assert(!rooted_schema_plan::Read(graph,false,reader,&new_plan,&error));
 before_commit=nullptr;before_commit_result=nullptr;
 puts("Actual graph window TU: schema paired Class/instance roots, moving-pointer reseal, exact origins, readonly Collected provider refusal before Update, raced/duplicate/missing child refusal and original failure result PASS");
}
