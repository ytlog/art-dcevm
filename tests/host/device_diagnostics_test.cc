#include "platform/android13/device_diagnostics.h"
#include "platform/android13/device_diagnostics_abi.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/member_decode_abi.h"
#include "platform/android13/art_method_abi.h"
#include <functional>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <vector>
#ifndef _LIBCPP_VERSION
#error This oracle requires libc++.
#endif
using namespace artdcevm::android13;
using Function=std::function<void(const void*,void*)>;
using Method=art_method_abi::ArtMethod;
JNINativeInterface_ normal_table{},checked_table{},unknown_table{};
Method method{},other{};member_decode_abi::Executable executable{};
diagnostics_abi::InstrumentationPrefix instrumentation{};
void* cache=reinterpret_cast<void*>(111),*jit_mutex=reinterpret_cast<void*>(222);
bool cached=false,pc_cached=false,access_supported=true,locked=false;
const void* oat=nullptr,*invocation=nullptr,*bridge_entry=nullptr,*nterp_entry=nullptr;
size_t invalidations=0,scope_count=0,lock_count=0,visits=0;
std::vector<std::pair<const void*,void*>> entries;
namespace artdcevm::android13 {
NativeObjectAccess::NativeObjectAccess(JNIEnv*):self_(reinterpret_cast<void*>(333)),supported_(access_supported),transitioned_(false){++scope_count;}
NativeObjectAccess::~NativeObjectAccess(){assert(!transitioned_);--scope_count;}
void* NativeObjectAccess::Decode(jobject value)const{return value;}
void* RuntimeJitCodeCache(){return cache;}void* RuntimeInstrumentation(){return &instrumentation;}void* RuntimeClassLinker(){return reinterpret_cast<void*>(444);}
uint64_t HeapReferenceMarkCalls(){return 17;}
}
const JNINativeInterface_* Normal(){return &normal_table;}const JNINativeInterface_* Checked(){return &checked_table;}
void SetCheck(JNIEnv* env,bool enable){env->functions=enable?&checked_table:&normal_table;}
bool Contains(void* value,void* m){assert(scope_count&&value==cache&&m==&method);return cached;}
bool ContainsPc(const void* value,const void*){assert(scope_count&&value==cache);return pc_cached;}
const void* Oat(void* m,size_t size){assert(m==&method&&size==8);return oat;}
bool Bridge(const void* linker,const void* entry){assert(linker==reinterpret_cast<void*>(444));return entry==bridge_entry;}
const void* Nterp(){return nterp_entry;}const void* Invoke(void* value,void* m){assert(value==&instrumentation&&m==&method);return invocation;}
void Invalidate(void* value){assert(value==cache&&scope_count);++invalidations;}
void Lock(void* value,void* thread){assert(value==jit_mutex&&thread==reinterpret_cast<void*>(333)&&!locked);locked=true;++lock_count;}
void Unlock(void* value,void* thread){assert(value==jit_mutex&&thread==reinterpret_cast<void*>(333)&&locked);locked=false;}
void Visit(void* value,const diagnostics_abi::BorrowedFunction* raw){assert(locked&&value==cache);++visits;const auto& fn=*reinterpret_cast<const Function*>(raw);for(auto entry:entries)fn(entry.first,entry.second);}
extern "C" bool ArtDcevmSourceProbeDeviceDiagnostics(int (*kind)(const void*),const diagnostics_abi::BorrowedFunction* raw,void* value){
 static_assert(sizeof(Function)==sizeof(*raw));static_assert(alignof(Function)==alignof(diagnostics_abi::BorrowedFunction));assert(value==&instrumentation);const auto& fn=*reinterpret_cast<const Function*>(raw);fn(nullptr,nullptr);
 alignas(8) unsigned char bytes[32]{};auto* code=bytes+16;uint32_t data=8;std::memcpy(code-4,&data,4);assert(kind(code)==1);data=0x40000010;std::memcpy(code-4,&data,4);
 for(unsigned char flags:{0,1,2,3}){bytes[0]=flags;assert(kind(code)==(flags&1?3:2));fn(code,reinterpret_cast<void*>(1));}return true;}
class Hooks:public artdcevm::HookEngine{public:bool Initialize(std::string*)override{return true;}
 void* FindSymbol(const char*,const char* s)override{
 if(std::strstr(s,"GetJniNativeInterface"))return reinterpret_cast<void*>(Normal);if(std::strstr(s,"GetCheckJniNativeInterface"))return reinterpret_cast<void*>(Checked);if(std::strstr(s,"SetCheckJniEnabled"))return reinterpret_cast<void*>(SetCheck);
 if(std::strstr(s,"ContainsMethod"))return reinterpret_cast<void*>(Contains);if(std::strstr(s,"ContainsPc"))return reinterpret_cast<void*>(ContainsPc);if(std::strstr(s,"GetOatMethodQuickCode"))return reinterpret_cast<void*>(Oat);
 if(std::strstr(s,"IsQuickToInterpreterBridge"))return reinterpret_cast<void*>(Bridge);if(std::strstr(s,"GetNterpEntryPoint"))return reinterpret_cast<void*>(Nterp);if(std::strstr(s,"GetCodeForInvoke"))return reinterpret_cast<void*>(Invoke);
 if(std::strstr(s,"InvalidateAllCompiledCode"))return reinterpret_cast<void*>(Invalidate);if(std::strstr(s,"jit_lock"))return &jit_mutex;
 if(std::strstr(s,"ExclusiveLock"))return reinterpret_cast<void*>(Lock);if(std::strstr(s,"ExclusiveUnlock"))return reinterpret_cast<void*>(Unlock);if(std::strstr(s,"VisitAllMethods"))return reinterpret_cast<void*>(Visit);return nullptr;}
 void* Install(void*,void*,void**,std::string*)override{return nullptr;}bool Remove(void*,std::string*)override{return true;}};
int main(){Hooks hooks;std::string error;assert(ConfigureDeviceDiagnostics(hooks,dlopen(nullptr,RTLD_NOW),&error));JNIEnv env{&normal_table};
 assert(DiagnosticCheckJniMode(&env,-1)==0);assert(DiagnosticCheckJniMode(&env,1)==0&&env.functions==&checked_table);assert(DiagnosticCheckJniMode(&env,0)==1&&env.functions==&normal_table);env.functions=&unknown_table;assert(DiagnosticCheckJniMode(&env,7)==-1&&env.functions==&unknown_table);
 assert(DiagnosticReadBarrierCalls()==17);executable.art_method=reinterpret_cast<uint64_t>(&method);jobject object=reinterpret_cast<jobject>(&executable);
 cache=nullptr;assert(DiagnosticJitMethodState(&env,object)==0&&DiagnosticJitCacheDetail(&env,object)==-1&&DiagnosticInvalidateAllJit(&env)==-1);
 cache=reinterpret_cast<void*>(111);assert(DiagnosticJitMethodState(&env,object)==1);cached=true;assert(DiagnosticJitMethodState(&env,object)==2);assert(DiagnosticJitInlineState(&env,object)==1&&DiagnosticJitCodeKind(&env,object)==0);
 alignas(8) unsigned char baseline[32]{},optimized[32]{},inline_code[32]{};const void* b=baseline+16,*o=optimized+16,*i=inline_code+16;uint32_t data=8;std::memcpy(baseline+12,&data,4);data=0x40000010;std::memcpy(optimized+12,&data,4);std::memcpy(inline_code+12,&data,4);inline_code[0]=3;
 pc_cached=true;for(auto pair:std::vector<std::pair<const void*,int>>{{b,1},{o,2},{i,3}}){method.entry_point=reinterpret_cast<uint64_t>(pair.first);assert(DiagnosticJitCodeKind(&env,object)==pair.second);assert(DiagnosticJitInlineState(&env,object)==(pair.second==3?3:2));}
 oat=i;invocation=i;assert(DiagnosticAotMethodState(&env,object)==(1|2|4|128));instrumentation.level=2;assert(DiagnosticAotMethodState(&env,object)==(1|2|4|64|128));oat=nullptr;invocation=nullptr;bridge_entry=i;nterp_entry=i;assert(DiagnosticAotMethodState(&env,object)==(4|8|16|64));
 entries={{b,&method},{o,&other},{i,&method}};assert(DiagnosticJitCacheDetail(&env,object)==31&&lock_count==1&&visits==1&&!locked);entries={{o,&other}};assert(DiagnosticJitCacheDetail(&env,object)==0&&lock_count==2);
 assert(DiagnosticInvalidateAllJit(&env)==1&&invalidations==1);access_supported=false;assert(DiagnosticJitMethodState(&env,object)==-1&&DiagnosticAotMethodState(&env,object)==-1&&DiagnosticJitCacheDetail(&env,object)==-1);assert(scope_count==0);
 puts("device diagnostics: CheckJNI restore, owned counts, AOT bits, JIT kinds, pinned function callback/cache aggregation+lock, invalidation and unsupported access passed");}
