#include "platform/common/source_check_episode.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <new>
using namespace artdcevm::native_hierarchy_source_checks::detail;
// Native source leaves are explicit doubles. This verifies the interception
// algorithm and exact dynamic extent, not actual graph or device authorization.
static thread_local unsigned forwarded=0;
static void OriginalFailure(void*,jvmtiError,const void*,const void*) {++forwarded;}
int main() {
 int owner=1,driver=2,sig=3,message=4,foreign=5;unsigned mark=0,replay=0,proof=0;
 auto granted=[&]{++proof;return true;};auto marker=[&]{assert(!active_episode);++mark;return true;};
 auto refuse=[&](void* value){assert(value==&owner&&!active_episode);++replay;};
 auto hierarchy=[&](void*){assert(active_episode);Record(OriginalFailure,&driver,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,&sig,&message);return false;};
 assert(Invoke(&owner,&driver,&sig,hierarchy,granted,marker,refuse));
 assert(proof==2&&mark==1&&replay==0&&forwarded==0&&!active_episode);
 // Single-def/name/modifier/constructor-lock errors all remain original errors.
 for(auto code:{JVMTI_ERROR_ILLEGAL_ARGUMENT,JVMTI_ERROR_NAMES_DONT_MATCH,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,JVMTI_ERROR_NOT_AVAILABLE}) {
  unsigned prior=forwarded;
  auto original=[&](void*){Record(OriginalFailure,&driver,code,&sig,&message);return false;};
  assert(!Invoke(&owner,&driver,&sig,original,granted,marker,refuse));assert(forwarded==prior+1&&mark==1&&replay==0);
 }
 // Inlined/unobserved RecordFailure is never inferred to be hierarchy changed.
 assert(!Invoke(&owner,&driver,&sig,[](void*){return false;},granted,marker,refuse));assert(mark==1);
 for(bool other_driver:{false,true}) {
  unsigned prior=forwarded;
  auto original=[&](void*){Record(OriginalFailure,other_driver?&foreign:&driver,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,other_driver?&sig:&foreign,&message);return false;};
  assert(!Invoke(&owner,&driver,&sig,original,granted,marker,refuse));assert(forwarded==prior+1&&mark==1);
 }
 // Exactly first 66 is deferred; repeats immediately preserve a real failure.
 auto twice=[&](void* value){hierarchy(value);hierarchy(value);return false;};unsigned prior=forwarded;
 assert(!Invoke(&owner,&driver,&sig,twice,granted,marker,refuse));assert(forwarded==prior+1&&mark==1&&replay==0);
 auto other_error=[&](void* value){hierarchy(value);Record(OriginalFailure,&driver,JVMTI_ERROR_OUT_OF_MEMORY,&sig,&message);return false;};prior=forwarded;
 assert(!Invoke(&owner,&driver,&sig,other_error,granted,marker,refuse));assert(forwarded==prior+1&&replay==0);
 auto other_class=[&](void* value){hierarchy(value);Record(OriginalFailure,&driver,JVMTI_ERROR_OUT_OF_MEMORY,&foreign,&message);return false;};prior=forwarded;
 assert(!Invoke(&owner,&driver,&sig,other_class,granted,marker,refuse));assert(forwarded==prior+1&&replay==0);
 // Native proof or marker failure cannot silently discard the source error.
 unsigned attempt=0;
 assert(!Invoke(&owner,&driver,&sig,hierarchy,[&]{return ++attempt==1;},marker,refuse));assert(replay==1&&mark==1);
 assert(!Invoke(&owner,&driver,&sig,hierarchy,granted,[]{return false;},refuse));assert(replay==2);
 auto inconsistent=[&](void* value){hierarchy(value);return true;};
 assert(!Invoke(&owner,&driver,&sig,inconsistent,granted,marker,refuse));assert(replay==3&&mark==1);
 // Reentrant CheckClass with identical driver/signature must not be credited to
 // outer original. The proxy masks interception during the nested original.
 prior=forwarded;
 auto recursive=[&](void*){
  assert(!Invoke(&owner,&driver,&sig,[&](void*){assert(!active_episode);Record(OriginalFailure,&driver,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,&sig,&message);return false;},granted,marker,refuse));return false;
 };
 assert(!Invoke(&owner,&driver,&sig,recursive,granted,marker,refuse));assert(forwarded==prior+1&&mark==1&&replay==3);
 prior=forwarded;
 auto deferred_recursive=[&](void* value){hierarchy(value);recursive(value);return false;};
 assert(!Invoke(&owner,&driver,&sig,deferred_recursive,granted,marker,refuse));assert(forwarded==prior+1&&replay==3);
 auto across_thread=[&](void*){
  std::thread thread([&]{assert(!active_episode);Record(OriginalFailure,&driver,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,&sig,&message);assert(forwarded==1);});thread.join();return false;
 };
 assert(!Invoke(&owner,&driver,&sig,across_thread,granted,marker,refuse));assert(mark==1&&replay==3);
 // Exception restoration is required before outer source failure bridge runs.
 try{Invoke(&owner,&driver,&sig,[](void*)->bool{throw std::bad_alloc();},granted,marker,refuse);assert(false);}catch(const std::bad_alloc&){assert(!active_episode);}
 prior=forwarded;Record(OriginalFailure,&driver,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,&sig,&message);assert(forwarded==prior+1);
 // Class flags choose exactly 71 for the exact staged final/abstract delta.
 // Fixed CheckClass returns there before name/hierarchy comparisons, so the
 // adapter must independently prove those declarations/edges before and after.
 auto modifier=[&](void*){Record(OriginalFailure,&driver,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED,&sig,&message);return false;};
 auto invoke_modifier=[&](auto original,auto verified,auto marked){return Invoke(&owner,&driver,&sig,original,verified,marked,refuse,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED);};
 unsigned prior_mark=mark;prior=forwarded;
 assert(invoke_modifier(modifier,granted,marker));assert(mark==prior_mark+1&&forwarded==prior);
 assert(!invoke_modifier(hierarchy,granted,marker));assert(forwarded==prior+1); // 66 cannot inherit a 71 grant.
 prior=forwarded;
 auto repeated_modifier=[&](void*value){modifier(value);modifier(value);return false;};
 assert(!invoke_modifier(repeated_modifier,granted,marker));assert(forwarded==prior+1);
 attempt=0;unsigned prior_replay=replay;
 assert(!invoke_modifier(modifier,[&]{return ++attempt==1;},marker));assert(replay==prior_replay+1);
 assert(!invoke_modifier(modifier,granted,[]{return false;}));assert(replay==prior_replay+2);
 prior=forwarded;
 auto lock_failure=[&](void*){Record(OriginalFailure,&driver,JVMTI_ERROR_INTERNAL,&sig,&message);return false;};
 assert(!invoke_modifier(lock_failure,granted,marker));assert(forwarded==prior+1); // Original 13/14 lock error remains original.
 prior=forwarded;
 assert(!Invoke(&owner,&driver,&sig,modifier,granted,marker,refuse));assert(forwarded==prior+1); // Unchanged flags still preserve 71.
 std::puts("source CheckClass exact-episode hierarchy66/classflags71 isolation PASS");
}
