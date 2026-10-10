#pragma once
#include "platform/abi/arm64/android13/handle_root_scope_abi.h"
#include <cassert>
#include <cstring>
#include <vector>
// Independent source-contract walker: only non-null compressed reference slots
// are forwarded, in top-to-link order, exactly as Thread::HandleScopeVisitRoots.
inline void HostHandleScopeVisitRoots(void* thread,artdcevm::android13::handle_root_abi::RootVisitor* visitor,uint32_t id){
 namespace a=artdcevm::android13::handle_root_abi;void* top;
 std::memcpy(&top,static_cast<unsigned char*>(thread)+280,8);
 std::vector<uint32_t*> slots;
 for(void* scope=top;scope;){void* link;int32_t count;std::memcpy(&link,scope,8);std::memcpy(&count,static_cast<unsigned char*>(scope)+8,4);assert(count>=0);
  for(int32_t i=0;i<count;++i){auto* slot=reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(scope)+12+4*i);if(*slot)slots.push_back(slot);}scope=link;
 }
 a::RootInfo info{nullptr,4,id};if(!slots.empty())visitor->VisitRoots(slots.data(),slots.size(),info);
}
