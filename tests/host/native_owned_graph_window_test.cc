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
int driver=0,failures=0,graph_checks=0;bool original_ok=true,field_ok=true,late_child=false;
jvmtiError recorded=JVMTI_ERROR_NONE;
bool Original(void* d,transaction_root::Holder* h){assert(d==&driver&&h==&batch_boundary::holder&&!stopped);if(!original_ok)recorded=JVMTI_ERROR_FAILS_VERIFICATION;return original_ok;}
void Failure(void* d,jvmtiError code,const void* klass,const void* message){assert(d==&driver&&klass&&message&&!stopped);auto& text=*static_cast<const android13::BorrowedPlatformString*>(message);assert(text.length&&text.data);++failures;recorded=code;}
void* RootBarrier(void* handle){assert(!stopped);return reinterpret_cast<void*>(uintptr_t(Read32(handle)));}
jobject Add(void*,void* self,abi::arm64::android11::ReleaseObjectPointer value){assert(!stopped&&self==current);return reinterpret_cast<jobject>(value.reference);}
jsize Length(JNIEnv*,jarray array){Jni();return managed.at(array).size();}
jobject Element(JNIEnv*,jobjectArray array,jsize index){Jni();return managed.at(array).at(index);}
jint Vm(JNIEnv*,JavaVM** out){Jni();*out=reinterpret_cast<JavaVM*>(&runtime);return JNI_OK;}
jclass ForbiddenFind(JNIEnv*,const char*){assert(false&&"Paused graph window cannot load classes");return nullptr;}
bool Check(JNIEnv* env,jvmtiEnv*,const std::vector<jclass>& actual,void*,std::string* error){
 ++graph_checks;if(actual.size()!=staged.size()||!field_ok){*error="Unstaged child or changed field proof";return false;}
 for(auto klass:actual){size_t matches=0;for(auto candidate:staged)matches+=env->IsSameObject(klass,candidate);if(matches!=1){*error="Class namespace mismatch";return false;}}
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
  auto objects=reinterpret_cast<jobject>(heap.New(16));managed[objects]={old};slots[n*13+11]=objects;}
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
 before_commit=nullptr;before_commit_result=nullptr;
 puts("Actual graph window TU: paused rooted graph bijection, raced/duplicate/missing child and stale field rejection before publication, original RecordFailure result and no class-loading JNI PASS");
}
