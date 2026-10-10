#pragma once
#include "platform/common/runnable_jni_bridge.h"
namespace artdcevm::readonly_field_scope {
using Oracle=bool(*)(const runnable_jni::Context&,jobject,void**);
using Current=void*(*)();
using Decode=void*(*)(void*,void*,jobject);
struct Authorization {
 void* owner;JNIEnv* env;void* vm;Current current;Oracle oracle;
 const runnable_jni::Context* context;
};
inline thread_local const Authorization* active=nullptr;
// Called only from the actual executor callback. The independent oracle must
// prove its live strong anchor before granting a read lifetime; it remains
// consulted for every other root. No transaction permission is published.
inline bool Run(void* owner,JNIEnv* env,void* vm,Current current,Oracle oracle,
 const runnable_jni::Context& context,jobject anchor,runnable_jni::Operation operation,void* data){
 if(!owner||!env||!vm||!current||!oracle||!anchor||!operation||
    context.env!=env||context.vm!=vm||!context.self||current()!=context.self)return false;
 void* rooted=nullptr;if(!oracle(context,anchor,&rooted)||!rooted||current()!=context.self)return false;
 const Authorization authorization{owner,env,vm,current,oracle,&context};
 struct Reset{const Authorization* previous;~Reset(){active=previous;}}reset{active};
 active=&authorization;const bool ok=operation(context,data);
 return ok&&context.env==env&&context.vm==vm&&current()==context.self;
}
inline bool Read(void* owner,void* vm,Current current,Decode decode,jobject root,void** out){
 const auto* a=active;
 if(!a||a->owner!=owner||a->vm!=vm||a->current!=current||!current||!decode||!root||!out||
    !a->context||a->context->env!=a->env||a->context->vm!=vm||current()!=a->context->self)return false;
 void* oracle=nullptr;
 if(!a->oracle(*a->context,root,&oracle)||!oracle||current()!=a->context->self)return false;
 void* actual=decode(owner,a->context->self,root);
 if(!actual||actual!=oracle||current()!=a->context->self)return false;
 *out=actual;return true;
}
inline bool Matches(void* owner,const runnable_jni::Context& context){
 const auto* a=active;return a&&a->owner==owner&&a->context==&context&&
   context.env==a->env&&context.vm==a->vm&&a->current&&a->current()==context.self;
}
}
