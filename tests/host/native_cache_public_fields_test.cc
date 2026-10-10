#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_commit_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_cache_public_fields.h"
#include <cstdlib>
#include <map>
namespace {
int fault=0,leaf=0,trigger=0,globals=0,buffers=0,reads=0,manager,root_attempts=0;
bool pending=false,decode_mismatch=false,native_badname=false;
std::map<void*,int> root_counts;
jboolean Pending(JNIEnv*){Jni();return pending;}
void Step(){++leaf;if(trigger&&leaf==trigger)pending=true;}
char* Text(const char* s){++buffers;auto* p=static_cast<char*>(std::malloc(std::strlen(s)+1));std::strcpy(p,s);return p;}
jvmtiError Free(jvmtiEnv*,unsigned char* p){assert(p&&buffers>0);--buffers;std::free(p);return JVMTI_ERROR_NONE;}
jvmtiError Signature(jvmtiEnv*,jclass,char** s,char**){Step();*s=Text(fault==1?"LFake;":"Ljava/lang/DexCache;");return fault==2?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
jvmtiError Loader(jvmtiEnv*,jclass,jobject* o){Step();*o=fault==3?reinterpret_cast<jobject>(parent_class):nullptr;return JVMTI_ERROR_NONE;}
jvmtiError Fields(jvmtiEnv*,jclass,jint* n,jfieldID** p){Step();*n=fault==4?5:4;*p=static_cast<jfieldID*>(std::malloc(sizeof(jfieldID)*size_t(*n)));++buffers;for(int i=0;i<4;++i)(*p)[i]=reinterpret_cast<jfieldID>(old_fields[size_t(i)]);if(*n==5)(*p)[4]=(*p)[1];if(fault==5)(*p)[2]=nullptr;return fault==6?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
size_t Index(jfieldID id){for(size_t i=0;i<4;++i)if(reinterpret_cast<jfieldID>(old_fields[i])==id)return i;std::abort();}
jvmtiError Name(jvmtiEnv*,jclass,jfieldID id,char** n,char** t,char**){Step();const auto i=Index(id);*n=Text(names[i]);*t=Text(fault==7&&i==1?"I":types[i]);return fault==8?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
jvmtiError Flags(jvmtiEnv*,jclass,jfieldID id,jint* flags){Step();*flags=Index(id)==0||fault==9?8:2;return JVMTI_ERROR_NONE;}
jvmtiError Declaring(jvmtiEnv*,jclass,jfieldID,jclass* c){Step();*c=reinterpret_cast<jclass>(fault==10?parent_class:old_class);return JVMTI_ERROR_NONE;}
jobject Rooted(JNIEnv*,jobject p){Step();++root_attempts;if(fault==11&&root_attempts==1)return nullptr;++globals;++root_counts[p];return p;}
void Release(JNIEnv*,jobject p){assert(globals>0&&root_counts[p]>0);--globals;--root_counts[p];}
jclass ExactClass(JNIEnv*,jobject p){Step();assert(p==reinterpret_cast<jobject>(old_instance));return reinterpret_cast<jclass>(fault==12?parent_class:old_class);}
jboolean Exact(JNIEnv*,jobject a,jobject b){Step();return a==b;}
jobject Reflect(JNIEnv*,jclass c,jfieldID id,jboolean statik){Step();assert(c==reinterpret_cast<jclass>(old_class)&&!statik);return fault==13?nullptr:reinterpret_cast<jobject>(&mirrors[Index(id)]);}
jfieldID Roundtrip(JNIEnv*,jobject f){Step();auto* m=reinterpret_cast<Mirror*>(f);return reinterpret_cast<jfieldID>(fault==14?old_fields[0]:m->field);}
jlong Long(JNIEnv*,jobject o,jfieldID id){Step();++reads;assert(o==reinterpret_cast<jobject>(old_instance)&&id==reinterpret_cast<jfieldID>(old_fields[1]));return jlong(0x123400005678ULL);}
void* DecodeId(void* m,jfieldID id){assert(m==&manager&&Read32(current)==0);return decode_mismatch?old_fields[0]:reinterpret_cast<void*>(id);}
const char* OriginalNativeName(void* field){return native_badname&&field==old_fields[1]?"wrong":FieldName(field);}
class PublicHooks:public Hooks {public:void* FindSymbol(const char* lib,const char* name)override{if(std::strstr(name,"DecodeFieldId"))return reinterpret_cast<void*>(DecodeId);if(std::strstr(name,"ArtField7GetName"))return reinterpret_cast<void*>(OriginalNativeName);return Hooks::FindSymbol(lib,name);}};
}
int main(){
 InitJni();InitializeHeap();names[1]="resolvedFields";types[1]="J";old_fields[1]->access_flags=2;
 PublicHooks hooks;modern_field_metadata::FieldMetadata metadata;std::string error;
 assert(metadata.Bind(hooks,&error));std::array<modern_field_metadata::FieldOracle,4> oracle{};
 for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:i==1?2u:1u};
 struct Calibration{modern_field_metadata::FieldMetadata* metadata;const modern_field_metadata::FieldOracle* oracle;std::string* error;} cal{&metadata,oracle.data(),&error};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),[](const runnable_jni::Context& c,void* raw){auto& r=*static_cast<Calibration*>(raw);return r.metadata->Calibrate(c,reinterpret_cast<jobject>(old_class),"LFixture;",r.oracle,4,r.error);},&cal)==runnable_jni::Result::kSuccess);
 table.ExceptionCheck=Pending;table.NewGlobalRef=Rooted;table.DeleteGlobalRef=Release;table.GetObjectClass=ExactClass;table.IsSameObject=Exact;table.ToReflectedField=Reflect;table.FromReflectedField=Roundtrip;table.GetLongField=Long;
 jvmtiInterface_1_ ti_table{};ti_table.GetClassSignature=Signature;ti_table.GetClassLoader=Loader;ti_table.GetClassFields=Fields;ti_table.GetFieldName=Name;ti_table.GetFieldModifiers=Flags;ti_table.GetFieldDeclaringClass=Declaring;ti_table.Deallocate=Free;jvmtiEnv ti{&ti_table};
 native_cache_public_fields::Service service;runtime_services::Snapshot runtime;assert(!service.Bind(hooks,runtime,&error));runtime.receivers.jni_id_manager=&manager;assert(service.Bind(hooks,runtime,&error));
 std::unique_ptr<native_cache_public_fields::Witness> output;
 auto observe=[&]{leaf=0;root_attempts=0;return service.Observe(&env,&ti,reinterpret_cast<jobject>(old_instance),reinterpret_cast<jclass>(old_class),"resolvedFields",metadata,&output,&error);};
 // Ordinary preparation precedes production's source-run context publication:
 // use the explicit caller JVMTI, and reject an absent/stale member context
 // before accessing any JNI/JVMTI or allocating roots.
 leaf=0;const int initial_reads=reads;
 assert(!service.Observe(&env,nullptr,reinterpret_cast<jobject>(old_instance),reinterpret_cast<jclass>(old_class),"resolvedFields",metadata,&output,&error));
 assert(error=="Public cache Field observation lacks caller JNI/JVMTI context"&&leaf==0&&reads==initial_reads&&!output&&globals==0&&buffers==0);
 assert(observe());const int total=leaf;assert(output->Id()==reinterpret_cast<jfieldID>(old_fields[1])&&output->NativeField()==old_fields[1]&&output->Value()==0x123400005678LL&&globals==3&&buffers==0);
 auto* retained=output.get();
 runnable_jni::Context scope{current,&env,&runtime};jlong fresh=-1;
 const auto state=Read32(current);Put32(current,0,0);
 runnable_jni::Context missing_scope{current,nullptr,&runtime};
 assert(!service.Refresh(missing_scope,*output,metadata,&fresh,&error)&&fresh==-1);
 runnable_jni::Context missing_vm{current,&env,nullptr};
 assert(!service.Refresh(missing_vm,*output,metadata,&fresh,&error)&&fresh==-1);
 assert(service.Refresh(scope,*output,metadata,&fresh,&error)&&fresh==output->Value());
 decode_mismatch=true;fresh=-1;assert(!service.Refresh(scope,*output,metadata,&fresh,&error)&&fresh==-1);decode_mismatch=false;
 Put32(current,0,state);const int old_reads=reads;
 for(fault=1;fault<=14;++fault){assert(!observe());assert(output.get()==retained&&globals==3&&buffers==0&&reads==old_reads);}
 fault=0;decode_mismatch=true;assert(!observe());decode_mismatch=false;assert(output.get()==retained&&globals==3&&buffers==0&&reads==old_reads);
 native_badname=true;assert(!observe());native_badname=false;assert(output.get()==retained&&globals==3&&buffers==0&&reads==old_reads);
 const auto original_owner=old_fields[1]->declaring_class;old_fields[1]->declaring_class=uint32_t(uintptr_t(parent_class));assert(!observe());old_fields[1]->declaring_class=original_owner;assert(output.get()==retained&&globals==3&&buffers==0&&reads==old_reads);
 // Every actual JNI/JVMTI leaf can produce a pending exception. Stop rather
 // than chaining calls, preserve it, release all returned error buffers/roots.
 for(trigger=1;trigger<=total;++trigger){pending=false;const int before=reads;assert(!observe());assert(pending&&leaf==trigger&&output.get()==retained&&globals==3&&buffers==0);assert(reads==before||trigger==total);}
 trigger=0;pending=true;leaf=0;assert(!observe()&&leaf==0);pending=false;
 output.reset();assert(globals==0&&buffers==0);std::cout<<"Public DexCache field oracle: actual calibrated metadata, opaque original field IDs, exact bootstrap Class, native identity, readonly JNI long, every-leaf pending stop and owned-root/error-buffer lifetime passed\n";
}
