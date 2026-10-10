#include "platform/common/public_declared_field_mirror.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace artdcevm;
namespace {
JNINativeInterface_ table{};JNIEnv env{&table};jvmtiInterface_1_ jtable{};jvmtiEnv ti{&jtable};
int buffers=0,ti_failure=0;bool empty_fields=false;bool missing_accessor=false,static_accessor=false,duplicate_accessor=false,foreign_accessor=false,foreign_bootstrap=false,noarg_accessor=false;
int calls=0,fail_at=0,locals=0,globals=0,enumerations=0;bool pending=false,wrong_owner=false,wrong_static=false,duplicate=false,bulk=false,moved=false;
constexpr jsize bulk_count=1024;int element_reads=0,id_reads=0;
jobject BulkField(jsize n){return reinterpret_cast<jobject>((moved?0x90000000ULL:0x80000000ULL)+uintptr_t(n)*32);}
jsize BulkOrdinal(jobject f){return jsize((uintptr_t(f)-(moved?0x90000000ULL:0x80000000ULL))/32);}
jfieldID BulkId(jsize n){return reinterpret_cast<jfieldID>(0xa0000001ULL+uintptr_t(n)*4);}
auto owner=reinterpret_cast<jclass>(0x100001000ULL);auto type=reinterpret_cast<jclass>(0x200002000ULL);auto array=reinterpret_cast<jobjectArray>(0x300003000ULL);auto field1=reinterpret_cast<jobject>(0x400004000ULL);auto field2=reinterpret_cast<jobject>(0x500005000ULL);
auto id1=reinterpret_cast<jfieldID>(0x600006002ULL);auto id2=reinterpret_cast<jfieldID>(0x700007001ULL);
bool Step(){assert(!pending);++calls;if(calls==fail_at){pending=true;return false;}return true;}
jboolean Exception(JNIEnv*){return pending;}
jclass Type(JNIEnv*,jobject){if(!Step())return nullptr;++locals;return type;}
jmethodID Method(JNIEnv*,jclass,const char*name,const char*){assert(std::strcmp(name,"getDeclaredFieldsUnchecked"));if(!Step())return nullptr;uintptr_t value=!std::strcmp(name,"getDeclaredFieldsUnchecked")?1:!std::strcmp(name,"getDeclaringClass")?2:!std::strcmp(name,"getModifiers")?3:0;assert(value);return reinterpret_cast<jmethodID>(value);}
jobject Object(JNIEnv*,jobject object,jmethodID method,va_list args){if(!Step())return nullptr;++locals;if(uintptr_t(method)==1){assert(object==owner&&va_arg(args,int)==JNI_FALSE);++enumerations;return array;}assert(uintptr_t(method)==2);return wrong_owner?reinterpret_cast<jobject>(8):owner;}
jsize Length(JNIEnv*,jarray object){assert(object==array);Step();return empty_fields?0:bulk?bulk_count:2;}
jobject Element(JNIEnv*,jobjectArray object,jsize index){assert(object==array);if(!Step())return nullptr;++locals;++element_reads;return bulk?BulkField(index):index?field2:field1;}
jfieldID From(JNIEnv*,jobject field){Step();++id_reads;return bulk?BulkId(BulkOrdinal(field)):field==field1||duplicate?id1:id2;}
jboolean Same(JNIEnv*,jobject a,jobject b){Step();return a==b;}
jint Flags(JNIEnv*,jobject field,jmethodID method,va_list){assert(uintptr_t(method)==3);Step();return ((bulk?(BulkOrdinal(field)%2)!=0:field==field2)^wrong_static)?8:0;}
jobject Global(JNIEnv*,jobject object){if(!Step())return nullptr;++globals;return object;}
void DeleteLocal(JNIEnv*,jobject){--locals;assert(locals>=0);}
void DeleteGlobal(JNIEnv*,jobject){--globals;assert(globals>=0);}
jclass Find(JNIEnv*,const char* name){assert(!std::strcmp(name,"java/lang/Class"));if(!Step())return nullptr;++locals;return type;}
char* Text(const char* value){++buffers;return strdup(value);}
jvmtiError Free(jvmtiEnv*,unsigned char* value){assert(value);--buffers;assert(buffers>=0);free(value);return JVMTI_ERROR_NONE;}
jvmtiError Signature(jvmtiEnv*,jclass c,char** text,char**){assert(c==type);Step();*text=Text("Ljava/lang/Class;");return ti_failure==1?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
jvmtiError Loader(jvmtiEnv*,jclass c,jobject* loader){assert(c==type);Step();*loader=foreign_bootstrap?owner:nullptr;if(*loader)++locals;return ti_failure==2?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
jvmtiError Methods(jvmtiEnv*,jclass c,jint* count,jmethodID** methods){assert(c==type);Step();*count=duplicate_accessor?2:1;*methods=static_cast<jmethodID*>(malloc(sizeof(jmethodID)*size_t(*count)));++buffers;for(int n=0;n<*count;++n)(*methods)[n]=reinterpret_cast<jmethodID>(1);return ti_failure==3?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
jvmtiError MethodText(jvmtiEnv*,jmethodID method,char** name,char** descriptor,char**){assert(uintptr_t(method)==1);Step();*name=Text(missing_accessor?"getDeclaredFields":"getDeclaredFieldsUnchecked");*descriptor=Text(noarg_accessor?"()[Ljava/lang/reflect/Field;":"(Z)[Ljava/lang/reflect/Field;");return ti_failure==4?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
jvmtiError MethodFlags(jvmtiEnv*,jmethodID,jint* flags){Step();*flags=static_accessor?0x108:0x101;return ti_failure==5?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
jvmtiError MethodOwner(jvmtiEnv*,jmethodID,jclass* c){Step();*c=foreign_accessor?owner:type;++locals;return ti_failure==6?JVMTI_ERROR_INTERNAL:JVMTI_ERROR_NONE;}
void Reset(){assert(!locals&&!globals&&!buffers);ti_failure=0;empty_fields=false;missing_accessor=static_accessor=duplicate_accessor=foreign_accessor=foreign_bootstrap=noarg_accessor=false;calls=fail_at=0;pending=wrong_owner=wrong_static=duplicate=bulk=moved=false;element_reads=id_reads=0;}
}
int main(){jtable.Deallocate=Free;jtable.GetClassSignature=Signature;jtable.GetClassLoader=Loader;jtable.GetClassMethods=Methods;jtable.GetMethodName=MethodText;jtable.GetMethodModifiers=MethodFlags;jtable.GetMethodDeclaringClass=MethodOwner;table.FindClass=Find;table.ExceptionCheck=Exception;table.GetObjectClass=Type;table.GetMethodID=Method;table.CallObjectMethodV=Object;table.CallIntMethodV=Flags;table.GetArrayLength=Length;table.GetObjectArrayElement=Element;table.FromReflectedField=From;table.IsSameObject=Same;table.NewGlobalRef=Global;table.DeleteLocalRef=DeleteLocal;table.DeleteGlobalRef=DeleteGlobal;
 for(bool statik:{false,true}){Reset();jobject result=nullptr;std::string error;assert(public_declared_field_mirror::Create(&env,&ti,owner,statik?id2:id1,statik,&result,&error));assert(result==(statik?field2:field1)&&!locals&&globals==1);int total=calls;DeleteGlobal(&env,result);
  for(int fail=1;fail<=total;++fail){Reset();fail_at=fail;result=reinterpret_cast<jobject>(1);assert(!public_declared_field_mirror::Create(&env,&ti,owner,statik?id2:id1,statik,&result,&error));assert(!result&&pending&&calls==fail&&!locals&&!globals);}
 }
 for(int fault=0;fault<4;++fault){Reset();wrong_owner=fault==0;wrong_static=fault==1;duplicate=fault==2;jobject result=nullptr;std::string error;auto id=fault==3?reinterpret_cast<jfieldID>(9):id1;assert(!public_declared_field_mirror::Create(&env,&ti,owner,id,false,&result,&error));assert(!result&&!locals&&!globals&&!pending);}
 Reset();enumerations=0;public_declared_field_mirror::Cache cache;jobject cached=nullptr;std::string cache_error;assert(cache.Bind(&env,&ti,&cache_error));
 assert(cache.Create(&env,owner,id1,false,&cached,&cache_error));DeleteGlobal(&env,cached);
 assert(cache.Create(&env,owner,id2,true,&cached,&cache_error));DeleteGlobal(&env,cached);
 assert(enumerations==1&&globals==3&&!locals);
 wrong_static=true;assert(!cache.Create(&env,owner,id1,false,&cached,&cache_error)&&!cached);wrong_static=false;
 duplicate=true;assert(!cache.Create(&env,owner,id2,true,&cached,&cache_error)&&!cached);duplicate=false;
 assert(enumerations==1&&globals==3);cache.Clear(&env);assert(!globals);
 assert(cache.Bind(&env,&ti,&cache_error));assert(cache.Create(&env,owner,id1,false,&cached,&cache_error));DeleteGlobal(&env,cached);assert(enumerations==2);
 owner=reinterpret_cast<jclass>(0x100002000ULL);
 assert(cache.Create(&env,owner,id2,true,&cached,&cache_error));DeleteGlobal(&env,cached);assert(enumerations==3&&globals==3);
 pending=true;int before=calls;assert(!cache.Create(&env,owner,id1,false,&cached,&cache_error)&&calls==before);cache.Clear(&env);assert(!globals&&!locals);pending=false;
 Reset();bulk=true;enumerations=0;assert(cache.Bind(&env,&ti,&cache_error));
 for(jsize n=0;n<bulk_count;++n){assert(cache.Create(&env,owner,BulkId(n),(n%2)!=0,&cached,&cache_error));assert(cached==BulkField(n));DeleteGlobal(&env,cached);}
 assert(enumerations==1&&element_reads==bulk_count*2&&id_reads==bulk_count*2&&globals==3&&!locals);
 // A moving rooted array returns current mirrors; cached ordinals cannot retain
 // old mirror addresses. No enumeration or full scan is repeated after movement.
 moved=true;const int reads=element_reads;
 for(jsize n=0;n<bulk_count;++n){assert(cache.Create(&env,owner,BulkId(n),(n%2)!=0,&cached,&cache_error));assert(cached==BulkField(n));DeleteGlobal(&env,cached);}
 assert(element_reads-reads==bulk_count&&enumerations==1&&globals==3&&!locals);
 cache.Clear(&env);assert(!globals);
 for(int fault=0;fault<6;++fault){Reset();missing_accessor=fault==0;static_accessor=fault==1;duplicate_accessor=fault==2;foreign_accessor=fault==3;foreign_bootstrap=fault==4;noarg_accessor=fault==5;
  jobject rejected=reinterpret_cast<jobject>(1);std::string error;assert(!public_declared_field_mirror::Create(&env,&ti,owner,id1,false,&rejected,&error));assert(!rejected&&!globals&&!locals&&!buffers&&!pending);
 }
 for(int stage=1;stage<=6;++stage){Reset();ti_failure=stage;jobject rejected=reinterpret_cast<jobject>(1);std::string error;
  assert(!public_declared_field_mirror::Create(&env,&ti,owner,id1,false,&rejected,&error)&&!rejected&&!pending&&!buffers&&!globals&&!locals);
 }
 Reset();empty_fields=true;jobject empty=nullptr;std::string empty_error;
 assert(!public_declared_field_mirror::Create(&env,&ti,owner,id1,false,&empty,&empty_error)&&!empty&&!globals&&!locals&&!buffers&&!pending);
 Reset();pending=true;jobject result=reinterpret_cast<jobject>(1);std::string error;assert(!public_declared_field_mirror::Create(&env,&ti,owner,id1,false,&result,&error)&&!result&&!calls&&pending);
 std::puts("Original declared-field mirror boundary: even/odd opaqueIDs, exact owner/static, missing/duplicate refusal, every throwing JNI leaf cleanup, no getType/ToReflected(true) PASS");
}
