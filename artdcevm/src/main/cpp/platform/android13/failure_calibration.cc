#include "platform/android13/failure_calibration.h"
#include "platform/abi/arm64/android13/failure_calibration_abi.h"
#include "platform/abi/arm64/android13/redefinition_art_abi.h"
#include "platform/android13/redefinition_failure.h"
#include "platform/abi/arm64/android13/platform_string_abi.h"
#include <cstring>
#include <jvmti.h>
namespace artdcevm::android13 {
namespace {
using Storage=failure_calibration_abi::PlatformString;
struct ErrorOwner {
 Storage text;
 void(*destroy)(void*);
 explicit ErrorOwner(void(*d)(void*)):destroy(d){}
 ~ErrorOwner(){destroy(&text);}
 ErrorOwner(const ErrorOwner&)=delete;
 ErrorOwner& operator=(const ErrorOwner&)=delete;
};
void Signature(unsigned char* output,const char* text,bool inline_string){
 size_t length=std::strlen(text);
 if(inline_string){output[0]=static_cast<unsigned char>(length<<1);std::memcpy(output+1,text,length+1);}
 else {BorrowedPlatformString borrowed(text);std::memcpy(output,&borrowed,sizeof(borrowed));}
}
bool Equal(const Storage& string,const std::string& expected){
 const char* text;size_t size;
 if(string.bytes[0]&1){std::memcpy(&size,string.bytes+8,8);std::memcpy(&text,string.bytes+16,8);}
 else {size=string.bytes[0]>>1;text=reinterpret_cast<const char*>(string.bytes+1);if(size>22)return false;}
 return text&&size==expected.size()&&std::memcmp(text,expected.data(),size)==0&&text[size]=='\0';
}
}
bool CalibrateRedefinitionFailures(HookEngine& hooks,const DeviceProfile& profile,std::string* error){
 if(!profile.Matched()||!profile.DeclarationId()||std::strcmp(profile.DeclarationId(),failure_calibration_abi::kDeclarationId)){
  *error="Redefinition failure calibration requires the matched locked device profile";return false;
 }
 auto destroy=reinterpret_cast<void(*)(void*)>(hooks.FindSymbol("libc++.so",failure_calibration_abi::kStringDestroy));
 if(!destroy){*error="Missing named platform error-string destructor";return false;}
 if(!ConfigureRedefinitionFailures(hooks,error))return false;
 // Input representations include platform SSO, an empty long borrowed view,
 // and long class signatures. Every message crosses only as a const borrow.
 for(const char* signature:{"", "LP;", "Lartdcevm/NamedFailureLayoutCalibration;"}){
  for(bool inline_string:{true,false}){
   if(inline_string&&std::strlen(signature)>22)continue;
   ErrorOwner output(destroy);failure_calibration_abi::Driver driver;driver.error=&output.text;
   redefinition_abi::ClassRedefinition definition{};definition.driver=&driver;
   Signature(definition.class_signature,signature,inline_string);
   // Reuse the same platform error string across assignments to validate the
   // device's ownership of both the initial and previous long allocation.
   for(const char* message:{"", "short", "Long error text exceeding the platform small-string buffer"}){
    RecordRedefinitionFailure(&definition,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED,message);
    const std::string expected=std::string("Unable to perform redefinition of '")+signature+"': "+message;
    if(driver.result!=JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED||!Equal(output.text,expected)){
     *error="Named redefinition failure recorder did not match result or platform string layout";return false;
    }
   }
  }
 }
 return true;
}
}
