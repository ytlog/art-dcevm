#pragma once
#include "api/hook_engine.h"
#include "platform/common/runnable_jni_bridge.h"
#include "platform/abi/arm64/android11/jni_object_decode.h"
#include <mutex>
#include <unordered_map>
namespace artdcevm::native_reference_decoder {
enum class Scope {Runnable,NoSuspend};
struct Bindings {
 void* owner=nullptr;
 JavaVM* vm=nullptr;
 // Already calibrated actual CurrentThread getter; no TLS/VM offsets inferred.
 void* (*current_thread)(void*)=nullptr;
 // Existing independently calibrated root decoder, established through real
 // public JNI/object observations. Borrowed results cannot cross a safepoint.
 bool (*strong_oracle)(void*,const runnable_jni::Context&,jobject,void**)=nullptr;
 bool (*native_scope)(void*,JNIEnv*,jobject,runnable_jni::Operation,void*)=nullptr;
};
class Decoder {
 public:
 bool Bind(HookEngine&,Bindings,std::string*);
 // Ordinary JNI. Caller owns each handle until Forget, and forgets before
 // DeleteGlobalRef/DeleteWeakGlobalRef; aliases retire decoder readers first.
 bool RegisterStrong(JNIEnv*,jobject,std::string*);
 bool RegisterWeak(JNIEnv*,jweak,std::string*);
 void Forget(jobject);
 // Owns temporary public global/weak references to distinct supplied objects,
 // verifies actual Thread/VM receivers and both live and GC-cleared weak refs.
 bool Calibrate(JNIEnv*,jobject distinct_a,jobject distinct_b,std::string*);
 bool Ready()const noexcept{return ready_;}
 // No JNI, allocation, or inferred handle tag. Only registered public globals.
 bool Strong(const runnable_jni::Context&,Scope,jobject,void**)const;
 // FieldWritten supplies its actual Thread. Runnable/shared mutator ownership
 // is mandatory; decoder can suspend. NoSuspend refuses BEFORE any leaf call.
 // Source Thread decoder normalizes cleared weak references; named
 // IsJWeakCleared independently witnesses the GC-cleared state.
 bool Weak(void* actual_thread,Scope,jweak,void**)const;
 private:
 using Object=abi::arm64::android11::ReleaseObjectPointer;
 using StrongLeaf=Object(*)(const void*,jobject);
 using ThreadClearedLeaf=bool(*)(const void*,jweak);
 using ClearedLeaf=bool(*)(void*,void*,void*);
 bool Register(JNIEnv*,jobject,jobjectRefType,std::string*);
 bool Registered(jobject,jobjectRefType)const;
 bool Receiver(const runnable_jni::Context&)const;
 bool IsCleared(void*,jweak)const;
 bool WeakRaw(void*,jweak,void**)const;
 static bool LiveProof(const runnable_jni::Context&,void*);
 static bool ClearedProof(const runnable_jni::Context&,void*);
 Bindings bindings_{};StrongLeaf candidates_[2]={nullptr,nullptr},strong_=nullptr;
 StrongLeaf thread_weak_=nullptr;ThreadClearedLeaf thread_cleared_=nullptr;ClearedLeaf cleared_=nullptr;
 bool bound_=false,ready_=false;
 mutable std::mutex handles_mutex_;struct Handle{jobjectRefType kind;size_t registrations;};
 std::unordered_map<jobject,Handle> handles_;
};
} // namespace artdcevm::native_reference_decoder
