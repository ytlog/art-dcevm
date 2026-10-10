#include <cstring>
#include <iostream>
static const char* waiting_case="valid";
static int waiting_mutex;
static bool waiting_locked=false;
static unsigned waiting_starts=0,waiting_preparations=0;
using WaitingOperation=void(*)(void*,void*);
using WaitingWait=int32_t(*)(void*,int32_t,void*);
static WaitingOperation waiting_lock_proxy=nullptr,waiting_unlock_proxy=nullptr;
static WaitingWait waiting_wait_proxy=nullptr;
static void ExecuteWaitingStart(void*,void*,int32_t,int32_t);
static void ExecuteWaitingFinish(void*,void*);
static void ExecuteWaitingCachePrepare();
#include "waiting_success_body.inc"
static void ExecuteWaitingStart(void*h,void*p,int32_t cause,int32_t collector){
 assert(h==&owner_heap&&p==thread.data()&&cause==8&&collector==1&&!gc&&!stopped&&Read32(p)==0);
 // The ordinary Collected view must be gone before the original GC scope:
 // a real mapping read here must consult the OS, never its old cached view.
 const auto queries=method_identity::MappingOsQueries();size_t extent=0;
 assert(method_identity::ReadableMappingExtent(p,&extent)&&extent>=thread.size());
 assert(method_identity::MappingOsQueries()>queries);
 Put32(p,0,74u<<24);waiting_lock_proxy(&waiting_mutex,p);
 assert(waiting_wait_proxy(h,cause,p)==2);
 gc=true;++waiting_starts;waiting_unlock_proxy(&waiting_mutex,p);
 assert(!stopped&&!waiting_locked&&Read32(p)==74u<<24);
 Put32(p,0,0); // Original host ART C++ scope restores Runnable; production never writes state.
}
static void ExecuteWaitingCachePrepare(){WaitingLock(&mutex,current);WaitingUnlock(&mutex,current);}
static void ExecuteWaitingFinish(void*,void*p){waiting_lock_proxy(&waiting_mutex,p);waiting_unlock_proxy(&waiting_mutex,p);}
int main(int argc,char**argv){
 if(argc>1)waiting_case=argv[1];
 RunWaitingTransaction();
 assert(waiting_starts==2&&waiting_preparations==1&&!waiting_locked&&!gc&&!stopped&&Read32(thread.data())==92u<<24);
 std::cout<<"Whole RunState original WaitingGC prepare/restore: "<<waiting_case<<" PASS\n";
}
