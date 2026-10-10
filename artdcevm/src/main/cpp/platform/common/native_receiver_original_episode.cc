#include "platform/common/native_receiver_original_episode.h"
namespace artdcevm::receiver_guard {
namespace {
struct Local {JNIEnv*env;jobject value=nullptr;~Local(){if(value)env->DeleteLocalRef(value);}};
bool Fail(std::string*e,const char*t){if(e)*e=t;return false;}
struct Snapshot {jlong bodies=0,sentinel=0;jint field=0;};
struct PublicState {
 JNIEnv*env;jclass fixture;jobject receiver;jfieldID field;jmethodID counters;
 bool Read(Snapshot*out){
  Local array{env,env->CallStaticObjectMethod(fixture,counters)};
  if(!array.value||env->ExceptionCheck())return false;
  auto length=env->GetArrayLength(static_cast<jlongArray>(array.value));if(env->ExceptionCheck()||length!=2)return false;
  jlong values[2]{};env->GetLongArrayRegion(static_cast<jlongArray>(array.value),0,2,values);if(env->ExceptionCheck()||values[0]<0)return false;
  auto value=env->GetIntField(receiver,field);if(env->ExceptionCheck())return false;
  *out={values[0],values[1],value};return true;
 }
};
bool IsRead(unsigned p){return p==2||p==5||p==8||p==11||p==14;}
bool IsWrite(unsigned p){return p==3||p==6||p==9||p==12;}
}
OriginalEntriesResult RunOriginalReceiverEntries(JNIEnv*env,jobject fixture,JniFixtureEntries&owner,std::string*error){
 OriginalEntriesResult result;
 if(!env||!fixture||env->ExceptionCheck()||owner.Owned()){Fail(error,"Original receiver entries require fresh ordinary JNI fixture/entry owner");return result;}
 Local type{env,env->GetObjectClass(fixture)};if(!type.value||env->ExceptionCheck()){Fail(error,"Original receiver fixture Class unavailable");return result;}
 auto receiver_id=env->GetFieldID(static_cast<jclass>(type.value),"receiver","Lcom/github/ytlog/artdcevm/internal/ReceiverGuardFixture$Receiver;");if(!receiver_id||env->ExceptionCheck()){Fail(error,"Original receiver fixture receiver declaration unavailable");return result;}
 Local receiver{env,env->GetObjectField(fixture,receiver_id)};if(!receiver.value||env->ExceptionCheck()){Fail(error,"Original receiver fixture instance unavailable");return result;}
 Local receiver_type{env,env->GetObjectClass(receiver.value)};if(!receiver_type.value||env->ExceptionCheck()){Fail(error,"Original receiver instance Class unavailable");return result;}
 Local class_type{env,env->GetObjectClass(receiver_type.value)};if(!class_type.value||env->ExceptionCheck()){Fail(error,"Original receiver Class public type unavailable");return result;}
 auto superclass_id=env->GetMethodID(static_cast<jclass>(class_type.value),"getSuperclass","()Ljava/lang/Class;");if(!superclass_id||env->ExceptionCheck()){Fail(error,"Original receiver Class superclass query unavailable");return result;}
 Local base{env,env->CallObjectMethod(receiver_type.value,superclass_id)};if(!base.value||env->ExceptionCheck()){Fail(error,"Original receiver Base Class unavailable");return result;}
 auto field=env->GetFieldID(static_cast<jclass>(base.value),"value","I");if(!field||env->ExceptionCheck()){Fail(error,"Original receiver Base retained field unavailable");return result;}
 auto write_id=env->GetFieldID(static_cast<jclass>(type.value),"writeValue","I");if(!write_id||env->ExceptionCheck()){Fail(error,"Original receiver write value declaration unavailable");return result;}
 jint write=env->GetIntField(fixture,write_id);if(env->ExceptionCheck()||write!=89){Fail(error,"Original receiver independent write fixture differs from 89");return result;}
 auto execute=env->GetMethodID(static_cast<jclass>(type.value),"execute","(I)I");if(!execute||env->ExceptionCheck()){Fail(error,"Original receiver public execute entry unavailable");return result;}
 auto counters=env->GetStaticMethodID(static_cast<jclass>(type.value),"counters","()[J");if(!counters||env->ExceptionCheck()){Fail(error,"Original receiver independent counters unavailable");return result;}
 if(!owner.Register(env,static_cast<jclass>(type.value),static_cast<jclass>(base.value),error))return result;
 const auto run=[&](){
  PublicState state{env,static_cast<jclass>(type.value),receiver.value,field,counters};
  for(unsigned path=0;path<16;++path){
   Snapshot before,after;
   if(!state.Read(&before))return Fail(error,"Original receiver public before snapshot failed");
   jint value=env->CallIntMethod(fixture,execute,jint(path));if(env->ExceptionCheck())return Fail(error,"Original receiver public entry threw");
   if(!state.Read(&after))return Fail(error,"Original receiver public after snapshot failed");
   if(after.sentinel!=before.sentinel)return Fail(error,"Original receiver sentinel changed during original entry");
   if(IsRead(path)){
    if(value!=before.field||after.field!=before.field||after.bodies!=before.bodies)return Fail(error,"Original receiver read result/field/body witness differs");
   }else if(IsWrite(path)){
    if(value!=write||after.field!=write||after.bodies!=before.bodies)return Fail(error,"Original receiver write result/field/body witness differs");
   }else{
    if(before.bodies==INT64_MAX||value!=113||after.bodies!=before.bodies+1||after.field!=before.field)return Fail(error,"Original receiver invoke return/body/field witness differs");
   }
   ++result.completed;
  }
  return true;
 };
 const bool witnessed=run();
 // Never clear an exception thrown by the actual original entry. The retained
 // caller-owned registrar holds the native/Class owner until exception handling
 // permits real UnregisterNatives. Successful teardown is part of passing.
 if(env->ExceptionCheck()){if(error&&error->empty())*error="Original receiver JNI teardown awaits pending original exception handling";return result;}
 if(!owner.Unregister(env,error))return result;
 result.passed=witnessed&&result.completed==16;return result;
}
}
