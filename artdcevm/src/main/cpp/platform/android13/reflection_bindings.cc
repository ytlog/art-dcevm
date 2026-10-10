#include "platform/android13/reflection_bindings.h"
#include "platform/abi/arm64/android13/art_method_abi.h"
#include "platform/android13/native_object_access.h"
#include "platform/android13/runtime_access.h"
#include "platform/android13/symbols/profile.h"
#include <cstring>
#include <dlfcn.h>
#include <jvmti.h>
#include <vector>

namespace artdcevm::android13 {
namespace {
struct Binding { const char* klass; std::string method, signature, symbol; };
std::vector<Binding> Targets() {
  std::vector<Binding> result;
  struct Type { const char* name; const char* code; };
  constexpr Type types[]{{"Boolean","Z"},{"Byte","B"},{"Char","C"},{"Short","S"},
      {"Int","I"},{"Long","J"},{"Float","F"},{"Double","D"},{"Object","Ljava/lang/Object;"}};
  constexpr const char* mangled_codes[]{"h","a","t","s","i","l","f","d",""};
  for (size_t i=0;i<9;++i) for (bool setter : {false,true}) {
    const bool object=i==8;
    result.push_back({"java/lang/reflect/Field",
        std::string(setter?"set":"get")+(object?"":types[i].name),
        std::string("(Ljava/lang/Object;")+(setter?types[i].code:"")+
            ")"+(setter?"V":types[i].code),
        cas::ReflectionFieldSymbol(setter,types[i].name,mangled_codes[i],object)});
  }
  result.push_back({"java/lang/reflect/Constructor","newInstance0",
      "([Ljava/lang/Object;)Ljava/lang/Object;",cas::kRetiredMethodHooks[20]});
  return result;
}
bool Fail(std::string* error,const std::string& message) {
  if(error)*error=message;
  return false;
}
struct Frame {
  JNIEnv* env;
  bool pushed;
  explicit Frame(JNIEnv* value):env(value),pushed(value->PushLocalFrame(24)==JNI_OK) {}
  ~Frame(){if(pushed)env->PopLocalFrame(nullptr);}
};
void* Entry(JNIEnv* env,jvmtiEnv* ti,jmethodID id,bool allow_static=false) {
  jint flags=0;
  if(ti->GetMethodModifiers(id,&flags)!=JVMTI_ERROR_NONE || (flags&0x100)==0 ||
      (!allow_static && (flags&0x8)!=0)) return nullptr;
  NativeObjectAccess access(env);
  if(!access.Supported()) return nullptr;
  void* method=DecodeMethodId(id);
  if(!method) return nullptr;
  uint32_t actual=0;
  uint64_t entry=0;
  actual=art_method_abi::ReadAccessFlags(method);
  if((actual&0xffffu)!=static_cast<uint32_t>(flags))return nullptr;
  // Fixed art_method.h GetEntryPointFromJniPtrSize() uses data_ for native methods.
  std::memcpy(&entry,static_cast<char*>(method)+offsetof(art_method_abi::ArtMethod,data),sizeof(entry));
  return reinterpret_cast<void*>(entry);
}
jmethodID FindDeclaredMethod(jvmtiEnv* ti,jclass klass,const Binding& binding) {
  jint count=0;
  jmethodID* methods=nullptr;
  if(ti->GetClassMethods(klass,&count,&methods)!=JVMTI_ERROR_NONE)return nullptr;
  jmethodID result=nullptr;
  for(jint i=0;i<count;++i) {
    char* name=nullptr;
    char* signature=nullptr;
    if(ti->GetMethodName(methods[i],&name,&signature,nullptr)==JVMTI_ERROR_NONE &&
        name && signature && binding.method==name && binding.signature==signature)result=methods[i];
    if(name)ti->Deallocate(reinterpret_cast<unsigned char*>(name));
    if(signature)ti->Deallocate(reinterpret_cast<unsigned char*>(signature));
    if(result)break;
  }
  if(methods)ti->Deallocate(reinterpret_cast<unsigned char*>(methods));
  return result;
}
bool Calibrate(JNIEnv* env,jvmtiEnv* ti,HookEngine& hooks) {
  jvmtiFrameInfo frames[16]{};
  jint count=0;
  if(ti->GetStackTrace(nullptr,0,16,frames,&count)!=JVMTI_ERROR_NONE)return false;
  for(jint i=0;i<count;++i) {
    jclass klass=nullptr;
    char* signature=nullptr;
    char* name=nullptr;
    char* descriptor=nullptr;
    const bool identity=frames[i].location==-1 &&
        ti->GetMethodDeclaringClass(frames[i].method,&klass)==JVMTI_ERROR_NONE &&
        ti->GetClassSignature(klass,&signature,nullptr)==JVMTI_ERROR_NONE &&
        ti->GetMethodName(frames[i].method,&name,&descriptor,nullptr)==JVMTI_ERROR_NONE;
    const char* symbol=nullptr;
    if(identity && signature && name && descriptor && std::strcmp(descriptor,"()I")==0) {
      if(std::strcmp(signature,"Lcom/github/ytlog/artdcevm/api/DcevmRuntime;")==0 &&
          std::strcmp(name,"nativeInitializeEvolution")==0)
        symbol="Java_com_github_ytlog_artdcevm_api_DcevmRuntime_nativeInitializeEvolution";
      if(std::strcmp(signature,"Lcom/github/ytlog/artdcevm/testing/NativeBridge;")==0 &&
          std::strcmp(name,"initializeEvolution")==0)
        symbol="Java_com_github_ytlog_artdcevm_testing_NativeBridge_initializeEvolution";
    }
    void* expected=symbol?hooks.FindSymbol("libartdcevm.so",symbol):nullptr;
    bool matches=expected && Entry(env,ti,frames[i].method,true)==expected;
    if(klass)env->DeleteLocalRef(klass);
    for(char* value : {signature,name,descriptor}) if(value)ti->Deallocate(reinterpret_cast<unsigned char*>(value));
    if(matches)return true;
  }
  return false;
}
}
bool ReflectionBindings::Configure(JNIEnv* env,const DeviceProfile& profile,std::string* error) {
  bindings_.clear();
  if(!env || !profile.Matched())return Fail(error,"Reflection bindings require calibrated device identity and JNI");
  std::vector<Binding> missing;
  for(const auto& target:Targets())
    if(force_native_ || !delegate_.FindSymbol("libart.so",target.symbol.c_str()))missing.push_back(target);
  if(missing.empty())return true;
  JavaVM* vm=nullptr;
  jvmtiEnv* ti=nullptr;
  if(env->GetJavaVM(&vm)!=JNI_OK || vm->GetEnv(reinterpret_cast<void**>(&ti),JVMTI_VERSION_1_2)!=JNI_OK)
    return Fail(error,"Reflection bindings require attached JVMTI");
  Frame frame(env);
  if(!frame.pushed || !Calibrate(env,ti,delegate_))
    return Fail(error,"Owned native ArtMethod binding differs from its named SDK ELF entry");
  using IsLookupStub=bool(*)(const void*,const void*);
  auto lookup=reinterpret_cast<IsLookupStub>(delegate_.FindSymbol("libart.so",
      "_ZNK3art11ClassLinker20IsJniDlsymLookupStubEPKv"));
  auto critical_lookup=reinterpret_cast<IsLookupStub>(delegate_.FindSymbol("libart.so",
      "_ZNK3art11ClassLinker28IsJniDlsymLookupCriticalStubEPKv"));
  if(!lookup || !critical_lookup)return Fail(error,"Missing named native binding resolution checks");
  std::unordered_map<std::string,void*> candidate;
  for(const auto& target:missing) {
    auto klass=env->FindClass(target.klass);
    // JVMTI identifies declared methods without changing hidden API policy.
    auto id=klass?FindDeclaredMethod(ti,klass,target):nullptr;
    if(!id || env->ExceptionCheck())
      return Fail(error,"Missing declared native reflection method: "+target.method+target.signature);
    void* entry=id&&!env->ExceptionCheck()?Entry(env,ti,id):nullptr;
    Dl_info owner{};
    // Match the loaded ART identity, not merely a function pointer-shaped value.
    if(!entry || lookup(RuntimeClassLinker(),entry) || critical_lookup(RuntimeClassLinker(),entry) ||
        !dladdr(entry,&owner) || !owner.dli_fname ||
        profile.Modules()[0].path!=owner.dli_fname)
      return Fail(error,"Native reflection binding has wrong ART owner: "+target.method+target.signature);
    void* named=delegate_.FindSymbol("libart.so",target.symbol.c_str());
    if(named && named!=entry)return Fail(error,"Named ELF and native reflection binding disagree: "+target.method);
    candidate.emplace(target.symbol,entry);
    env->DeleteLocalRef(klass);
  }
  bindings_=std::move(candidate);
  return true;
}
void* ReflectionBindings::FindSymbol(const char* library,const char* symbol) {
  const bool art=std::strcmp(library,"libart.so")==0;
  auto found=art?bindings_.find(symbol):bindings_.end();
  if(force_native_ && found!=bindings_.end())return found->second;
  if(void* named=delegate_.FindSymbol(library,symbol))return named;
  return found!=bindings_.end()?found->second:nullptr;
}
}
