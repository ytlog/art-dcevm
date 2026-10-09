#include "platform/common/prepared_root_registry.h"
#include <cassert>
#include <cstdlib>
#include <new>
#include <iostream>
static bool forbid=false;
void* operator new(std::size_t bytes){if(forbid)throw std::bad_alloc();if(auto p=std::malloc(bytes))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void* operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete[](void* p)noexcept{::operator delete(p);}
struct State {void* live[16]{};size_t size=0,released=0,decoded=0;};
jobject Root(void* raw,void* managed){auto& state=*static_cast<State*>(raw);assert(state.size<16);state.live[state.size]=managed;return reinterpret_cast<jobject>(&state.live[state.size++]);}
bool Decode(void* raw,jobject root,void** out){++static_cast<State*>(raw)->decoded;*out=*reinterpret_cast<void**>(root);return *out!=nullptr;}
void Release(void* raw,jobject){++static_cast<State*>(raw)->released;}
int main(){using namespace artdcevm::prepared_roots;int a=1,b=2,new_a=3,new_b=4;State state;
 Registry registry({&state,Root,Decode,Release});Bindings bindings;void *left=&a,*right=&b,*null_parent=nullptr;
 assert(bindings.Add(&left,registry.Collect())&&bindings.Add(&right,registry.Collect())&&bindings.Add(&null_parent,registry.Collect()));
 size_t duplicate=99;assert(registry.Add(&a,&duplicate)&&duplicate==0&&registry.Size()==2);
 Bindings stopped;void* stopped_left=&a;void* missing=&a;
 assert(stopped.Add(&stopped_left,registry.Collect()));stopped.Seal();
 bindings.Seal();assert(registry.Freeze());state.live[0]=&new_a;state.live[1]=&new_b;
 size_t found=99;auto existing=registry.Existing();
 assert(!existing.add(existing.owner,&new_a,&found)&&found==99);
 assert(!bindings.CanRefresh(registry.Final()));
 forbid=true;bool throws=false;try{auto p=::operator new(16);::operator delete(p);}catch(const std::bad_alloc&){throws=true;}assert(throws);
 assert(registry.Resolve()&&bindings.CanRefresh(registry.Final()));bindings.Refresh(registry.Final());
 assert(left==&new_a&&right==&new_b&&!null_parent);
 const auto decoded=state.decoded;
 assert(stopped.CanRefresh(registry.Final()));stopped.Refresh(registry.Final());
 assert(stopped_left==&new_a);
 assert(!existing.add(existing.owner,missing,&found));
 for(size_t i=0;i<1000;++i){
  found=99;assert(existing.add(existing.owner,&new_b,&found)&&found==1);
 }
 assert(state.decoded==decoded&&state.size==2&&state.released==0);
 registry.Invalidate();
 assert(!stopped.CanRefresh(registry.Final()));
 assert(!existing.add(existing.owner,&new_a,&found));
 state.live[0]=&a;state.live[1]=&b;
 assert(registry.Resolve()&&stopped.CanRefresh(registry.Final()));stopped.Refresh(registry.Final());
 assert(stopped_left==&a);
 assert(!existing.add(existing.owner,&new_a,&found));
 assert(existing.add(existing.owner,&a,&found)&&found==0);
 state.live[1]=nullptr;assert(!registry.Resolve());assert(left==&new_a&&right==&new_b);
 assert(!bindings.CanRefresh(registry.Final())&&!stopped.CanRefresh(registry.Final()));
 assert(!existing.add(existing.owner,&a,&found));
 assert(state.released==0&&state.size==2);
 size_t slot=0;assert(!registry.Add(&a,&slot));forbid=false;registry.Clear();assert(state.released==2&&registry.Size()==0);
 std::cout<<"Actual prepared registry: moving identities, lookup-only root reuse without decoder/root/release/allocation, invalidation and partial-resolution refusal PASS\n";
}
