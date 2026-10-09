#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_receiver_field_fixture.inc"
#pragma clang diagnostic pop
using namespace artdcevm::receiver_guard;
namespace {
struct Class{Class*parent=nullptr;};struct Receiver{Class*klass;};Class base,before{&base},after;Receiver receiver{&before};const char*exception=nullptr;unsigned bodies=0;int written=0;
static bool Thread(void*,void*self)noexcept{return self==&current_thread;}
static bool Member(void*,void*m,bool field,uint32_t*flags,void**owner)noexcept{if(!field||m!=&field_a)return false;*flags=field_a.access_flags;*owner=&base;return true;}
static bool ObjectClass(void*,void*object,void**klass)noexcept{if(object!=&receiver)return false;*klass=receiver.klass;return true;}
static bool Normalize(void*,void*,void*old,void*,void**current)noexcept{*current=old;return true;}
static bool Assignable(void*,void*d,void*r,bool*out)noexcept{*out=false;for(auto*k=static_cast<Class*>(r);k;k=k->parent)if(k==d)*out=true;return true;}
static bool Reference(void*,void*,void*object,void**out)noexcept{*out=object;return object;}
static void Raise(void*,void*,const char*type,const char*)noexcept{exception=type;}
static bool FieldId(void*,void*,void*id,void**out)noexcept{if(id!=instance.id)return false;*out=&field_a;return true;}
static jint Read(JNIEnv*,jobject,jfieldID){++bodies;return 99;}
static void Write(JNIEnv*,jobject,jfieldID,jint value){++bodies;written=value;}
static jint ReflectRead(JNIEnv*,jobject,jobject){++bodies;return 99;}
static void ReflectWrite(JNIEnv*,jobject,jobject,jint value){++bodies;written=value;}
}
int main(){using namespace retirement;table.ExceptionCheck=Exception;table.FindClass=Find;table.NewGlobalRef=NewRef;table.DeleteGlobalRef=DeleteRoot;env.functions=&table;
 instance.target=&field_a;instance.id=reinterpret_cast<jfieldID>(&field_a);field_a.access_flags=1;state.registry=&registry;state.anchor=Obj(&owner_a);state.throw_exception=ThrowRetired;original_field=reinterpret_cast<void*>(NativeField);
 JniField<0,jint,false>::get=reinterpret_cast<void*>(Read);JniField<0,jint,false>::set=reinterpret_cast<void*>(Write);ReflectionField<jint>::get=reinterpret_cast<void*>(ReflectRead);ReflectionField<jint>::set=reinterpret_cast<void*>(ReflectWrite);
 receiver_guard::Guard guard({nullptr,Thread,Member,ObjectClass,Normalize,Assignable,nullptr,Reference,nullptr,Raise,FieldId});assert(Bind(guard));
 for(auto path:{Path::JniRead,Path::JniWrite,Path::ReflectRead,Path::ReflectWrite})for(bool refuse:{false,true}){
  receiver.klass=refuse?&after:&before;exception=nullptr;const unsigned count=bodies;written=0;Invocation call{path,&current_thread,&field_a,&receiver,nullptr,nullptr,true};assert(guard.BeginProof({call,&base,receiver.klass,refuse}));
  jint result=0;if(path==Path::JniRead)result=JniField<0,jint,false>::Get(&env,Obj(&receiver),instance.id);
  else if(path==Path::JniWrite)JniField<0,jint,false>::Set(&env,Obj(&receiver),instance.id,99);
  else if(path==Path::ReflectRead)result=ReflectionField<jint>::Get(&env,Obj(&instance),Obj(&receiver));
  else ReflectionField<jint>::Set(&env,Obj(&instance),Obj(&receiver),99);
  assert(guard.EndProof(!refuse,refuse,true));assert(bodies==count+(refuse?0:1));if(path==Path::JniRead||path==Path::ReflectRead)assert(result==(refuse?0:99));else assert(written==(refuse?0:99));if(refuse)assert(exception&&!std::strcmp(exception,"Ljava/lang/IncompatibleClassChangeError;"));
 }
 assert(roots_created==roots_deleted);assert(!guard.Proven());
 std::puts("Sole JNI/reflected field entries: owned Runnable + actual native FieldId/mirror identity, same old receiver migration, get/set pass or ICCE before original body and root refund PASS; incomplete paths never ready");
}
