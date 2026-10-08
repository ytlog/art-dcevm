#pragma once
#include <jni.h>
#include <vector>
namespace artdcevm::production_bootstrap {
// Callers serialize operations. Weak Class identity distinguishes equal names
// from different loaders; each comparison pins a live Class only for that call.
class Provenance {
 public:
  bool Store(JNIEnv* env,jclass klass,const std::vector<unsigned char>& dex){
    if(!env||!klass||env->ExceptionCheck())return false;
    Sweep(env);if(env->ExceptionCheck())return false;
    for(auto& entry:entries_){
      auto live=env->NewLocalRef(entry.klass);
      if(env->ExceptionCheck()){if(live)env->DeleteLocalRef(live);return false;}
      bool same=live&&env->IsSameObject(live,klass);if(live)env->DeleteLocalRef(live);
      if(same){entry.dex=dex;return true;}
    }
    auto weak=env->NewWeakGlobalRef(klass);
    if(!weak||env->ExceptionCheck())return false;
    entries_.push_back({weak,dex});return true;
  }
  bool Find(JNIEnv* env,jclass klass,std::vector<unsigned char>* dex){
    if(!env||!klass||!dex||env->ExceptionCheck())return false;
    Sweep(env);if(env->ExceptionCheck())return false;
    for(auto& entry:entries_){
      auto live=env->NewLocalRef(entry.klass);
      if(env->ExceptionCheck()){if(live)env->DeleteLocalRef(live);return false;}
      bool same=live&&env->IsSameObject(live,klass);if(live)env->DeleteLocalRef(live);
      if(same){*dex=entry.dex;return true;}
    }
    return false;
  }
  void Sweep(JNIEnv* env){
    for(auto entry=entries_.begin();entry!=entries_.end();){
      auto live=env->NewLocalRef(entry->klass);
      if(env->ExceptionCheck()){if(live)env->DeleteLocalRef(live);return;}
      if(live){env->DeleteLocalRef(live);++entry;}
      else {env->DeleteWeakGlobalRef(entry->klass);entry=entries_.erase(entry);}
    }
  }
  void Clear(JNIEnv* env){for(auto& entry:entries_)env->DeleteWeakGlobalRef(entry.klass);entries_.clear();}
  size_t Size()const{return entries_.size();}
 private:
  struct Entry {jweak klass;std::vector<unsigned char> dex;};
  std::vector<Entry> entries_;
};
}
