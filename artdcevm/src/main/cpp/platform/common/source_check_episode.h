#pragma once
#include <jvmti.h>
#include <cstddef>
namespace artdcevm::native_hierarchy_source_checks::detail {
// Internal interception mechanism, not an ART permission service. The source
// adapter must independently prove the actual owner/driver/graph before entry.
struct Episode {
 void* driver; const void* signature; size_t deferred=0;
 jvmtiError expected_failure=JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED;
 bool other_failure=false; Episode* previous=nullptr;
};
inline thread_local Episode* active_episode=nullptr;
using Failure=void(*)(void*,jvmtiError,const void*,const void*);
inline void Record(Failure original,void* driver,jvmtiError code,
                   const void* signature,const void* message) {
 auto* e=active_episode;
 // A different Class in the same source driver can set that driver's result.
 // Preserve its diagnostic and invalidate any deferred success in this episode.
 if(e&&e->driver==driver&&e->signature!=signature)e->other_failure=true;
 if(e&&e->driver==driver&&e->signature==signature) {
  if(code==e->expected_failure) {
   ++e->deferred;
   // A repeated error is already anomalous. Forward it now, while its actual
   // platform string still exists, and never grant this invocation afterward.
   if(e->deferred==1&&!e->other_failure)return;
  } else e->other_failure=true;
 }
 original(driver,code,signature,message);
}
template<class Check,class Proof,class Mark,class Refuse>
bool Invoke(void* owner,void* driver,const void* signature,Check original,
            Proof proof,Mark mark,Refuse refuse,
            jvmtiError expected_failure=JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED) {
 // Never nest authority episodes: a recursive original check remains original.
 if(active_episode) {
  active_episode->other_failure=true;
  struct Reset {Episode* previous;~Reset(){active_episode=previous;}} reset{active_episode};
  active_episode=nullptr;
  return original(owner);
 }
 if(!proof())return original(owner);
 if(expected_failure!=JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED&&
    expected_failure!=JVMTI_ERROR_UNSUPPORTED_REDEFINITION_CLASS_MODIFIERS_CHANGED)return original(owner);
 Episode episode{driver,signature,0,expected_failure,false,active_episode};
 bool result;
 {
  struct Reset {Episode* previous;~Reset(){active_episode=previous;}} reset{active_episode};
  active_episode=&episode;
  result=original(owner);
 }
 // No callback, marker or fallback runs inside the suppression dynamic extent.
 if(episode.deferred==0)return result;
 if(!result&&episode.deferred==1&&!episode.other_failure&&proof()&&mark())return true;
 // Every other real failure was forwarded. Only replace an exclusively deferred
 // expected diagnostic. Never retain the original temporary std::string.
 if(episode.deferred==1&&!episode.other_failure)refuse(owner);
 return false;
}
}
