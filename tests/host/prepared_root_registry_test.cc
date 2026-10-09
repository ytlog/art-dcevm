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
struct State {void* live[16]{};size_t size=0,released=0;};
jobject Root(void* raw,void* managed){auto& state=*static_cast<State*>(raw);assert(state.size<16);state.live[state.size]=managed;return reinterpret_cast<jobject>(&state.live[state.size++]);}
bool Decode(void*,jobject root,void** out){*out=*reinterpret_cast<void**>(root);return *out!=nullptr;}
void Release(void* raw,jobject){++static_cast<State*>(raw)->released;}
int main(){using namespace artdcevm::prepared_roots;int a=1,b=2,new_a=3,new_b=4;State state;
 Registry registry({&state,Root,Decode,Release});Bindings bindings;void *left=&a,*right=&b,*null_parent=nullptr;
 assert(bindings.Add(&left,registry.Collect())&&bindings.Add(&right,registry.Collect())&&bindings.Add(&null_parent,registry.Collect()));
 size_t duplicate=99;assert(registry.Add(&a,&duplicate)&&duplicate==0&&registry.Size()==2);
 bindings.Seal();assert(registry.Freeze());state.live[0]=&new_a;state.live[1]=&new_b;
 forbid=true;bool throws=false;try{auto p=::operator new(16);::operator delete(p);}catch(const std::bad_alloc&){throws=true;}assert(throws);
 assert(registry.Resolve()&&bindings.CanRefresh(registry.Final()));bindings.Refresh(registry.Final());
 assert(left==&new_a&&right==&new_b&&!null_parent);
 state.live[1]=nullptr;assert(!registry.Resolve());assert(left==&new_a&&right==&new_b);
 size_t slot=0;assert(!registry.Add(&a,&slot));forbid=false;registry.Clear();assert(state.released==2&&registry.Size()==0);
 std::cout<<"Actual prepared registry: rooted moving identities, dedup/null roots, freeze and refresh with allocator forbidden PASS\n";
}
