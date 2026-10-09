#pragma once
#include <cstdint>
namespace artdcevm {
// Diagnostic only. Saturation/truncation never grants or changes permissions.
struct BoundedNativeEventTrace {
 uint64_t order=0;uint32_t mask=0,events=0;bool truncated=false;
 void Record(uint8_t event) noexcept {
  if(event==0||event>15)return;
  mask|=uint32_t{1}<<event;
  if(events<16)order|=uint64_t{event}<<(4*events);else truncated=true;
  if(events!=UINT32_MAX)++events;
 }
};
}
