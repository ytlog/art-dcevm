#include "platform/common/class_loader_cleanup.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
using namespace artdcevm::class_loader_cleanup;
using Record = artdcevm::class_loader_abi::arm64::ClassLoaderData;
namespace {
struct Allocator {bool live=true;};
int linker,thread;
Declaration selected;
std::vector<void*> pruned,forwarded;
bool expected_cha;
void Original(void* l,void* t,const void* raw,bool cha){
 assert(l==&linker&&t==&thread&&cha==expected_cha);
 const auto& record=*static_cast<const Record*>(raw);
 if(record.allocator){
  auto* allocator=static_cast<Allocator*>(record.allocator);
  assert(allocator->live);
  assert(!pruned.empty()&&pruned.back()==allocator);
  if(selected==Declaration::kDeleteAndDestroy)allocator->live=false;
 }
 forwarded.push_back(const_cast<void*>(raw));
}
}
int main(){
 assert(std::strcmp(Symbol(Declaration::kDeleteAndDestroy),"_ZN3art11ClassLinker17DeleteClassLoaderEPNS_6ThreadERKNS0_15ClassLoaderDataEb")==0);
 assert(std::strcmp(Symbol(Declaration::kPrepareForBatchDelete),"_ZN3art11ClassLinker26PrepareToDeleteClassLoaderEPNS_6ThreadERKNS0_15ClassLoaderDataEb")==0);
 assert(!Symbol(static_cast<Declaration>(99)));
 for(auto declaration:{Declaration::kDeleteAndDestroy,Declaration::kPrepareForBatchDelete})for(bool cha:{false,true}){
  selected=declaration;expected_cha=cha;pruned.clear();forwarded.clear();
  Allocator allocators[3];Record records[3];
  for(int i=0;i<3;++i)records[i]={&linker,&thread,&allocators[i]};
  for(int i=0;i<3;++i){
   Record before=records[i];
   assert(Dispatch(declaration,Original,&linker,&thread,&records[i],cha,[&](void* a){
    assert(static_cast<Allocator*>(a)->live);
    // Batch preparation must retain previous allocator metadata until all
    // loader dependencies have been visited. The helper cannot free it early.
    if(declaration==Declaration::kPrepareForBatchDelete)for(auto& item:allocators)assert(item.live);
    pruned.push_back(a);
   }));
   assert(std::memcmp(&before,&records[i],sizeof(before))==0);
   assert(forwarded.back()==&records[i]);
   assert(allocators[i].live==(declaration==Declaration::kPrepareForBatchDelete));
  }
  assert(pruned.size()==3&&forwarded.size()==3);
  if(declaration==Declaration::kPrepareForBatchDelete)for(auto& item:allocators)item.live=false;
  Record empty{nullptr,nullptr,nullptr};
  assert(Dispatch(declaration,Original,&linker,&thread,&empty,cha,[](void*){assert(false);}));
  assert(pruned.size()==3&&forwarded.size()==4);
  int calls=0;
  auto forbidden=[&](void*){++calls;};
  assert(!Dispatch(static_cast<Declaration>(99),Original,&linker,&thread,&empty,cha,forbidden));
  assert(!Dispatch(declaration,nullptr,&linker,&thread,&empty,cha,forbidden));
  assert(!Dispatch(declaration,Original,&linker,&thread,nullptr,cha,forbidden));
  assert(calls==0&&forwarded.size()==4);
 }
 std::cout<<"Class-loader cleanup: exact declarations, pre-cleanup pruning, batch lifetime and original argument forwarding passed\n";
}
