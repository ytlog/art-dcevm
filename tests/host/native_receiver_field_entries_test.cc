#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "native_receiver_field_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_receiver_frame.h"
#include <array>
using namespace artdcevm::receiver_guard;
namespace {
struct Class{Class*parent=nullptr;};struct Receiver{Class*klass;};Class base,before{&base},after;Receiver receiver{&before};const char*exception=nullptr;unsigned bodies=0;int written=0;
static bool Thread(void*,void*self)noexcept{return self==&current_thread;}
static bool Member(void*,void*m,bool field,uint32_t*flags,void**owner)noexcept{if(!field||m!=&field_a)return false;*flags=field_a.access_flags;*owner=&base;return true;}
static bool ObjectClass(void*,void*object,void**klass)noexcept{if(object!=&receiver&&object!=reinterpret_cast<void*>(0x4000))return false;*klass=receiver.klass;return true;}
static bool Normalize(void*,void*,void*old,void*,void**current)noexcept{*current=old;return true;}
static bool Assignable(void*,void*d,void*r,bool*out)noexcept{*out=false;for(auto*k=static_cast<Class*>(r);k;k=k->parent)if(k==d)*out=true;return true;}
static bool Reference(void*,void*,void*object,void**out)noexcept{*out=object;return object;}
static void Raise(void*,void*,const char*type,const char*)noexcept{exception=type;}
static bool FieldId(void*,void*,void*id,void**out)noexcept{if(id!=instance.id)return false;*out=&field_a;return true;}
std::array<unsigned char,96> frame{};
struct Operands:InstructionOperands{Operands():InstructionOperands(1){}uint32_t GetOperand(size_t i)const override{assert(!i);return 0;}}operands;
static bool FrameExtent(void*,void*,const void*p,size_t*out)noexcept{if(p==frame.data())*out=frame.size();else if(p==&operands)*out=sizeof(operands);else return false;return true;}
static bool FrameReference(void*,void*,uint32_t value,void**out)noexcept{*out=reinterpret_cast<void*>(uintptr_t(value));return value==0x4000;}
static bool VarBody(void*,int,void*f,const void*args,void*result){assert(f==frame.data()&&args==&operands&&in_hot);++bodies;*static_cast<uint64_t*>(result)=99;return true;}
static jint Read(JNIEnv*,jobject,jfieldID){++bodies;return 99;}
static void Write(JNIEnv*,jobject,jfieldID,jint value){++bodies;written=value;}
static jint ReflectRead(JNIEnv*,jobject,jobject){++bodies;return 99;}
static void ReflectWrite(JNIEnv*,jobject,jobject,jint value){++bodies;written=value;}
}
int main(){using namespace retirement;table.ExceptionCheck=Exception;table.FindClass=Find;table.NewGlobalRef=NewRef;table.DeleteGlobalRef=DeleteRoot;env.functions=&table;
 instance.target=&field_a;instance.id=reinterpret_cast<jfieldID>(&field_a);field_a.access_flags=1;state.registry=&registry;state.anchor=Obj(&owner_a);state.throw_exception=ThrowRetired;original_field=reinterpret_cast<void*>(NativeField);
 JniField<0,jint,false>::get=reinterpret_cast<void*>(Read);JniField<0,jint,false>::set=reinterpret_cast<void*>(Write);ReflectionField<jint>::get=reinterpret_cast<void*>(ReflectRead);ReflectionField<jint>::set=reinterpret_cast<void*>(ReflectWrite);
 int caller;void*method=&caller;uint32_t count=2,compressed=0x4000;std::memcpy(frame.data()+8,&method,8);std::memcpy(frame.data()+48,&count,4);std::memcpy(frame.data()+64+count*4,&compressed,4);FrameReader frames({nullptr,Thread,FrameExtent,FrameReference});assert(frames.Observe(frame.data(),&operands,{&current_thread,method,reinterpret_cast<void*>(0x4000),0})&&frames.Seal());
 receiver_guard::Guard guard({nullptr,Thread,Member,ObjectClass,Normalize,Assignable,nullptr,Reference,nullptr,Raise,FieldId,&frames,nullptr});assert(Bind(guard));
 for(auto path:{Path::JniRead,Path::JniWrite,Path::ReflectRead,Path::ReflectWrite})for(bool refuse:{false,true}){
  receiver.klass=refuse?&after:&before;exception=nullptr;const unsigned count=bodies;written=0;Invocation call{path,&current_thread,&field_a,&receiver,nullptr,nullptr,true};assert(guard.BeginProof({call,&base,receiver.klass,refuse}));
  jint result=0;if(path==Path::JniRead)result=JniField<0,jint,false>::Get(&env,Obj(&receiver),instance.id);
  else if(path==Path::JniWrite)JniField<0,jint,false>::Set(&env,Obj(&receiver),instance.id,99);
  else if(path==Path::ReflectRead)result=ReflectionField<jint>::Get(&env,Obj(&instance),Obj(&receiver));
  else ReflectionField<jint>::Set(&env,Obj(&instance),Obj(&receiver),99);
  assert(guard.EndProof(!refuse,refuse,true));assert(bodies==count+(refuse?0:1));if(path==Path::JniRead||path==Path::ReflectRead)assert(result==(refuse?0:99));else assert(written==(refuse?0:99));if(refuse)assert(exception&&!std::strcmp(exception,"Ljava/lang/IncompatibleClassChangeError;"));
 }
 // Actual FieldVarHandle Access owns the same target reader and source Class
 // classification. Each mode reaches the receiver consumer before original.
 Hooks hooks;std::string error;assert(vars.metadata.Bind(hooks,&error));first.prefix.klass=0x1000;second.prefix.klass=0x2000;first.prefix.field=int64_t(uintptr_t(&field_a));second.prefix.field=int64_t(uintptr_t(&field_b));vars.reader=std::make_unique<var_handle_field::Reader>();assert(vars.reader->ObserveOracle(&first,&field_a,false));assert(vars.reader->ObserveOracle(&second,&field_b,true));assert(vars.reader->Seal());vars.current=Current;vars.registry=&registry;vars.decode_class=DecodeClass;vars.instance_class=Obj(&base_class);vars.static_class=Obj(&static_handle_class);installed=true;original_var_access=reinterpret_cast<void*>(VarBody);
 for(int mode=0;mode<=30;++mode)for(bool refuse:{false,true}){receiver.klass=refuse?&after:&before;exception=nullptr;uint64_t result=123;const auto count_bodies=bodies;Invocation call{Path::VarHandle,&current_thread,&field_a,reinterpret_cast<void*>(0x4000),nullptr,&result,true};assert(guard.BeginProof({call,&base,receiver.klass,refuse}));in_hot=true;const bool succeeded=VarAccess(&first,mode,frame.data(),&operands,&result);in_hot=false;assert(succeeded==!refuse&&guard.EndProof(succeeded,refuse,true));assert(bodies==count_bodies+(refuse?0:1)&&result==(refuse?0:99));}
 assert(roots_created==roots_deleted);assert(!guard.Proven());
 std::puts("Sole JNI/reflected field entries: owned Runnable + actual native FieldId/mirror identity, same old receiver migration, get/set/all 31 VarHandle modes pass or ICCE before original body and root refund PASS; incomplete paths never ready");
}
