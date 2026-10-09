#pragma once
static void WaitingLock(void*m,void*p){if(m==&mutex){assert(p==thread.data());if(stopped){assert(!locked);++locked;}return;}assert(m==&waiting_mutex&&p==thread.data()&&!waiting_locked);waiting_locked=true;}
static void WaitingUnlock(void*m,void*p){if(m==&mutex){if(stopped)Unlock(m,p);return;}assert(m==&waiting_mutex&&p==thread.data()&&waiting_locked);waiting_locked=false;}
static int32_t WaitingForGC(void*h,int32_t cause,void*p){
 assert(h==&owner_heap&&p==thread.data()&&cause==8&&Read32(p)==74u<<24&&waiting_locked);
 waiting_unlock_proxy(&waiting_mutex,p);waiting_lock_proxy(&waiting_mutex,p);return 2;
}
class WaitingHooks final:public OwnerHooks {
 public:void* FindSymbol(const char*lib,const char*n)override {
  if(std::strstr(n,"SetStatusLocked"))return reinterpret_cast<void*>(MatchedStatus);
  if(std::strstr(n,"SetObsoleteClass"))return reinterpret_cast<void*>(MatchedRetain);
  if(std::strstr(n,"SetEntryPointsForObsoleteMethod"))return reinterpret_cast<void*>(MatchedObsolete);
  if(std::strstr(n,"ReplaceReferences"))return reinterpret_cast<void*>(MatchedReplace);
  if(!std::strcmp(n,"_ZN3art2gc4Heap25WaitForGcToCompleteLockedENS0_7GcCauseEPNS_6ThreadE"))return reinterpret_cast<void*>(WaitingForGC);
  if(!std::strcmp(n,"_ZN3art5Mutex13ExclusiveLockEPNS_6ThreadE"))return reinterpret_cast<void*>(WaitingLock);
  if(!std::strcmp(n,"_ZN3art5Mutex15ExclusiveUnlockEPNS_6ThreadE"))return reinterpret_cast<void*>(WaitingUnlock);
  return OwnerHooks::FindSymbol(lib,n);
 }
 void* Install(void*t,void*p,void**o,std::string*e)override {
  if(t==reinterpret_cast<void*>(WaitingForGC)){*o=t;waiting_wait_proxy=reinterpret_cast<WaitingWait>(p);return t;}
  if(t==reinterpret_cast<void*>(WaitingLock)){*o=t;waiting_lock_proxy=reinterpret_cast<WaitingOperation>(p);return t;}
  if(t==reinterpret_cast<void*>(WaitingUnlock)){*o=t;waiting_unlock_proxy=reinterpret_cast<WaitingOperation>(p);return t;}
  return OwnerHooks::Install(t,p,o,e);
 }
};
static bool WaitingMappings(void*o,std::string*e){
 assert(Read32(thread.data())==74u<<24&&gc&&stopped==1&&!waiting_locked);++waiting_preparations;
 if(!std::strcmp(waiting_case,"throw"))throw std::bad_alloc();
 if(!std::strcmp(waiting_case,"refuse")){*e="independent stopped mapping refusal";return false;}
 return RunStateMappings::Refresh(o,e);
}
