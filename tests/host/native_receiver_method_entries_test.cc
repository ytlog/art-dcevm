#include "platform/common/modern_method_guards.cc"
#include <cassert>
#include <cstdio>
using namespace artdcevm;
using namespace artdcevm::receiver_guard;
struct Class {Class* parent;};struct Object{Class* klass;};struct Member{uint32_t flags;Class* owner;};
struct Context{void*self;Object*receiver;const char*exception=nullptr;unsigned originals=0;};Context context;
static bool Current(void*p,void*t)noexcept{return static_cast<Context*>(p)->self==t;}
static bool Metadata(void*,void*m,bool,uint32_t*f,void**c)noexcept{auto&member=*static_cast<Member*>(m);*f=member.flags;*c=member.owner;return true;}
static bool Klass(void*,void*r,void**c)noexcept{*c=static_cast<Object*>(r)->klass;return true;}
static bool Normalize(void*,void*,void*d,void*,void**c)noexcept{*c=d;return true;}
static bool Related(void*,void*d,void*r,bool*out)noexcept{*out=false;for(auto*k=static_cast<Class*>(r);k;k=k->parent)if(k==d)*out=true;return true;}
static bool Reference(void*,void*,void*r,void**out)noexcept{*out=r;return true;}
static bool Compressed(void*p,void*,uint32_t r,void**out)noexcept{if(r!=7)return false;*out=static_cast<Context*>(p)->receiver;return true;}
static void Raise(void*p,void*,const char*e,const char*)noexcept{static_cast<Context*>(p)->exception=e;}
static void InvokeOriginal(void*,void*,uint32_t*,uint32_t,void*r,const char*){++context.originals;*static_cast<uint64_t*>(r)=99;}
static jobject ReflectOriginal(const retirement::Scope&,jobject,jobject,jobject,size_t){++context.originals;return reinterpret_cast<jobject>(uintptr_t(99));}
static uint64_t NativeOriginal(const retirement::Scope&,jobject,void*,const jvalue*){++context.originals;return 99;}
static uint64_t IdOriginal(const retirement::Scope&,jobject,jmethodID,const jvalue*){++context.originals;return 99;}
static uint64_t VarOriginal(const retirement::Scope&,jobject,jmethodID,va_list){++context.originals;return 99;}
static void* Reflected(const retirement::Scope&,jobject m){return m;}
static void* Decode(void*,jmethodID m){return m;}
uint64_t VarCall(const retirement::Scope& scope,jobject receiver,jmethodID id,...){va_list args;va_start(args,id);auto result=retirement::IdVarargs<false>(scope,receiver,id,args);va_end(args);return result;}
int main(){int thread;Class base{nullptr},before{&base},after{nullptr};Object object{&before};Member member{1,&base};context={&thread,&object};
 Guard guard({&context,Current,Metadata,Klass,Normalize,Related,nullptr,Reference,Compressed,Raise});assert(Bind(guard));
 retirement::RetiredMethodRegistry registry;retirement::state.registry=&registry;retirement::state.reflected=Reflected;retirement::state.decode=Decode;
 retirement::original_invoke=reinterpret_cast<void*>(InvokeOriginal);retirement::original_reflect=reinterpret_cast<void*>(ReflectOriginal);retirement::original_native_values=reinterpret_cast<void*>(NativeOriginal);retirement::original_id_values=reinterpret_cast<void*>(IdOriginal);retirement::original_virtual_values=reinterpret_cast<void*>(IdOriginal);retirement::original_id_varargs=reinterpret_cast<void*>(VarOriginal);retirement::original_virtual_varargs=reinterpret_cast<void*>(VarOriginal);
 retirement::Scope scope{};scope.self=&thread;uint32_t args[]{7};uint64_t result=0;
 for(auto path:{Path::NativeInvoke,Path::ReflectMethod,Path::JniMethod})for(bool refuse:{false,true}){
  object.klass=refuse?&after:&before;context.exception=nullptr;const unsigned calls=context.originals;Invocation call{path,&thread,&member,&object,nullptr,&result,false};
  assert(guard.BeginProof({call,&base,object.klass,refuse}));
  if(path==Path::NativeInvoke)retirement::Invoke(&member,&thread,args,4,&result,"V");
  else if(path==Path::ReflectMethod)result=uintptr_t(retirement::Reflect(scope,reinterpret_cast<jobject>(&member),reinterpret_cast<jobject>(&object),nullptr,0));
  else result=retirement::IdValues<false>(scope,reinterpret_cast<jobject>(&object),reinterpret_cast<jmethodID>(&member),nullptr);
  assert(guard.EndProof(!refuse,refuse,true));assert(context.originals==calls+(refuse?0:1));assert(result==(refuse?0:99));
 }
 // The same independently guarded native member also covers JNI native method,
 // virtual ID and varargs entry owners. Original exceptions/results are kept.
 for(unsigned entry=0;entry<3;++entry){object.klass=&after;Invocation call{Path::JniMethod,&thread,&member,&object,nullptr,nullptr,false};assert(guard.BeginProof({call,&base,&after,true}));
 uint64_t value=entry==0?retirement::NativeValues(scope,reinterpret_cast<jobject>(&object),&member,nullptr):entry==1?retirement::IdValues<true>(scope,reinterpret_cast<jobject>(&object),reinterpret_cast<jmethodID>(&member),nullptr):VarCall(scope,reinterpret_cast<jobject>(&object),reinterpret_cast<jmethodID>(&member));assert(value==0&&guard.EndProof(false,true,true));}
 // A matching ingress owner cannot authorize original dispatch from an
 // uncalibrated EntrySource or merely from the supplied Runnable fields.
 struct Missing:OriginalIngress::TypedBounds {unsigned calls=0;bool Enter(const abi::common::RunnableJniScope&,void*,jobject,modern_field_metadata::ReadContext*)noexcept override{++calls;return false;}void Leave()noexcept override{std::abort();}} missing;
 EntrySource unproven;ClassSource classes;OriginalIngress ingress(unproven,classes,missing);assert(!ingress.Publish());assert(ingress.Attach(guard)&&ingress.Publish());
 retirement::state.throw_exception=[](void*self,const char*type,const char*){assert(self==context.self);context.exception=type;};
 object.klass=&before;context.exception=nullptr;auto old=context.originals;
 Invocation rejected{Path::JniMethod,&thread,&member,&object};assert(guard.BeginProof({rejected,&base,&before,false}));
 assert(retirement::IdValues<false>(scope,reinterpret_cast<jobject>(&object),reinterpret_cast<jmethodID>(&member),nullptr)==0);
 assert(context.originals==old&&missing.calls==0&&context.exception&&!std::strcmp(context.exception,"Ljava/lang/IllegalStateException;"));assert(!guard.EndProof(false,false,true));
 context.exception=nullptr;rejected.path=Path::ReflectMethod;assert(guard.BeginProof({rejected,&base,&before,false}));
 assert(!retirement::Reflect(scope,reinterpret_cast<jobject>(&member),reinterpret_cast<jobject>(&object),nullptr,0)&&context.originals==old);assert(!guard.EndProof(false,false,true));
 assert(!guard.Active());
 consumer.store(nullptr,std::memory_order_release);assert(ingress.RetireForTesting());
 assert(!guard.Proven()); // Other actual source paths remain required.
 std::puts("Sole native method entry owners: Invoke/reflection/native JNI/virtual ID/varargs pass or ICCE before original body with a migrated old receiver PASS; incomplete coverage never ready");
}
