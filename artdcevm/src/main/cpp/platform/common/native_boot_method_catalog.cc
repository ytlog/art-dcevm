#include "platform/common/native_boot_method_catalog.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>
namespace artdcevm::native_boot_method_catalog {
namespace {
bool Primitive(char c){return c=='Z'||c=='B'||c=='C'||c=='S'||c=='I'||c=='J'||c=='F'||c=='D';}
bool Signature(const char*s){
 if(!s||*s++!='(')return false;
 while(*s&&*s!=')'){if(!Primitive(*s++))return false;}
 if(*s++!=')')return false;
 if(*s!='V'&&!Primitive(*s))return false;
 return s[1]==0;
}
struct Identity {Codec codec;jobject reflected;jmethodID id;void* method=nullptr;};
bool Observe(const runnable_jni::Context& c,void*raw){
 auto&r=*static_cast<Identity*>(raw);
 const abi::arm64::android_common::RunnableJniScope actual{c.self,c.env,c.vm};
 void*a=r.codec.leaf(actual,r.reflected),*b=r.codec.decode(r.codec.manager,r.id);
 if(!a||a!=b)return false;r.method=a;return true;
}
template<class T>struct Memory {
 jvmtiEnv*ti;T*p;
 ~Memory(){if(p)ti->Deallocate(reinterpret_cast<unsigned char*>(p));}
};
struct Loaded {
 jvmtiEnv*ti;JNIEnv*env;jclass*p;jint count;
 ~Loaded(){if(p){for(jint i=0;i<count;++i)env->DeleteLocalRef(p[i]);ti->Deallocate(reinterpret_cast<unsigned char*>(p));}}
};
}
Body Classify(const unsigned char*bytes,size_t size,const char*descriptor)noexcept{
 if(!bytes||!size||(size&1)||!Signature(descriptor))return Body::kUnknown;
 // Deliberately closed straight-line scalar subset. All reference, field,
 // array, call, branch/payload, synchronization and opaque opcodes remain unknown.
 // A rejection is not a claim that the original method cannot be recovered.
 const char result=std::strchr(descriptor,')')[1];
 bool returned=false;
 for(size_t pc=0;pc<size/2;){
  const uint16_t unit=uint16_t(bytes[pc*2])|(uint16_t(bytes[pc*2+1])<<8);
  const unsigned op=unit&255;size_t width=0;
  if(returned)return Body::kUnknown;
  if(op==0&&unit==0)width=1;
  else if(op>=1&&op<=6)width=(op-1)%3+1;
  else if(op>=0x0e&&op<=0x10){
   const unsigned expected=result=='V'?0x0e:result=='J'||result=='D'?0x10:0x0f;
   if(op!=expected)return Body::kUnknown; width=1;returned=true;
  }
  else if(op>=0x12&&op<=0x19){constexpr size_t widths[]={1,2,3,2,2,3,5,2};width=widths[op-0x12];}
  else if(op>=0x2d&&op<=0x31)width=2;
  else if(op>=0x7b&&op<=0x8f)width=1;
  else if(op>=0x90&&op<=0xaf)width=2;
  else if(op>=0xb0&&op<=0xcf)width=1;
  else if(op>=0xd0&&op<=0xe2)width=2;
  if(!width||width>size/2-pc)return Body::kUnknown;
  pc+=width;
 }
 return returned?Body::kPrimitiveLocal:Body::kUnknown;
}
void Catalog::Clear(JNIEnv*env)noexcept{
 if(env)for(auto root:roots_)env->DeleteGlobalRef(root);
 roots_.clear();methods_.clear();complete_=false;
}
bool Catalog::Prepare(JNIEnv*env,jvmtiEnv*ti,const Codec&codec,std::string*error){
 if(!env){if(error)*error="boot-catalog-missing-JNI-environment";return false;}
 Clear(env);
 auto fail=[&](const char*why){Clear(env);if(error)*error=why;return false;};
 if(!env||!ti||!codec.manager||!codec.decode||!codec.leaf)return fail("boot-catalog-missing-actual-source-codec");
 try{
 jvmtiCapabilities caps{};if(ti->GetCapabilities(&caps)!=JVMTI_ERROR_NONE)return fail("boot-catalog-capabilities-unavailable");
 if(!caps.can_get_bytecodes){jvmtiCapabilities want{};want.can_get_bytecodes=1;if(ti->AddCapabilities(&want)!=JVMTI_ERROR_NONE||ti->GetCapabilities(&caps)!=JVMTI_ERROR_NONE||!caps.can_get_bytecodes)return fail("boot-catalog-bytecode-capability-unavailable");}
 std::unordered_map<std::string,size_t> seen;
 bool fixed=false;
 for(unsigned round=0;round<8&&!fixed;++round){
  jint count=0;jclass*classes=nullptr;
  const auto loaded=ti->GetLoadedClasses(&count,&classes);Loaded loaded_roots{ti,env,classes,count>0?count:0};
  if(loaded!=JVMTI_ERROR_NONE||count<0||(count&&!classes))return fail("boot-catalog-loaded-Class-query-failed");
  bool ok=true;size_t added=0;
  for(jint c=0;c<count;++c){
   jclass klass=classes[c];jobject loader=nullptr;
   if(ti->GetClassLoader(klass,&loader)!=JVMTI_ERROR_NONE){ok=false;break;}
   if(loader){env->DeleteLocalRef(loader);continue;}
   jint status=0;if(ti->GetClassStatus(klass,&status)!=JVMTI_ERROR_NONE){ok=false;break;}
   if(!(status&JVMTI_CLASS_STATUS_PREPARED))continue;
   char*signature=nullptr;
   const auto signature_result=ti->GetClassSignature(klass,&signature,nullptr);Memory<char> signature_memory{ti,signature};
   if(signature_result!=JVMTI_ERROR_NONE||!signature){ok=false;break;}
   const std::string name(signature);
   auto found=seen.find(name);
   if(found!=seen.end()){if(!env->IsSameObject(klass,roots_[found->second]))ok=false;if(!ok)break;continue;}
   auto root=static_cast<jclass>(env->NewGlobalRef(klass));
   if(!root){ok=false;break;}
   const size_t index=roots_.size();try{roots_.push_back(root);}catch(...){env->DeleteGlobalRef(root);throw;}seen.emplace(name,index);++added;
   jint n=0;jmethodID*ids=nullptr;
   const auto methods_result=ti->GetClassMethods(klass,&n,&ids);Memory<jmethodID> method_memory{ti,ids};
   if(methods_result!=JVMTI_ERROR_NONE||n<0||(n&&!ids)){ok=false;break;}
   for(jint m=0;m<n&&ok;++m){
    Method entry;entry.declaring_root=index;entry.declaring_signature=name;
    char*desc=nullptr;char*method_name=nullptr;
    const auto modifiers_result=ti->GetMethodModifiers(ids[m],&entry.modifiers);
    const auto name_result=ti->GetMethodName(ids[m],&method_name,&desc,nullptr);
    Memory<char> desc_memory{ti,desc},name_memory{ti,method_name};
    if(modifiers_result!=JVMTI_ERROR_NONE||name_result!=JVMTI_ERROR_NONE||!desc||!method_name){ok=false;break;}
    entry.descriptor=desc;entry.name=method_name;
    jclass declaring=nullptr;
    if(ti->GetMethodDeclaringClass(ids[m],&declaring)!=JVMTI_ERROR_NONE||!declaring){ok=false;break;}
    const bool same=env->IsSameObject(declaring,klass);env->DeleteLocalRef(declaring);
    if(!same)continue; // Inherited/copied methods require their own declaring root.
    jobject reflected=env->ToReflectedMethod(klass,ids[m],(entry.modifiers&8)!=0);
    jobject strong=reflected?env->NewGlobalRef(reflected):nullptr;
    if(reflected)env->DeleteLocalRef(reflected);
    if(!strong||env->ExceptionCheck()){if(strong)env->DeleteGlobalRef(strong);ok=false;break;}
    Identity request{codec,strong,ids[m],nullptr};
    const auto observed=runnable_executor::With(env,strong,Observe,&request);env->DeleteGlobalRef(strong);
    if(observed!=runnable_jni::Result::kSuccess||!request.method){ok=false;break;}
    entry.native_method=request.method;
    jint size=0;unsigned char*bytes=nullptr;
    const auto result=ti->GetBytecodes(ids[m],&size,&bytes);Memory<unsigned char> byte_memory{ti,bytes};
    if(result==JVMTI_ERROR_NONE&&size>=0){
     if(size&& !bytes){ok=false;}else if(size)entry.dex.assign(bytes,bytes+size);
     if(ti->GetMaxLocals(ids[m],&entry.max_locals)!=JVMTI_ERROR_NONE)entry.max_locals=0;
     entry.body=Classify(entry.dex.data(),entry.dex.size(),entry.descriptor.c_str());
    }else if(result!=JVMTI_ERROR_NATIVE_METHOD&&result!=JVMTI_ERROR_ABSENT_INFORMATION)ok=false;
    if(ok)methods_.push_back(std::move(entry));
   }
   if(!ok)break;
  }
  if(!ok||env->ExceptionCheck())return fail("boot-catalog-public-or-independent-native-identity-query-failed");
  fixed=added==0;
 }
 if(!fixed)return fail("boot-catalog-loaded-Class-fixed-point-not-reached");
 std::sort(methods_.begin(),methods_.end(),[](const Method&a,const Method&b){return reinterpret_cast<uintptr_t>(a.native_method)<reinterpret_cast<uintptr_t>(b.native_method);});
 for(size_t i=1;i<methods_.size();++i)if(methods_[i-1].native_method==methods_[i].native_method)return fail("boot-catalog-ambiguous-native-method-owner");
 complete_=true;return true;
 }catch(...){return fail("boot-catalog-native-storage-preparation-failed");}
}
}
