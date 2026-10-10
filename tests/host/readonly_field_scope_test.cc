#include "platform/common/readonly_field_scope.h"
#include <cassert>
#include <stdexcept>
#include <thread>
#include <cstdio>
using namespace artdcevm;
namespace {
void* self=reinterpret_cast<void*>(0x110001000ULL);void* owner=reinterpret_cast<void*>(0x220002000ULL);void* vm=reinterpret_cast<void*>(0x330003000ULL);
JNIEnv* env=reinterpret_cast<JNIEnv*>(0x440004000ULL);jobject root=reinterpret_cast<jobject>(0x550005000ULL);
void* native=reinterpret_cast<void*>(0x660006000ULL);bool executing=true,bad_decode=false;
void* Current(){return self;}
bool Oracle(const runnable_jni::Context& c,jobject r,void** out){if(!executing||c.self!=self||c.vm!=vm||c.env!=env||r!=root)return false;*out=native;return true;}
void* Decode(void* o,void* t,jobject r){assert(o==owner&&t==self&&r==root);return bad_decode?nullptr:native;}
bool Read(const runnable_jni::Context& c,void*){
 assert(readonly_field_scope::Matches(owner,c));void* out=nullptr;
 assert(readonly_field_scope::Read(owner,vm,Current,Decode,root,&out)&&out==native);
 out=reinterpret_cast<void*>(7);bad_decode=true;assert(!readonly_field_scope::Read(owner,vm,Current,Decode,root,&out)&&out==reinterpret_cast<void*>(7));bad_decode=false;
 assert(!readonly_field_scope::Read(reinterpret_cast<void*>(8),vm,Current,Decode,root,&out));
 assert(!readonly_field_scope::Read(owner,reinterpret_cast<void*>(9),Current,Decode,root,&out));
 std::thread other([]{void* out=nullptr;assert(!readonly_field_scope::Read(owner,vm,Current,Decode,root,&out));});other.join();
 return true;
}
bool Nested(const runnable_jni::Context& c,void*){
 const auto* outer=readonly_field_scope::active;
 assert(readonly_field_scope::Run(owner,env,vm,Current,Oracle,c,root,Read,nullptr));
 assert(readonly_field_scope::active==outer&&readonly_field_scope::Matches(owner,c));return true;
}
bool Throws(const runnable_jni::Context&,void*){throw std::runtime_error("original failure");}
bool Changed(const runnable_jni::Context&,void*){self=reinterpret_cast<void*>(10);return true;}
}
int main(){runnable_jni::Context context{self,env,vm};void* out=reinterpret_cast<void*>(7);
 assert(!readonly_field_scope::Read(owner,vm,Current,Decode,root,&out)&&out==reinterpret_cast<void*>(7));
 assert(readonly_field_scope::Run(owner,env,vm,Current,Oracle,context,root,Read,nullptr));assert(!readonly_field_scope::active);
 assert(readonly_field_scope::Run(owner,env,vm,Current,Oracle,context,root,Nested,nullptr));assert(!readonly_field_scope::active);
 executing=false;assert(!readonly_field_scope::Run(owner,env,vm,Current,Oracle,context,root,Read,nullptr));executing=true;
 auto wrong=context;wrong.vm=reinterpret_cast<void*>(9);assert(!readonly_field_scope::Run(owner,env,vm,Current,Oracle,wrong,root,Read,nullptr));
 wrong=context;wrong.env=reinterpret_cast<JNIEnv*>(9);assert(!readonly_field_scope::Run(owner,env,vm,Current,Oracle,wrong,root,Read,nullptr));
 try{readonly_field_scope::Run(owner,env,vm,Current,Oracle,context,root,Throws,nullptr);assert(false);}catch(const std::runtime_error&){}assert(!readonly_field_scope::active);
 assert(!readonly_field_scope::Run(owner,env,vm,Current,Oracle,context,root,Changed,nullptr));self=context.self;assert(!readonly_field_scope::active);
 std::puts("Readonly Field scope: independent roots, high pointers, owner/VM/env/thread refusal, executor absence, TLS cleanup and postcallback context proof PASS");
}
