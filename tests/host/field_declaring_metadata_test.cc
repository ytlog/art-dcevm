#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_commit_fixture.inc"
#pragma clang diagnostic pop
#include <iostream>
namespace {
bool bad_owner=false,bad_cookie=false,raised=false,pending=false;unsigned named_calls=0;
abi::arm64::android11::ReleaseObjectPointer OriginalOwner(void* field){
 ++named_calls;if(raised)pending=true;
 auto bits=uintptr_t(bad_owner?new_class:Root(field));if(bad_cookie)bits|=uintptr_t{1}<<48;
 return {bits};
}
jboolean Pending(JNIEnv*){Jni();return pending;}
class NamedHooks final:public Hooks {
 public:bool absent=false;
 void* FindSymbol(const char* lib,const char* name)override{
  if(!std::strcmp(name,field_declaring_class_source::Source::kOriginal))return absent?nullptr:reinterpret_cast<void*>(OriginalOwner);
  return Hooks::FindSymbol(lib,name);
 }
};
struct Request {modern_field_metadata::FieldMetadata* metadata;std::array<modern_field_metadata::FieldOracle,4>* oracle;std::string* error;};
bool Run(const runnable_jni::Context& c,void* p){auto& r=*static_cast<Request*>(p);return r.metadata->Calibrate(c,reinterpret_cast<jobject>(old_class),"LFixture;",r.oracle->data(),4,r.error);}
}
int main(){
 InitJni();InitializeHeap();table.ExceptionCheck=Pending;std::string error;
 std::array<modern_field_metadata::FieldOracle,4> oracle{};
 for(size_t i=0;i<4;++i)oracle[i]={reinterpret_cast<jobject>(&mirrors[i]),names[i],types[i],i==0?8u:1u};
 auto calibrate=[&](modern_field_metadata::FieldMetadata& metadata){Request request{&metadata,&oracle,&error};return runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),Run,&request)==runnable_jni::Result::kSuccess;};
 NamedHooks hooks;modern_field_metadata::FieldMetadata good;assert(good.Bind(hooks,&error));assert(calibrate(good));
 assert(named_calls>=4&&good.FieldDeclaringClass(old_fields[0])==old_class);
 field_copy::CopyFieldView view{};assert(good.FieldIdentity(old_fields[1],&view,&error));assert(view.name==names[1]&&view.descriptor==types[1]);
 assert(good.MethodDeclaringClass(old_method)==old_class);
 old_fields[1]->declaring_class=uint32_t(uintptr_t(new_class));assert(good.FieldDeclaringClass(old_fields[1])==new_class);old_fields[1]->declaring_class=uint32_t(uintptr_t(old_class));
 bad_owner=true;assert(!calibrate(good)&&!good.Calibrated());
 assert(!good.FieldIdentity(old_fields[1],&view,&error));
 bad_owner=false;assert(calibrate(good)&&good.Calibrated());
 raised=true;assert(!calibrate(good)&&pending&&!good.Calibrated());
 assert(!good.FieldIdentity(old_fields[1],&view,&error));raised=pending=false;
 assert(calibrate(good));pending=true;assert(!calibrate(good)&&!good.Calibrated());pending=false;
 assert(calibrate(good));
 bad_owner=true;modern_field_metadata::FieldMetadata foreign;assert(foreign.Bind(hooks,&error));assert(!calibrate(foreign)&&!foreign.Calibrated());assert(error.find("exact public owner")!=std::string::npos);bad_owner=false;
 bad_cookie=true;modern_field_metadata::FieldMetadata cookie;assert(cookie.Bind(hooks,&error));assert(!calibrate(cookie)&&!cookie.Calibrated());assert(error.find("ObjPtr codec")!=std::string::npos);bad_cookie=false;
 raised=true;modern_field_metadata::FieldMetadata exception;assert(exception.Bind(hooks,&error));auto before=named_calls;assert(!calibrate(exception)&&pending&&!exception.Calibrated());assert(named_calls==before+1&&error.find("pending exception")!=std::string::npos);raised=pending=false;
 auto saved=oracle[1];oracle[1]=oracle[0];modern_field_metadata::FieldMetadata duplicate;assert(duplicate.Bind(hooks,&error));assert(!calibrate(duplicate)&&error.find("duplicated")!=std::string::npos);oracle[1]=saved;
 oracle[1].modifiers^=8;modern_field_metadata::FieldMetadata flags;assert(flags.Bind(hooks,&error));assert(!calibrate(flags)&&error.find("modifiers/static")!=std::string::npos);oracle[1]=saved;
 hooks.absent=true;modern_field_metadata::FieldMetadata generic;assert(generic.Bind(hooks,&error));before=named_calls;assert(calibrate(generic)&&named_calls==before);assert(generic.FieldIdentity(old_fields[1],&view,&error));
 oracle[1].modifiers^=8;assert(!calibrate(generic)&&!generic.Calibrated());
 assert(!generic.FieldIdentity(old_fields[1],&view,&error));oracle[1]=saved;
 assert(calibrate(generic)&&generic.Calibrated());
 std::cout<<"actual FieldMetadata original declaring getter calibration/Text/current-root, method generic independence, exact owner/cookie/duplicate/static/pending negatives PASS\n";
}
