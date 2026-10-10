#pragma once
#include "platform/common/native_receiver_bytecode.h"
#include "platform/common/native_receiver_source_observer.h"
#include <atomic>
#include <vector>
#include <jni.h>
namespace artdcevm::native_reference_decoder {class Decoder;}
namespace artdcevm::receiver_guard {
// Ordinary public GetBytecodes + reflected/JNI identities stage each sample.
// Only the actual sole DexPcMoved event may observe the source frame/resolver.
// Roots are freshly decoded inside that event after any original resolution.
class SourceCalibration final {
 public:
 SourceCalibration(FrameReader&frames,BytecodeSource&source,const native_reference_decoder::Decoder&roots):frames_(frames),source_(source),roots_(roots),observer_(this,Notify,&frames,&source){}
 bool Stage(void*actual_thread,void*native_caller,void*native_target,jobject receiver,jobject special_caller,
            InvokeKind,bool range,const unsigned char*public_bytecodes,size_t bytes);
 void DexPcMoved(void*actual_thread,void*actual_caller,uint32_t actual_pc)noexcept;
 bool Finish(bool original_public_result)noexcept;
 bool Seal()noexcept;
 bool Proven()const noexcept{return sealed_.load(std::memory_order_acquire)&&source_.Proven()&&source_.UsesFrames(frames_);}
 const SourceObservation& Observer()const noexcept{return observer_;}
 const FrameReader& Frames()const noexcept{return frames_;}
 const BytecodeSource& Source()const noexcept{return source_;}
 private:
 static void Notify(void*p,void*t,void*m,uint32_t pc)noexcept{static_cast<SourceCalibration*>(p)->DexPcMoved(t,m,pc);}
 FrameReader&frames_;BytecodeSource&source_;const native_reference_decoder::Decoder&roots_;
 const SourceObservation observer_;
 std::vector<uint16_t> public_code_;void*thread_=nullptr,*caller_=nullptr,*target_=nullptr;
 jobject receiver_=nullptr,special_=nullptr;InvokeKind kind_=InvokeKind::NonInvoke;bool range_=false;
 std::atomic<bool> armed_{false},seen_{false};uint16_t witnessed_form_=0;
 uint16_t forms_=0;std::atomic<bool> sealed_{false};
};
}
