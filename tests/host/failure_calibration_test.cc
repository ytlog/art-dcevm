#include "platform/android13/failure_calibration.h"
#include "platform/android13/field_validation.h"
#include "platform/android13/validation_hooks.h"
#include "platform/android13/redefinition_input.h"
#include "platform/common/schema_snapshot.h"
#include "platform/abi/arm64/android13/failure_calibration_abi.h"
#include "platform/abi/arm64/android13/redefinition_art_abi.h"
#include "platform/abi/arm64/android13/platform_string_abi.h"
#include "platform/android13/symbols/profile.h"
#include <jvmti.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <unordered_set>
#include <vector>
using namespace artdcevm;using namespace artdcevm::android13;
namespace {
// Independent host libc++ oracle of the locked source Driver declarations.
struct OracleDriver {void* environment;jvmtiError result;void* runtime;void* self;int type;std::vector<redefinition_abi::ClassRedefinition> redefinitions;std::string* error;};
static_assert(sizeof(std::string)==24&&alignof(std::string)==8);
static_assert(sizeof(OracleDriver)==sizeof(failure_calibration_abi::Driver));
static_assert(offsetof(OracleDriver,result)==offsetof(failure_calibration_abi::Driver,result));
static_assert(offsetof(OracleDriver,error)==offsetof(failure_calibration_abi::Driver,error));
static_assert(offsetof(OracleDriver,redefinitions)==offsetof(failure_calibration_abi::Driver,redefinitions_begin));
#ifdef _LIBCPP_ABI_ALTERNATE_STRING_LAYOUT
#error Failure oracle requires the locked libc++ ABI v1
#endif
#ifndef _LIBCPP_VERSION
#error Failure calibration must use a real libc++ string implementation
#endif
std::unordered_set<void*> strings;
int calls=0,destroys=0,short_signatures=0,long_signatures=0;
bool bad_result=false,bad_text=false;
bool active=true;int input_error=0,snapshot_error=0,markers=0,final_guards=0;
std::vector<evolution::Member> old_fields,new_fields;
void Marker(void* raw){++markers;static_cast<redefinition_abi::ClassRedefinition*>(raw)->added_fields=true;}
void Record(void* raw,int result,const void* sig,const BorrowedPlatformString* message){
 auto* driver=static_cast<OracleDriver*>(raw);assert(!driver->environment&&!driver->runtime&&!driver->self&&driver->type==0);
 assert(driver->redefinitions.empty());assert(driver->result==JVMTI_ERROR_INTERNAL||driver->result==JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED);++calls;
 const auto& signature=*static_cast<const std::string*>(sig);const auto& text=*reinterpret_cast<const std::string*>(message);
 if(*static_cast<const unsigned char*>(sig)&1)++long_signatures;else ++short_signatures;
 // The host oracle constructs its platform object at first use. Subsequent
 // assignments and destruction use real libc++ allocation ownership.
 if(strings.insert(driver->error).second){const unsigned char zero[24]{};assert(!std::memcmp(driver->error,zero,24));new(driver->error)std::string;}
 *driver->error=bad_text?"incorrect":std::string("Unable to perform redefinition of '")+signature.c_str()+"': "+text.c_str();
 driver->result=bad_result?JVMTI_ERROR_INTERNAL:static_cast<jvmtiError>(result);
}
void Destroy(void* error){assert(strings.erase(error)==1);static_cast<std::string*>(error)->~basic_string();++destroys;}
class Hooks:public HookEngine{public:bool missing_destroy=false,missing_record=false,missing_marker=false;int finds=0;bool Initialize(std::string*)override{return true;}void* FindSymbol(const char* library,const char* name)override{++finds;
 if(!std::strcmp(library,"libc++.so")){assert(!std::strcmp(name,failure_calibration_abi::kStringDestroy));return missing_destroy?nullptr:reinterpret_cast<void*>(Destroy);}
 assert(!std::strcmp(library,"libopenjdkjvmti.so"));if(!std::strcmp(name,cas::kRecordNewFieldAdded))return missing_marker?nullptr:reinterpret_cast<void*>(Marker);assert(!std::strcmp(name,cas::kRecordFailure));return missing_record?nullptr:reinterpret_cast<void*>(Record);}
 void* Install(void*,void*,void**,std::string*)override{assert(false);return nullptr;}bool Remove(void*,std::string*)override{assert(false);return false;}};
const char* names[]{"libart.so","libopenjdkjvmti.so","libdexfile.so","libc++.so"};
const char* ids[]{"14fc77b24db0b351b919c97009dc86bf","2fe21c45581b9c8b0519b71cbde32938","e6aaffcef8ef63baf438267cda44d400","1f426797e505c9b841f55cc49d32b3f4"};
int mismatch=-1;
LoadedModule Find(const char* name){for(int i=0;i<4;++i)if(!std::strcmp(name,names[i]))return {i==mismatch?std::string{}:std::string("/platform/")+name,ids[i]};assert(false);return {};}
}
namespace artdcevm {LoadedModule FindLoadedModule(const char* name){return Find(name);}}
namespace artdcevm::android13 {
jvmtiEnv* ValidationHooks::TransactionEnvironment(){return active?reinterpret_cast<jvmtiEnv*>(1):nullptr;}
int ReadRedefinitionInput(void*,redefinition_abi::ClassRedefinition* metadata,evolution::ClassSchema* schema,std::string* error){schema->fields=new_fields;metadata->klass=reinterpret_cast<jclass>(1);if(input_error)*error="Malformed input";return input_error;}

}
struct DefinitionFixture {
 failure_calibration_abi::PlatformString text;
 failure_calibration_abi::Driver driver;
 redefinition_abi::ClassRedefinition definition{};
 DefinitionFixture(){driver.error=&text;definition.driver=&driver;definition.class_signature[0]=6;std::memcpy(definition.class_signature+1,"LP;",4);}
 ~DefinitionFixture(){if(strings.count(&text))Destroy(&text);}
};
int main(){Hooks hooks;DeviceProfile profile;std::string error;assert(!CalibrateRedefinitionFailures(hooks,profile,&error)&&hooks.finds==0&&calls==0);
 for(int i=0;i<4;++i){mismatch=i;assert(!MatchDeviceProfile(&profile,&error));assert(!CalibrateRedefinitionFailures(hooks,profile,&error)&&hooks.finds==0&&calls==0);}mismatch=-1;assert(MatchDeviceProfile(&profile,&error));
 hooks.missing_destroy=true;assert(!CalibrateRedefinitionFailures(hooks,profile,&error)&&calls==0);hooks.missing_destroy=false;hooks.missing_record=true;assert(!CalibrateRedefinitionFailures(hooks,profile,&error)&&calls==0);hooks.missing_record=false;
 assert(CalibrateRedefinitionFailures(hooks,profile,&error));assert(calls==15&&destroys==5&&short_signatures==6&&long_signatures==9&&strings.empty());
 bad_result=true;assert(!CalibrateRedefinitionFailures(hooks,profile,&error)&&strings.empty()&&destroys==6);bad_result=false;
 bad_text=true;assert(!CalibrateRedefinitionFailures(hooks,profile,&error)&&strings.empty()&&destroys==7);bad_text=false;
 assert(CalibrateRedefinitionFailures(hooks,profile,&error)&&strings.empty()&&destroys==12);
 DeviceProfile unmatched;int before=hooks.finds;assert(!ConfigureEvolutionFieldValidation(hooks,unmatched,&error)&&hooks.finds==before);
 hooks.missing_marker=true;assert(!ConfigureEvolutionFieldValidation(hooks,profile,&error));hooks.missing_marker=false;assert(ConfigureEvolutionFieldValidation(hooks,profile,&error));
 {DefinitionFixture f;assert(CheckEvolutionFields(&f.definition)&&!f.definition.added_fields&&!f.definition.has_virtuals);}
 new_fields={{"newValue","I",1}};
 {DefinitionFixture f;assert(CheckEvolutionFields(&f.definition)&&f.definition.added_fields&&f.definition.has_virtuals&&markers==1);}
 old_fields={{"newValue","I",1}};new_fields={{"newValue","I",0x11}};
 {DefinitionFixture f;assert(CheckEvolutionFields(&f.definition)&&final_guards==1);}
 new_fields={{"newValue","I",0x101}};
 {DefinitionFixture f;assert(!CheckEvolutionFields(&f.definition)&&f.driver.result==JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED&&!f.definition.added_fields);}
 active=false;{DefinitionFixture f;assert(!CheckEvolutionFields(&f.definition)&&f.driver.result==JVMTI_ERROR_NOT_AVAILABLE);}active=true;
 input_error=JVMTI_ERROR_ILLEGAL_ARGUMENT;{DefinitionFixture f;assert(!CheckEvolutionFields(&f.definition)&&f.driver.result==JVMTI_ERROR_ILLEGAL_ARGUMENT);}input_error=0;
 snapshot_error=JVMTI_ERROR_INTERNAL;{DefinitionFixture f;assert(!CheckEvolutionFields(&f.definition)&&f.driver.result==JVMTI_ERROR_INTERNAL);}snapshot_error=0;assert(strings.empty());
 std::puts("failure calibration: strict profile, real libc++ short/borrowed-long input, repeated owned assignment/result/destruction, rejection cleanup passed");}

namespace artdcevm::live_schema {
int ReadLiveFields(jvmtiEnv*,jclass,std::vector<evolution::Member>* fields,std::string* error){*fields=old_fields;if(snapshot_error)*error="Snapshot failed";return snapshot_error;}
}

namespace artdcevm::final_field_guard { void MarkFinalFieldGuard()noexcept{++final_guards;} }
