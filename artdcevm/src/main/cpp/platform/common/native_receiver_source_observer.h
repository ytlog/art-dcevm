#pragma once
#include <cstdint>
namespace artdcevm::receiver_guard {
class SourceCalibration;class FrameReader;class BytecodeSource;
// Immutable concrete source-calibration dispatch. Only SourceCalibration can
// construct it; callers cannot supply a handler or replace its native owner.
// It records source events and carries no publication/readiness authority.
class SourceObservation final {
 public:
 void DexPcMoved(void*t,void*m,uint32_t pc)const noexcept{notify_(owner_,t,m,pc);}
 const FrameReader& Frames()const noexcept{return *frames_;}
 const BytecodeSource& Source()const noexcept{return *source_;}
 SourceObservation(const SourceObservation&)=delete;SourceObservation&operator=(const SourceObservation&)=delete;
 private:
 friend class SourceCalibration;
 SourceObservation(void*p,void(*f)(void*,void*,void*,uint32_t)noexcept,const FrameReader*frames,const BytecodeSource*source):owner_(p),notify_(f),frames_(frames),source_(source){}
 void*const owner_;void(*const notify_)(void*,void*,void*,uint32_t)noexcept;const FrameReader*const frames_;const BytecodeSource*const source_;
};
}
