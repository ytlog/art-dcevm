#pragma once
#include "api/runtime_backend.h"
#include <jni.h>
#include <cstdlib>
#include <memory>
#include <vector>
namespace artdcevm::production_bootstrap {
// Callers serialize operations. Weak Class identity distinguishes equal names
// from different loaders; each comparison pins a live Class only for that call.
class Provenance {
 private:
  struct Entry {jweak klass;std::vector<unsigned char> dex;};
 public:
  // Allocate/copy before source mutation. A candidate shares existing weak
  // handles without owning them; only newly created handles belong to this
  // reservation until publication. Abort never changes a live cached DEX.
  class Reservation final {
   public:
    ~Reservation(){
      for(auto weak:created_)env_->DeleteWeakGlobalRef(weak);
      if(owner_){if(owner_->pending_!=this)std::abort();owner_->pending_=nullptr;}
    }
    Reservation(const Reservation&)=delete;Reservation&operator=(const Reservation&)=delete;
    // After actual whole-batch success and source cleanup. No allocation/JNI,
    // native Class read, or compilation/root/publication authority is involved.
    void Commit()noexcept{
      if(!owner_||owner_->pending_!=this)std::abort();
      owner_->entries_.swap(candidate_);created_.clear();
      owner_->pending_=nullptr;owner_=nullptr;
    }
   private:
    friend class Provenance;
    Reservation(Provenance&owner,JNIEnv*env):owner_(&owner),env_(env){}
    Provenance*owner_;JNIEnv*env_;
    std::vector<Entry>candidate_;
    std::vector<jweak>created_;
  };
  Provenance()=default;
  Provenance(const Provenance&)=delete;Provenance&operator=(const Provenance&)=delete;
  ~Provenance(){if(pending_||!entries_.empty())std::abort();}
  std::unique_ptr<Reservation>Prepare(JNIEnv*env,const std::vector<Definition>&definitions){
    if(!trusted_||!env||env->ExceptionCheck()||definitions.empty()||pending_||definitions.size()>SIZE_MAX-entries_.size())return nullptr;
    Sweep(env);if(env->ExceptionCheck())return nullptr;
    auto candidate=std::unique_ptr<Reservation>(new Reservation(*this,env));pending_=candidate.get();
    candidate->candidate_=entries_;
    candidate->candidate_.reserve(entries_.size()+definitions.size());
    candidate->created_.reserve(definitions.size());
    for(size_t n=0;n<definitions.size();++n){const auto&definition=definitions[n];
      if(!definition.klass||definition.dex.empty())return nullptr;
      for(size_t p=0;p<n;++p)if(env->IsSameObject(definition.klass,definitions[p].klass))return nullptr;
      if(env->ExceptionCheck())return nullptr;
      size_t found=SIZE_MAX;
      for(size_t p=0;p<candidate->candidate_.size();++p){
        auto live=env->NewLocalRef(candidate->candidate_[p].klass);
        if(env->ExceptionCheck()){if(live)env->DeleteLocalRef(live);return nullptr;}
        const bool same=live&&env->IsSameObject(live,definition.klass);if(live)env->DeleteLocalRef(live);
        if(env->ExceptionCheck())return nullptr;
        if(same){if(found!=SIZE_MAX)return nullptr;found=p;}
      }
      std::vector<unsigned char>bytes=definition.dex;
      if(found!=SIZE_MAX){candidate->candidate_[found].dex=std::move(bytes);continue;}
      auto weak=env->NewWeakGlobalRef(definition.klass);
      if(!weak||env->ExceptionCheck()){if(weak)env->DeleteWeakGlobalRef(weak);return nullptr;}
      // Both vectors have capacity. No allocation can orphan this JNI handle.
      candidate->created_.push_back(weak);
      candidate->candidate_.push_back({weak,std::move(bytes)});
    }
    return candidate;
  }
  bool Find(JNIEnv* env,jclass klass,std::vector<unsigned char>* dex){
    if(!trusted_||!env||!klass||!dex||env->ExceptionCheck())return false;
    Sweep(env);if(env->ExceptionCheck())return false;
    for(auto& entry:entries_){
      auto live=env->NewLocalRef(entry.klass);
      if(env->ExceptionCheck()){if(live)env->DeleteLocalRef(live);return false;}
      bool same=live&&env->IsSameObject(live,klass);if(live)env->DeleteLocalRef(live);
      if(env->ExceptionCheck())return false;
      if(same){*dex=entry.dex;return true;}
    }
    return false;
  }
  void Sweep(JNIEnv* env){
    if(pending_||!env||env->ExceptionCheck())return;
    for(auto entry=entries_.begin();entry!=entries_.end();){
      auto live=env->NewLocalRef(entry->klass);
      if(env->ExceptionCheck()){if(live)env->DeleteLocalRef(live);return;}
      if(live){env->DeleteLocalRef(live);++entry;}
      else {env->DeleteWeakGlobalRef(entry->klass);entry=entries_.erase(entry);}
    }
  }
  void Clear(JNIEnv* env){
    if(pending_||(!env&&!entries_.empty()))std::abort();
    for(auto& entry:entries_)env->DeleteWeakGlobalRef(entry.klass);entries_.clear();
  }
  // An unrecorded successful batch can affect any Class. Keep handles for
  // ordinary cleanup, but never fall back to stale cache/APK bytes afterward.
  void Invalidate()noexcept{trusted_=false;}
  bool Trusted()const noexcept{return trusted_;}
  size_t Size()const{return entries_.size();}
 private:
  std::vector<Entry> entries_;
  Reservation*pending_=nullptr;
  bool trusted_=true;
};
}
