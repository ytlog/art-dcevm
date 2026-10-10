#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_commit_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_cache_public_fields.h"
#include "platform/common/contextual_field_id.h"
#include <cstdlib>
#include <map>
namespace {
int contextual_fault=0,contextual_owner_calls=0,contextual_type_calls=0,contextual_flags_calls=0,name_lookup_calls=0;
int fault=0,leaf=0,trigger=0,globals=0,buffers=0,reads=0,manager,root_attempts=0;
bool pending=false,decode_mismatch=false,native_badname=false,offset_mismatch=false;int weak_refs=0,manager_decodes=0,opaque_mode=0;bool flip_native_after_read=false;
jfieldID Opaque(size_t i){return i==1?reinterpret_cast<jfieldID>(opaque_mode?17:0x12345002):reinterpret_cast<jfieldID>(old_fields[i]);}
std::map<void*,int> root_counts;
jboolean Pending(JNIEnv*){Jni();return pending;}
void Step(){++leaf;if(trigger&&leaf==trigger)pending=true;}
char* Text(const char* s){++buffers;auto* p=static_cast<char*>(std::malloc(std::strlen(s)+1));std::strcpy(p,s);return p;}
jvmtiError Free(jvmtiEnv*,unsigned char* p){assert(p&&buffers>0);--buffers;std::free(p);return JVMTI_ERROR_NONE;}
jvmtiError Signature(jvmtiEnv*,jclass owner,char** s,char**){Step();*s=Text(fault==1?"LFake;":owner==reinterpret_cast<jclass>(meta_class)?"Ljava/lang/Class;":"Ljava/lang/DexCache;");return fault==2?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
jvmtiError Loader(jvmtiEnv*,jclass,jobject* o){Step();*o=fault==3?reinterpret_cast<jobject>(parent_class):nullptr;return JVMTI_ERROR_NONE;}
jvmtiError Fields(jvmtiEnv*,jclass,jint* n,jfieldID** p){Step();*n=fault==4?5:fault==15?3:4;*p=static_cast<jfieldID*>(std::malloc(sizeof(jfieldID)*size_t(*n)));++buffers;for(int i=0;i<*n&&i<4;++i)(*p)[i]=Opaque(size_t(i));if(*n==5)(*p)[4]=(*p)[1];if(fault==15){(*p)[0]=Opaque(0);(*p)[1]=Opaque(2);(*p)[2]=Opaque(3);}if(fault==5)(*p)[2]=nullptr;return fault==6?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
size_t Index(jfieldID id){for(size_t i=0;i<4;++i)if(Opaque(i)==id||reinterpret_cast<jfieldID>(old_fields[i])==id)return i;std::abort();}
jvmtiError Name(jvmtiEnv*,jclass,jfieldID id,char** n,char** t,char**){Step();const auto i=Index(id);*n=Text(names[i]);*t=Text(fault==7&&i==1?"I":types[i]);return fault==8?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
jvmtiError OriginalFields(jvmtiEnv* ti,jclass owner,jint* n,jfieldID** p){const auto saved=fault;fault=0;const auto result=Fields(ti,owner,n,p);fault=saved;return result;}
jvmtiError ClassMethods(jvmtiEnv*,jclass owner,jint* count,jmethodID** out){Step();assert(owner==reinterpret_cast<jclass>(meta_class));*count=1;*out=static_cast<jmethodID*>(std::malloc(sizeof(jmethodID)));++buffers;(*out)[0]=reinterpret_cast<jmethodID>(6);return JVMTI_ERROR_NONE;}
jvmtiError MethodName(jvmtiEnv*,jmethodID id,char** name,char** descriptor,char** generic){Step();assert(uintptr_t(id)==6);*name=Text("getDeclaredFieldsUnchecked");*descriptor=Text("(Z)[Ljava/lang/reflect/Field;");if(generic)*generic=nullptr;return JVMTI_ERROR_NONE;}
jvmtiError MethodFlags(jvmtiEnv*,jmethodID id,jint* out){Step();assert(uintptr_t(id)==6);*out=0x102;return JVMTI_ERROR_NONE;}
jvmtiError MethodOwner(jvmtiEnv*,jmethodID id,jclass* out){Step();assert(uintptr_t(id)==6);*out=reinterpret_cast<jclass>(meta_class);return JVMTI_ERROR_NONE;}
jvmtiError ObjectBytes(jvmtiEnv*,jobject,jlong* out){Step();*out=112;return JVMTI_ERROR_NONE;}
jvmtiError Flags(jvmtiEnv*,jclass,jfieldID id,jint* flags){Step();*flags=Index(id)==0||fault==9?8:2;return JVMTI_ERROR_NONE;}
jvmtiError Declaring(jvmtiEnv*,jclass,jfieldID,jclass* c){Step();*c=reinterpret_cast<jclass>(fault==10?parent_class:old_class);return JVMTI_ERROR_NONE;}
jobject Rooted(JNIEnv*,jobject p){Step();++root_attempts;if(fault==11&&root_attempts==1)return nullptr;++globals;++root_counts[p];return p;}
void Release(JNIEnv*,jobject p){assert(globals>0&&root_counts[p]>0);--globals;--root_counts[p];}
jclass ExactClass(JNIEnv*,jobject p){Step();if(p==reinterpret_cast<jobject>(old_instance))return reinterpret_cast<jclass>(fault==12?parent_class:old_class);return reinterpret_cast<jclass>(meta_class);}
jboolean Exact(JNIEnv*,jobject a,jobject b){Step();return a==b;}
jobject Reflect(JNIEnv*,jclass c,jfieldID id,jboolean statik){Step();assert(c==reinterpret_cast<jclass>(old_class)&&!statik);return fault==13?nullptr:reinterpret_cast<jobject>(&mirrors[Index(id)]);}
jfieldID Roundtrip(JNIEnv*,jobject f){Step();auto* m=reinterpret_cast<Mirror*>(f);return fault==14?Opaque(0):Opaque(Index(reinterpret_cast<jfieldID>(m->field)));}
jlong Long(JNIEnv*,jobject o,jfieldID id){Step();++reads;assert(o==reinterpret_cast<jobject>(old_instance)&&id==Opaque(1));if(flip_native_after_read)decode_mismatch=true;return jlong(0x123400005678ULL);}
void* DecodeId(void* m,jfieldID){assert(m==&manager&&Read32(current)==0);++manager_decodes;return old_fields[0];}
void* OriginalField(void* mirror){auto* f=FieldLeaf(mirror);return decode_mismatch?old_fields[0]:f;}
jclass PublicFind(JNIEnv*,const char*){Step();return reinterpret_cast<jclass>(meta_class);}
jboolean PublicInstance(JNIEnv*,jobject,jclass){Step();return true;}
jmethodID PublicMethod(JNIEnv*,jclass,const char* name,const char*){assert(std::strcmp(name,"getDeclaredFieldsUnchecked"));Step();int id=!std::strcmp(name,"getOffset")?7:!std::strcmp(name,"getDeclaredFieldsUnchecked")?6:!std::strcmp(name,"getDeclaringClass")?1:!std::strcmp(name,"getName")?2:!std::strcmp(name,"getType")?3:!std::strcmp(name,"getModifiers")?4:5;return reinterpret_cast<jmethodID>(uintptr_t(id));}
jobject PublicObject(JNIEnv*,jobject value,jmethodID method,va_list args){Step();if(pending)return nullptr;switch(uintptr_t(method)){case 6:assert(va_arg(args,int)==JNI_FALSE);return reinterpret_cast<jobject>(&mirrors);case 1:return reinterpret_cast<jobject>(contextual_fault==1&&++contextual_owner_calls==2?parent_class:old_class);case 2:return (value==reinterpret_cast<jobject>(parent_class)||(contextual_fault==2&&value==reinterpret_cast<jobject>(meta_class)))?reinterpret_cast<jobject>(const_cast<char*>("long")):reinterpret_cast<jobject>(const_cast<char*>(names[Index(reinterpret_cast<jfieldID>(reinterpret_cast<Mirror*>(value)->field))]));case 3:return reinterpret_cast<jobject>(contextual_fault==2&&++contextual_type_calls==2?meta_class:parent_class);default:std::abort();}}
jsize MirrorLength(JNIEnv*,jarray){Step();return 4;}
jobject MirrorElement(JNIEnv*,jobjectArray,jsize i){Step();return reinterpret_cast<jobject>(&mirrors[size_t(i)]);}
jint PublicInt(JNIEnv*,jobject value,jmethodID method,va_list){Step();const auto* field=reinterpret_cast<abi::arm64::Field*>(reinterpret_cast<Mirror*>(value)->field);return uintptr_t(method)==7?jint(field->offset+(offset_mismatch?8:0)):jint(field->access_flags^(contextual_fault==3&&++contextual_flags_calls==2?1:0));}
jboolean PublicEqual(JNIEnv*,jobject a,jmethodID,va_list args){Step();return !pending&&(contextual_fault||a==va_arg(args,jobject));}
const char* PublicUtf(JNIEnv*,jstring text,jboolean*){Step();return pending?nullptr:reinterpret_cast<const char*>(text);}
void UtfRelease(JNIEnv*,jstring,const char*){}
jfieldID PublicFieldId(JNIEnv*,jclass,const char*,const char*){Step();++name_lookup_calls;pending=true;return nullptr;}
jweak Weak(JNIEnv*,jobject p){Step();++weak_refs;return p;}
void WeakDelete(JNIEnv*,jweak){assert(weak_refs>0);--weak_refs;}
const char* OriginalNativeName(void* field){return native_badname&&field==old_fields[1]?"wrong":FieldName(field);}
jint ActualVM(JNIEnv*,JavaVM** out){*out=reinterpret_cast<JavaVM*>(&runtime);return JNI_OK;}
bool unknown_global=false;
jobjectRefType ActualKind(JNIEnv*,jobject){Step();return unknown_global?JNIInvalidRefType:JNIGlobalRefType;}
void OriginalRoots(void*,abi::arm64::android_common::HandleRootVisitor*,uint32_t){}
class PublicHooks:public Hooks {public:void* FindSymbol(const char* lib,const char* name)override{if(std::strstr(name,"ClassUtil14GetClassFields"))return reinterpret_cast<void*>(OriginalFields);if(std::strstr(name,"FieldUtil12GetFieldName"))return reinterpret_cast<void*>(Name);if(std::strstr(name,"FieldUtil17GetFieldModifiers"))return reinterpret_cast<void*>(Flags);if(std::strstr(name,"HandleScopeVisitRoots")||std::strstr(name,"AllocateNewClassObject"))return reinterpret_cast<void*>(OriginalRoots);if(std::strstr(name,"GetArtFieldEv"))return reinterpret_cast<void*>(OriginalField);if(std::strstr(name,"DecodeFieldId"))return reinterpret_cast<void*>(DecodeId);if(std::strstr(name,"ArtField7GetName"))return reinterpret_cast<void*>(OriginalNativeName);return Hooks::FindSymbol(lib,name);}};
}
int main(){
 InitJni();InitializeHeap();names[1]="resolvedFields";
 PublicHooks hooks;modern_field_metadata::FieldMetadata baseline_metadata;std::string error;
 assert(baseline_metadata.Bind(hooks,&error));std::array<modern_field_metadata::FieldOracle,4> oracle{};
 for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 struct Calibration{modern_field_metadata::FieldMetadata* metadata;const modern_field_metadata::FieldOracle* oracle;std::string* error;} cal{&baseline_metadata,oracle.data(),&error};
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),[](const runnable_jni::Context& c,void* raw){auto& r=*static_cast<Calibration*>(raw);return r.metadata->Calibrate(c,reinterpret_cast<jobject>(old_class),"LFixture;",r.oracle,4,r.error);},&cal)==runnable_jni::Result::kSuccess);
 modern_field_storage::Service storage;assert(storage.Calibrate(hooks,&env,reinterpret_cast<jobject>(old_class),reinterpret_cast<jobject>(old_instance),baseline_metadata,oracle.data(),oracle.size(),&error));
 types[1]="J";old_fields[1]->access_flags=2;oracle[1].descriptor="J";oracle[1].modifiers=2;
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));cal.metadata=&metadata;
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),[](const runnable_jni::Context& c,void* raw){auto& r=*static_cast<Calibration*>(raw);return r.metadata->Calibrate(c,reinterpret_cast<jobject>(old_class),"LFixture;",r.oracle,4,r.error);},&cal)==runnable_jni::Result::kSuccess);
 void* self=thread.data();JNIEnv* actual_env=&env;std::memcpy(thread.data()+216,&self,8);std::memcpy(thread.data()+200,&actual_env,8);
 env_owner.self=self;env_owner.vm=&runtime;table.GetJavaVM=ActualVM;
 source_handle_roots::Service roots;assert(source_handle_roots::Service::Bind(hooks,storage,runnable_jni::Context{self,&env,&runtime},&roots,&error));
 table.GetObjectRefType=ActualKind;table.ExceptionCheck=Pending;table.NewGlobalRef=Rooted;table.DeleteGlobalRef=Release;table.GetObjectClass=ExactClass;table.IsSameObject=Exact;table.ToReflectedField=Reflect;table.FromReflectedField=Roundtrip;table.GetLongField=Long;table.FindClass=PublicFind;table.IsInstanceOf=PublicInstance;table.GetMethodID=PublicMethod;table.CallObjectMethodV=PublicObject;table.CallIntMethodV=PublicInt;table.CallBooleanMethodV=PublicEqual;table.GetStringUTFChars=PublicUtf;table.ReleaseStringUTFChars=UtfRelease;table.GetFieldID=PublicFieldId;table.NewWeakGlobalRef=Weak;table.DeleteWeakGlobalRef=WeakDelete;table.GetArrayLength=MirrorLength;table.GetObjectArrayElement=MirrorElement;
 jvmtiInterface_1_ ti_table{};ti_table.GetClassMethods=ClassMethods;ti_table.GetMethodName=MethodName;ti_table.GetMethodModifiers=MethodFlags;ti_table.GetMethodDeclaringClass=MethodOwner;ti_table.GetObjectSize=ObjectBytes;ti_table.GetClassSignature=Signature;ti_table.GetClassLoader=Loader;ti_table.GetClassFields=Fields;ti_table.GetFieldName=Name;ti_table.GetFieldModifiers=Flags;ti_table.GetFieldDeclaringClass=Declaring;ti_table.Deallocate=Free;jvmtiEnv ti{&ti_table};
 native_cache_public_fields::Service service;runtime_services::Snapshot receiver_snapshot;assert(service.Bind(hooks,receiver_snapshot,&error));
 std::unique_ptr<native_cache_public_fields::Witness> output;
 auto observe=[&]{leaf=0;root_attempts=0;return service.Observe(&env,&ti,reinterpret_cast<jobject>(old_instance),reinterpret_cast<jclass>(old_class),"resolvedFields",metadata,&output,&error);};
 // Ordinary preparation precedes production's source-run context publication:
 // use the explicit caller JVMTI, and reject an absent/stale member context
 // before accessing any JNI/JVMTI or allocating roots.
 leaf=0;const int initial_reads=reads;
 assert(!service.Observe(&env,nullptr,reinterpret_cast<jobject>(old_instance),reinterpret_cast<jclass>(old_class),"resolvedFields",metadata,&output,&error));
 assert(error=="Public cache Field observation lacks caller JNI/JVMTI context"&&leaf==0&&reads==initial_reads&&!output&&globals==0&&buffers==0);
 assert(observe());const int total=leaf;assert(output->Id()==Opaque(1)&&output->NativeField()==old_fields[1]&&output->Value()==0x123400005678LL&&globals==3&&buffers==0);
 // JVMTI-selected hidden resolvedTypes remains a valid exact public mirror.
 // Name-based JNI lookup is denied above. Even forged equals cannot hide a
 // returned mirror whose actual owner/type/full modifiers have changed.
 const auto selected_name=names[1];names[1]="resolvedTypes";
 std::unique_ptr<native_cache_public_fields::Witness> hidden_output;
 assert(service.Observe(&env,&ti,reinterpret_cast<jobject>(old_instance),reinterpret_cast<jclass>(old_class),"resolvedTypes",metadata,&hidden_output,&error));
 assert(hidden_output->Id()==Opaque(1)&&hidden_output->NativeField()==old_fields[1]&&!pending&&name_lookup_calls==0);
 hidden_output.reset();assert(globals==3&&weak_refs==1);
 auto selected=contextual_field_id::Prepare(&env,reinterpret_cast<jobject>(&mirrors[1]),Opaque(1),&error);
 assert(selected&&!pending&&name_lookup_calls==0);
 contextual_field_id::Release(&env,&selected);
 for(contextual_fault=1;contextual_fault<=3;++contextual_fault){
  contextual_owner_calls=contextual_type_calls=contextual_flags_calls=0;
  assert(!contextual_field_id::Prepare(&env,reinterpret_cast<jobject>(&mirrors[1]),Opaque(1),&error)&&!pending);
  assert(error.find("owner/name/type/modifiers round trip differs")!=std::string::npos);
  assert(globals==3&&weak_refs==1&&buffers==0);
 }
 contextual_fault=0;names[1]=selected_name;
 auto* retained=output.get();
 runnable_jni::Context scope{current,&env,&runtime};jlong fresh=-1;
 const auto state=Read32(current);Put32(current,0,0);
 runnable_jni::Context missing_scope{current,nullptr,&runtime};
 assert(!service.Refresh(missing_scope,roots,*output,metadata,&fresh,&error)&&fresh==-1);
 runnable_jni::Context missing_vm{current,&env,nullptr};
 assert(!service.Refresh(missing_vm,roots,*output,metadata,&fresh,&error)&&fresh==-1);
 assert(!runnable_executor::host_executor_active);void* rejected=nullptr;
 assert(!runnable_executor::DecodeStrongGlobal(scope,output->ReflectedField(),&rejected)&&!rejected);
 auto wrong=scope;wrong.self=foreign_thread.data();
 assert(!service.Refresh(wrong,roots,*output,metadata,&fresh,&error)&&fresh==-1&&error=="Cache Field identity roots unavailable");
 wrong=scope;wrong.vm=&linker;assert(!service.Refresh(wrong,roots,*output,metadata,&fresh,&error)&&fresh==-1&&error=="Cache Field identity roots unavailable");
 JNIEnv old_env{&table};wrong=scope;wrong.env=&old_env;
 assert(!service.Refresh(wrong,roots,*output,metadata,&fresh,&error)&&fresh==-1&&error=="Live public cache Field refresh owner unavailable");
 unknown_global=true;assert(!service.Refresh(scope,roots,*output,metadata,&fresh,&error)&&fresh==-1&&error=="Live cache Field/Class references are not retained strong globals");unknown_global=false;
 assert(service.Refresh(scope,roots,*output,metadata,&fresh,&error)&&fresh==output->Value());
 decode_mismatch=true;fresh=-1;assert(!service.Refresh(scope,roots,*output,metadata,&fresh,&error)&&fresh==-1&&error=="Public/native cache Field identity differs");decode_mismatch=false;
 flip_native_after_read=true;fresh=-1;assert(!service.Refresh(scope,roots,*output,metadata,&fresh,&error)&&fresh==-1&&decode_mismatch);flip_native_after_read=false;decode_mismatch=false;
 for(trigger=1;trigger<=3;++trigger){leaf=0;pending=false;fresh=-1;const auto before=reads;
  assert(!service.Refresh(scope,roots,*output,metadata,&fresh,&error)&&fresh==-1&&pending&&leaf==trigger);
  assert(reads==before+(trigger==3));
 }
 trigger=0;pending=false;
 Put32(current,0,state);const int old_reads=reads;
 for(fault=1;fault<=14;++fault){assert(!observe());assert(output.get()==retained&&globals==3&&buffers==0&&reads==old_reads);}
 fault=0;
 // The production caller requests three distinct pair-storage fields. A
 // missing member must expose the complete literal JVMTI inventory, rather
 // than implying that an Array field is an interchangeable pair pointer.
 const auto saved_name=names[1];names[1]="resolvedFieldsArray";
 assert(!observe()&&output.get()==retained&&globals==3&&buffers==0&&reads==old_reads);
 assert(error.find("required=resolvedFields count=4")!=std::string::npos);
 assert(error.find("resolvedFieldsArray:J flags=2")!=std::string::npos);
 assert(error.find(std::string(names[0])+":"+types[0]+" flags=8")!=std::string::npos);
 names[1]=saved_name;
 fault=15;assert(!observe()&&output.get()==retained&&globals==3&&weak_refs==1&&reads==old_reads);
 assert(error.find("required=resolvedFields count=3")!=std::string::npos);
 assert(error.find("original={count=4 status=0 observed={")!=std::string::npos);
 assert(error.find("uncheckedFalse={count=4 observed={")!=std::string::npos);
 assert(error.find("resolvedFields flags=2 nativeOwner=exact offset=")!=std::string::npos);
 assert(error.find("objectSize={bytes=112 status=0}")!=std::string::npos&&error.find("nativeOffset=exact")!=std::string::npos);
 const auto refused_total=leaf;
 for(trigger=1;trigger<=refused_total;++trigger){pending=false;assert(!observe());assert(pending&&leaf==trigger&&output.get()==retained&&globals==3&&weak_refs==1&&buffers==0);}
 trigger=0;pending=false;
 offset_mismatch=true;assert(!observe());offset_mismatch=false;
 assert(error.find("Independent cache mirror public/native offset differs")!=std::string::npos&&output.get()==retained&&globals==3&&buffers==0);
 decode_mismatch=true;assert(!observe());decode_mismatch=false;
 assert(error.find("uncheckedFalse={native-proof-refused: Independent cache mirror public/native name differs}")!=std::string::npos);
 assert(output.get()==retained&&globals==3&&weak_refs==1&&buffers==0&&reads==old_reads);
 const auto diagnostic_owner=old_fields[2]->declaring_class;old_fields[2]->declaring_class=uint32_t(uintptr_t(parent_class));
 assert(!observe());old_fields[2]->declaring_class=diagnostic_owner;
 assert(error.find("Independent cache mirror public/native owner differs")!=std::string::npos&&output.get()==retained&&globals==3&&buffers==0);
 fault=0;
 assert(!service.Observe(&env,&ti,reinterpret_cast<jobject>(old_instance),reinterpret_cast<jclass>(old_class),"resolvedTypes",metadata,&output,&error));
 assert(error.find("required=resolvedTypes count=4")!=std::string::npos&&output.get()==retained&&reads==old_reads);
 decode_mismatch=true;assert(!observe());assert(error.find("Calibrated cache Field name/type differs")!=std::string::npos);decode_mismatch=false;assert(output.get()==retained&&globals==3&&buffers==0&&reads==old_reads);
 native_badname=true;assert(!observe());native_badname=false;assert(output.get()==retained&&globals==3&&buffers==0&&reads==old_reads);
 const auto original_owner=old_fields[1]->declaring_class;old_fields[1]->declaring_class=uint32_t(uintptr_t(parent_class));assert(!observe());old_fields[1]->declaring_class=original_owner;assert(output.get()==retained&&globals==3&&buffers==0&&reads==old_reads);
 // Every actual JNI/JVMTI leaf can produce a pending exception. Stop rather
 // than chaining calls, preserve it, release all returned error buffers/roots.
 for(trigger=1;trigger<=total;++trigger){pending=false;const int before=reads;assert(!observe());if(!(pending&&leaf==trigger&&output.get()==retained&&globals==3&&buffers==0))std::cerr<<"trigger="<<trigger<<" leaf="<<leaf<<" globals="<<globals<<" weak="<<weak_refs<<" pending="<<pending<<" buffers="<<buffers<<" error="<<error<<"\n";
 assert(pending&&leaf==trigger&&output.get()==retained&&globals==3&&buffers==0);assert(reads==before||trigger==total);}
 trigger=0;pending=true;leaf=0;assert(!observe()&&leaf==0);pending=false;
 opaque_mode=1;assert(observe()&&output->Id()==Opaque(1)&&output->NativeField()==old_fields[1]&&globals==3&&weak_refs==1);assert(manager_decodes==0);
 assert(name_lookup_calls==0);output.reset();assert(globals==0&&buffers==0&&weak_refs==0&&manager_decodes==0);std::cout<<"Public DexCache field oracle: actual calibrated metadata, opaque original field IDs, exact bootstrap Class, native identity, readonly JNI long, every-leaf pending stop and owned-root/error-buffer lifetime passed\n";
}

namespace artdcevm::runnable_executor {bool Available(){return true;}}
