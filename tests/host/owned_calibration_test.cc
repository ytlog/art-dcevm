#include "platform/android13/owned_calibration.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/thread_suspend_scope.h"
#include "platform/android13/field_copy_storage.h"
#include "platform/abi/arm64/android13/field_copy_metadata_abi.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/abi/arm64/android13/validation_metadata_abi.h"
#include "platform/abi/arm64/android13/handle_root_scope_abi.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <string>
#include <sys/mman.h>
using namespace artdcevm;
using namespace artdcevm::android13;
namespace {
struct Spec{const char* name;const char* signature;bool stat;};
const Spec specs[]={
 {"value","I",false},{"wide","J",false},{"counter","I",true},
 {"booleanValue","Z",false},{"byteValue","B",false},{"charValue","C",false},{"shortValue","S",false},{"intValue","I",false},{"longValue","J",false},{"floatValue","F",false},{"doubleValue","D",false},{"referenceValue","Ljava/lang/Object;",false},{"volatileIntValue","I",false},{"volatileLongValue","J",false},{"volatileReferenceValue","Ljava/lang/Object;",false},
 {"staticBooleanValue","Z",true},{"staticByteValue","B",true},{"staticCharValue","C",true},{"staticShortValue","S",true},{"staticIntValue","I",true},{"staticLongValue","J",true},{"staticFloatValue","F",true},{"staticDoubleValue","D",true},{"staticReferenceValue","Ljava/lang/Object;",true},{"staticVolatileIntValue","I",true},{"staticVolatileLongValue","J",true},{"staticVolatileReferenceValue","Ljava/lang/Object;",true},{"arrayValue","[Ljava/lang/Object;",false},{"volatileArrayValue","[Ljava/lang/Object;",false},{"staticArrayValue","[Ljava/lang/Object;",true},{"staticVolatileArrayValue","[Ljava/lang/Object;",true}};
constexpr size_t count=sizeof(specs)/sizeof(specs[0]);
field_copy_abi::ArtField fields[count]{};art_method_abi::ArtMethod method{};
void* klass;void* parent_class;void* class_loader;void* instance;void* reference;void* thread;unsigned char* cards;void* relocated_class;bool move_lookup=false;bool move_public_metadata=false,metadata_moved=false;
std::vector<void*> allocations;
unsigned jni_calls=0,deletes=0,freed_text=0,reads=0,writes=0,monitors=0;
bool wrong_id=false,wrong_reflected=false,wrong_canonical=false,wrong_lookup=false,wrong_modifiers=false,bad_write=false;
bool suspend=false;int scopes=0;
int metadata_fault=0;unsigned metadata_classes=0,metadata_fields=0;
void* Allocate(){static uintptr_t at=0x21000000;void* p=mmap(reinterpret_cast<void*>(at),4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0);assert(p!=MAP_FAILED);at+=0x10000;uint32_t gc=0x30000000u;std::memcpy(static_cast<unsigned char*>(p)+4,&gc,4);allocations.push_back(p);return p;}
size_t Index(jfieldID id){uintptr_t raw=reinterpret_cast<uintptr_t>(id);assert(raw&1);return (raw>>1)-1;}
jfieldID Id(size_t i){return reinterpret_cast<jfieldID>((i+1)*2+1);}
size_t Width(char type){return type=='Z'||type=='B'?1:type=='C'||type=='S'?2:type=='I'||type=='F'||(type=='L'||type=='[')?4:8;}
unsigned char* Address(jobject object,jfieldID id){return static_cast<unsigned char*>(static_cast<void*>(object))+fields[Index(id)].offset;}
jboolean Exception(JNIEnv*){return JNI_FALSE;}
jclass Class(JNIEnv*,jobject){++jni_calls;return reinterpret_cast<jclass>(klass);}
jclass Super(JNIEnv*,jclass k){assert(!scopes);return k==reinterpret_cast<jclass>(klass)?reinterpret_cast<jclass>(parent_class):nullptr;}
jclass Find(JNIEnv*,const char*){++jni_calls;return reinterpret_cast<jclass>(klass);}
void Delete(JNIEnv*,jobject){++deletes;}
jboolean Same(JNIEnv*,jobject a,jobject b){return a==b;}
jobject Alloc(JNIEnv*,jclass){++jni_calls;return reinterpret_cast<jobject>(Allocate());}
jstring NewString(JNIEnv*,const char*){++jni_calls;return reinterpret_cast<jstring>(reference);}
jobjectArray NewArray(JNIEnv*,jsize,jclass,jobject){return reinterpret_cast<jobjectArray>(reference);}
jfieldID FieldId(JNIEnv*,jclass,const char* name,const char* sig){++jni_calls;for(size_t i=0;i<count;++i)if(!std::strcmp(name,specs[i].name)&&!std::strcmp(sig,specs[i].signature))return Id(i);return nullptr;}
jmethodID MethodId(JNIEnv*,jclass,const char*,const char*){++jni_calls;return reinterpret_cast<jmethodID>(1);}
jobject ReflectField(JNIEnv*,jclass,jfieldID id,jboolean){return reinterpret_cast<jobject>(reinterpret_cast<uintptr_t>(id)+0x1000);}
jfieldID UnreflectField(JNIEnv*,jobject ref){return reinterpret_cast<jfieldID>(reinterpret_cast<uintptr_t>(ref)-0x1000);}
jobject ReflectMethod(JNIEnv*,jclass,jmethodID,jboolean){return reinterpret_cast<jobject>(0x2001);}
jmethodID UnreflectMethod(JNIEnv*,jobject){return reinterpret_cast<jmethodID>(1);}
#define JNI_ACCESS(Type,type) type Get##Type(JNIEnv*,jobject object,jfieldID id){type value;std::memcpy(&value,Address(object,id),sizeof(value));return value;} void Set##Type(JNIEnv*,jobject object,jfieldID id,type value){std::memcpy(Address(object,id),&value,sizeof(value));} type GetStatic##Type(JNIEnv* env,jclass object,jfieldID id){return Get##Type(env,object,id);} void SetStatic##Type(JNIEnv* env,jclass object,jfieldID id,type value){Set##Type(env,object,id,value);}
JNI_ACCESS(Boolean,jboolean) JNI_ACCESS(Byte,jbyte) JNI_ACCESS(Char,jchar) JNI_ACCESS(Short,jshort) JNI_ACCESS(Int,jint) JNI_ACCESS(Long,jlong) JNI_ACCESS(Float,jfloat) JNI_ACCESS(Double,jdouble)
jobject GetObject(JNIEnv*,jobject object,jfieldID id){uint32_t raw;std::memcpy(&raw,Address(object,id),4);return reinterpret_cast<jobject>(uintptr_t(raw));}
void SetObject(JNIEnv*,jobject object,jfieldID id,jobject ref){assert(!suspend);uint32_t raw=reinterpret_cast<uintptr_t>(ref);std::memcpy(Address(object,id),&raw,4);cards[(reinterpret_cast<uintptr_t>(object)-0x21000000)>>10]=0x70;}
jobject GetStaticObject(JNIEnv* env,jclass object,jfieldID id){return GetObject(env,object,id);}
void SetStaticObject(JNIEnv* env,jclass object,jfieldID id,jobject ref){SetObject(env,object,id,ref);}
jint HashV(JNIEnv*,jclass,jmethodID,va_list args){assert(!suspend);jobject object=va_arg(args,jobject);uint32_t hash;std::memcpy(&hash,static_cast<unsigned char*>(static_cast<void*>(object))+4,4);if(!(hash&0x0fffffffu)){hash|=12345;std::memcpy(static_cast<unsigned char*>(static_cast<void*>(object))+4,&hash,4);}return hash&0x0fffffffu;}
jint Monitor(JNIEnv*,jobject){assert(!suspend);++monitors;return JNI_OK;}
jvmtiError Free(jvmtiEnv*,unsigned char* text){++freed_text;std::free(text);return JVMTI_ERROR_NONE;}
jvmtiError ClassSignature(jvmtiEnv*,jclass k,char** sig,char**){assert(!scopes);*sig=strdup(k==reinterpret_cast<jclass>(klass)?"LCalibrationFixture;":"Ljava/lang/Object;");return JVMTI_ERROR_NONE;}
jvmtiError PublicLoader(jvmtiEnv*,jclass k,jobject* out){assert(!scopes);*out=k==reinterpret_cast<jclass>(klass)?reinterpret_cast<jobject>(class_loader):nullptr;return JVMTI_ERROR_NONE;}
jvmtiError PublicFields(jvmtiEnv*,jclass k,jint* n,jfieldID** out){assert(!scopes);if(move_public_metadata&&k==reinterpret_cast<jclass>(klass))metadata_moved=true;*n=k==reinterpret_cast<jclass>(klass)?count:0;*out=*n?static_cast<jfieldID*>(std::malloc(count*sizeof(jfieldID))):nullptr;for(int i=0;i<*n;++i)(*out)[i]=Id(count-1-i);return JVMTI_ERROR_NONE;}
CopyClassView CopyClass(void* owner,bool stat){assert(scopes&&(owner==(metadata_moved?relocated_class:klass)||owner==parent_class));++metadata_classes;if(owner==parent_class)return {nullptr,nullptr,metadata_fault==9?"LWrong;":"Ljava/lang/Object;",0,true};size_t n=0;for(const auto& spec:specs)if(spec.stat==stat)++n;
 return {metadata_fault==1?nullptr:parent_class,metadata_fault==2?nullptr:class_loader,metadata_fault==3?"LWrong;":"LCalibrationFixture;",n+(metadata_fault==4),metadata_fault==5};}
CopyFieldView CopyField(void* owner,bool stat,size_t index){assert(scopes&&owner==(metadata_moved?relocated_class:klass));++metadata_fields;
 // The device reader's fixture enumeration is deliberately a different order
 // from JVMTI's result; calibration must match identities, not array positions.
 for(size_t i=0;i<count;++i)if(specs[i].stat==stat){if(index--==0)return {metadata_fault==8?&fields[(i+1)%count]:&fields[i],metadata_fault==6?"wrongName":specs[i].name,metadata_fault==7?"F":specs[i].signature};}
 assert(false);return {};}

jvmtiError FieldName(jvmtiEnv*,jclass,jfieldID id,char** name,char** sig,char**){*name=strdup(specs[Index(id)].name);*sig=strdup(specs[Index(id)].signature);return JVMTI_ERROR_NONE;}
jvmtiError FieldModifiers(jvmtiEnv*,jclass,jfieldID id,jint* mods){*mods=(1|(specs[Index(id)].stat?8:0)|(std::strstr(specs[Index(id)].name,"olatile")?0x40:0))^(wrong_modifiers?8:0);return JVMTI_ERROR_NONE;}
jvmtiError FieldClass(jvmtiEnv*,jclass,jfieldID,jclass* out){*out=reinterpret_cast<jclass>(klass);return JVMTI_ERROR_NONE;}
jvmtiError MethodName(jvmtiEnv*,jmethodID,char** name,char** sig,char**){*name=strdup("compute");*sig=strdup("()I");return JVMTI_ERROR_NONE;}
jvmtiError MethodModifiers(jvmtiEnv*,jmethodID,jint* mods){*mods=1;return JVMTI_ERROR_NONE;}
jvmtiError MethodClass(jvmtiEnv*,jmethodID,jclass* out){*out=reinterpret_cast<jclass>(klass);return JVMTI_ERROR_NONE;}
void* DecodeField(jfieldID id){if(!id)return nullptr;uintptr_t raw=reinterpret_cast<uintptr_t>(id);return raw&1?static_cast<void*>(&fields[Index(id)]):reinterpret_cast<void*>(id);}
void* OwnedField(jfieldID id){return wrong_id&&id?&fields[1]:DecodeField(id);}
void* DecodeMethod(jmethodID id){return id?&method:nullptr;}
void* NamedField(void* manager,jfieldID id){assert(manager==reinterpret_cast<void*>(9));return DecodeField(id);}
void* NamedMethod(void* manager,jmethodID id){assert(manager==reinterpret_cast<void*>(9));return DecodeMethod(id);}
void* ReflectedField(void* ref){return wrong_reflected?&fields[1]:DecodeField(reinterpret_cast<jfieldID>(reinterpret_cast<uintptr_t>(ref)-0x1000));}
void* ReflectedMethod(JNIEnv*,jobject){return &method;}
void* Canonical(void* m){return wrong_canonical?nullptr:m;}
void* NamedCanonical(void* m,size_t size){assert(size==8);return m;}
void* Lookup(void* linker,void* self,const char* name,void* loader){assert(linker==reinterpret_cast<void*>(8)&&self==thread&&loader==class_loader&&!std::strcmp(name,"LCalibrationFixture;"));if(move_lookup){void* top;std::memcpy(&top,static_cast<unsigned char*>(self)+handle_root_abi::kTopHandleScopeOffset,sizeof(top));uint32_t moved=reinterpret_cast<uintptr_t>(relocated_class);std::memcpy(static_cast<unsigned char*>(top)+handle_root_abi::kReferencesOffset,&moved,4);return relocated_class;}return wrong_lookup?nullptr:(metadata_moved?relocated_class:klass);}
uint64_t ReadField(void* owner,void* field,char type){++reads;return ReadCopyValue(owner,field,type);}
void WriteField(void* owner,void* field,char type,uint64_t bits){++writes;WriteCopyValue(owner,field,type,bits^(bad_write&&field==&fields[15]?1:0));}
void Transfer(void* target,void* source){assert(suspend);TransferCopyMonitor(target,source);}
void* Current(){return thread;}
class Hooks:public HookEngine{public:bool Initialize(std::string*)override{return true;}void* FindSymbol(const char*,const char* name)override{
 if(std::strstr(name,"LookupClass"))return reinterpret_cast<void*>(Lookup);if(std::strstr(name,"DecodeMethodId"))return reinterpret_cast<void*>(NamedMethod);if(std::strstr(name,"DecodeFieldId"))return reinterpret_cast<void*>(NamedField);if(std::strstr(name,"GetCanonicalMethod"))return reinterpret_cast<void*>(NamedCanonical);if(std::strstr(name,"CurrentFromGdb"))return reinterpret_cast<void*>(Current);return nullptr;}
 void* Install(void*,void*,void**,std::string*)override{assert(false);return nullptr;}bool Remove(void*,std::string*)override{assert(false);return false;}};
}
namespace artdcevm {LoadedModule FindLoadedModule(const char* name){const char* ids[]={"14fc77b24db0b351b919c97009dc86bf","2fe21c45581b9c8b0519b71cbde32938","e6aaffcef8ef63baf438267cda44d400","1f426797e505c9b841f55cc49d32b3f4"};const char* names[]={"libart.so","libopenjdkjvmti.so","libdexfile.so","libc++.so"};for(unsigned i=0;i<4;++i)if(!std::strcmp(name,names[i]))return {name,ids[i]};return {};}}
namespace artdcevm::android13 {
NativeObjectAccess::NativeObjectAccess(JNIEnv*):self_(thread),supported_(true),transitioned_(false){++scopes;}
NativeObjectAccess::~NativeObjectAccess(){assert(!transitioned_);--scopes;}
void* NativeObjectAccess::Decode(jobject object)const{assert(!suspend);return metadata_moved&&object==reinterpret_cast<jobject>(klass)?relocated_class:object;}
void* ReadHeapReference(void* object,size_t offset,bool){uint32_t raw;std::memcpy(&raw,static_cast<unsigned char*>(object)+offset,4);return reinterpret_cast<void*>(uintptr_t(raw));}
bool ConfigureHeapReferenceReader(HookEngine&,std::string*){return true;}
NativeSuspendScope::NativeSuspendScope(void* self):self_(self),supported_(true),transitioned_(false){assert(scopes&&self==thread);}
NativeSuspendScope::~NativeSuspendScope(){assert(self_==thread&&!transitioned_);}
OwnedSuspendAll::OwnedSuspendAll(const char*,bool){std::memset(&storage_,0,sizeof(storage_));assert(!suspend);suspend=true;}
OwnedSuspendAll::~OwnedSuspendAll(){assert(suspend);suspend=false;}
}
int main(){
 klass=Allocate();parent_class=Allocate();class_loader=Allocate();instance=Allocate();reference=Allocate();thread=Allocate();
 uint32_t loader_ref=reinterpret_cast<uintptr_t>(class_loader);std::memcpy(static_cast<unsigned char*>(klass)+offsetof(validation_abi::ClassPrefix,class_loader),&loader_ref,4);
 // Real production store/barrier reads a fixed thread slot; map all possible
 // fixture owner cards into an independent allocation, never stub the store.
 cards=static_cast<unsigned char*>(mmap(nullptr,0x400000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));assert(cards!=MAP_FAILED);
 uintptr_t bias=reinterpret_cast<uintptr_t>(cards)-(0x21000000>>10);std::memcpy(static_cast<unsigned char*>(thread)+144,&bias,8);
 for(size_t i=0;i<count;++i){fields[i].access_flags=1|(specs[i].stat?8:0)|(std::strstr(specs[i].name,"olatile")?0x40:0);fields[i].offset=256+i*8;
  uint64_t bits=(specs[i].signature[0]=='L'||specs[i].signature[0]=='[')?reinterpret_cast<uintptr_t>(reference):specs[i].signature[0]=='Z'?1:0x8123456789abcdefULL;
  std::memcpy(static_cast<unsigned char*>(specs[i].stat?klass:instance)+fields[i].offset,&bits,Width(specs[i].signature[0]));}
 method.access_flags=1;JNIEnv env{};JNINativeInterface_ table{};env.functions=&table;
 table.ExceptionCheck=Exception;table.GetObjectClass=Class;table.GetSuperclass=Super;table.FindClass=Find;table.DeleteLocalRef=Delete;table.IsSameObject=Same;table.AllocObject=Alloc;table.NewStringUTF=NewString;table.NewObjectArray=NewArray;
 table.GetFieldID=FieldId;table.GetStaticFieldID=FieldId;table.GetMethodID=MethodId;table.GetStaticMethodID=MethodId;table.ToReflectedField=ReflectField;table.FromReflectedField=UnreflectField;table.ToReflectedMethod=ReflectMethod;table.FromReflectedMethod=UnreflectMethod;
#define BIND(Type) table.Get##Type##Field=Get##Type;table.Set##Type##Field=Set##Type;table.GetStatic##Type##Field=GetStatic##Type;table.SetStatic##Type##Field=SetStatic##Type;
 BIND(Boolean) BIND(Byte) BIND(Char) BIND(Short) BIND(Int) BIND(Long) BIND(Float) BIND(Double) BIND(Object)
#undef BIND
 table.CallStaticIntMethodV=HashV;table.MonitorEnter=Monitor;table.MonitorExit=Monitor;
 jvmtiEnv ti{};jvmtiInterface_1 ti_table{};ti.functions=&ti_table;ti_table.Deallocate=Free;ti_table.GetClassSignature=ClassSignature;ti_table.GetClassLoader=PublicLoader;ti_table.GetClassFields=PublicFields;ti_table.GetFieldName=FieldName;ti_table.GetFieldModifiers=FieldModifiers;ti_table.GetFieldDeclaringClass=FieldClass;ti_table.GetMethodName=MethodName;ti_table.GetMethodModifiers=MethodModifiers;ti_table.GetMethodDeclaringClass=MethodClass;
 Hooks hooks;std::string error;DeviceProfile profile;OwnedCalibrationReaders r{DecodeMethod,OwnedField,ReflectedField,ReflectedMethod,Canonical,{CopyClass,CopyField,ReadField,WriteField,Transfer,CopyWriteBarrier}};runtime_access_abi::Services services{reinterpret_cast<void*>(8),reinterpret_cast<void*>(9),nullptr,nullptr};
 assert(!CalibrateOwnedMembers(profile,hooks,&env,&ti,reinterpret_cast<jobject>(instance),services,r,&error)&&jni_calls==0);
 assert(MatchDeviceProfile(&profile,&error));assert(ConfigureFieldCopyStorage(hooks,&error));
 assert(CalibrateOwnedMembers(profile,hooks,&env,&ti,reinterpret_cast<jobject>(instance),services,r,&error));
 assert(metadata_classes==4&&metadata_fields==count);
 for(int fault=1;fault<=9;++fault){metadata_fault=fault;assert(!CalibrateOwnedMembers(profile,hooks,&env,&ti,reinterpret_cast<jobject>(instance),services,r,&error)&&!error.empty());}metadata_fault=0;
 relocated_class=Allocate();std::memcpy(relocated_class,klass,4096);
 move_public_metadata=true;assert(CalibrateOwnedMembers(profile,hooks,&env,&ti,reinterpret_cast<jobject>(instance),services,r,&error));move_public_metadata=false;metadata_moved=false;
 move_lookup=true;assert(CalibrateOwnedMembers(profile,hooks,&env,&ti,reinterpret_cast<jobject>(instance),services,r,&error));move_lookup=false;
 for(bool* fault:{&wrong_id,&wrong_reflected,&wrong_canonical,&wrong_lookup,&wrong_modifiers}){*fault=true;assert(!CalibrateOwnedMembers(profile,hooks,&env,&ti,reinterpret_cast<jobject>(instance),services,r,&error)&&!error.empty());*fault=false;}
 unsigned char snapshot[4096],static_snapshot[4096];std::memcpy(snapshot,instance,4096);std::memcpy(static_snapshot,klass,4096);
 assert(CalibrateOwnedFieldStorage(profile,&env,&ti,reinterpret_cast<jobject>(instance),r,&error));assert(reads==84&&writes==56);
 assert(!std::memcmp(snapshot,instance,4096)&&!std::memcmp(static_snapshot,klass,4096));
 fields[12].access_flags^=0x40;assert(!CalibrateOwnedFieldStorage(profile,&env,&ti,reinterpret_cast<jobject>(instance),r,&error));fields[12].access_flags^=0x40;
 bad_write=true;assert(!CalibrateOwnedFieldStorage(profile,&env,&ti,reinterpret_cast<jobject>(instance),r,&error));bad_write=false;
 assert(!std::memcmp(snapshot,instance,4096)&&!std::memcmp(static_snapshot,klass,4096));
 assert(std::memchr(cards,0x70,0x400000));
 assert(CalibrateOwnedCardTable(profile,&env,reinterpret_cast<jobject>(instance),r,&error));
 const uintptr_t wrong_bias=bias+1;std::memcpy(static_cast<unsigned char*>(thread)+144,&wrong_bias,8);
 assert(!CalibrateOwnedCardTable(profile,&env,reinterpret_cast<jobject>(instance),r,&error));std::memcpy(static_cast<unsigned char*>(thread)+144,&bias,8);
 auto normal_barrier=r.fields.barrier;r.fields.barrier=+[](void*){};
 assert(!CalibrateOwnedCardTable(profile,&env,reinterpret_cast<jobject>(instance),r,&error));r.fields.barrier=normal_barrier;
 std::memset(cards,0x70,0x400000);assert(!CalibrateOwnedCardTable(profile,&env,reinterpret_cast<jobject>(instance),r,&error)&&error.find("inconclusive")!=std::string::npos);
 assert(CalibrateOwnedMonitorTransfer(profile,&env,reinterpret_cast<jobject>(instance),r.fields,&error));assert(monitors==4&&!suspend&&scopes==0&&deletes&&freed_text);
 auto transfer=r.fields.transfer_monitor;r.fields.transfer_monitor=+[](void*,void*){};
 assert(!CalibrateOwnedMonitorTransfer(profile,&env,reinterpret_cast<jobject>(instance),r.fields,&error));r.fields.transfer_monitor=transfer;
 assert(!suspend&&scopes==0);
 for(void* p:allocations)munmap(p,4096);munmap(cards,0x400000);
 puts("owned calibration: public JNI/JVMTI member oracle, complete public copy-metadata oracle and rejection, GC-updated roots, named receiver/ID/canonical calls, real primitive/reference/array/volatile stores, static failure restoration, clean-card oracle/bias/no-op/inconclusive rejection and STW monitor hash passed");
}
