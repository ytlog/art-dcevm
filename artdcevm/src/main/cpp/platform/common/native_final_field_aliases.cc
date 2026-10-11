#include "platform/common/native_final_field_aliases.h"
#include <algorithm>
#include <cstdlib>
namespace artdcevm::final_field_aliases {
Set::~Set(){while(first_){Node* next=first_->next;delete first_;first_=next;}}
void Set::Publish(std::unique_ptr<Node> node)noexcept{
 if(!node)return;
 std::lock_guard<std::mutex> lock(mutex_);if(sequence_==UINT64_MAX)std::abort();
 node->sequence=++sequence_;node->next=first_;first_=node.release();
}
bool Set::Next(uint64_t* sequence,size_t* index,Lease* lease)const noexcept{
 std::lock_guard<std::mutex> lock(mutex_);
 for(Node* node=first_;node;node=node->next)if(node->sequence<=*sequence){
  size_t begin=node->sequence==*sequence?*index:0;
  for(size_t i=begin;i<node->aliases.size();++i){auto& alias=node->aliases[i];if(!alias.weak)continue;
   ++alias.readers;*sequence=node->sequence;*index=i+1;*lease={&alias,alias.weak,alias.member,alias.kind};return true;
  }
 }
 return false;
}
void Set::Release(const Lease& lease)const noexcept{
 std::lock_guard<std::mutex> lock(mutex_);if(!lease.alias||!lease.alias->readers)std::abort();
 --lease.alias->readers;readers_.notify_all();
}
void* Set::HistoricalOwner(const Lease&lease)const noexcept{
 if(!lease.member)return nullptr;
 switch(lease.kind){
  case MemberKind::Field:return reader_.field_owner?reader_.field_owner(reader_.owner,lease.member):nullptr;
  case MemberKind::Method:return reader_.method_owner?reader_.method_owner(reader_.owner,lease.member):nullptr;
 }
 return nullptr;
}
bool Set::Equivalent(void* current,void* historical)const noexcept{
 if(!current||!historical)return false;if(current==historical)return true;
 if(!reader_.owner||!reader_.weak||(!reader_.field_owner&&!reader_.method_owner))return false;
 uint64_t sequence=UINT64_MAX;size_t index=0;Lease lease;
 while(Next(&sequence,&index,&lease)){void* live=nullptr;
  bool same=reader_.weak(reader_.owner,lease.weak,&live)&&live==current&&HistoricalOwner(lease)==historical;
  Release(lease);if(same)return true;
 }
 return false;
}
bool Set::EquivalentWriter(void* thread,void* field,void* method)const noexcept{
 if(!thread||!field||!method||!reader_.owner||!reader_.weak_thread||!reader_.field_owner||!reader_.method_owner)return false;
 void* current=reader_.field_owner(reader_.owner,field);void* historical=reader_.method_owner(reader_.owner,method);
 if(current&&current==historical)return true;
 uint64_t sequence=UINT64_MAX;size_t index=0;Lease lease;
 while(Next(&sequence,&index,&lease)){void* live=nullptr;bool same=false;
  // Weak decoding can suspend; neither borrowed Class pointers nor the native
  // alias mutex cross that call. JNI handle retirement waits only for this lease.
  if(reader_.weak_thread(reader_.owner,thread,lease.weak,&live)&&live){
   current=reader_.field_owner(reader_.owner,field);
   historical=reader_.method_owner(reader_.owner,method);
   same=current==live&&historical&&HistoricalOwner(lease)==historical;
  }
  Release(lease);if(same)return true;
 }
 return false;
}
std::vector<void*> Set::WeakHandles()const{
 std::lock_guard<std::mutex> lock(mutex_);std::vector<void*> handles;
 for(Node* node=first_;node;node=node->next)for(const auto& alias:node->aliases)if(alias.weak&&std::find(handles.begin(),handles.end(),alias.weak)==handles.end())handles.push_back(alias.weak);
 return handles;
}
bool Set::Deactivate(void* weak)noexcept{
 if(!weak)return false;
 std::unique_lock<std::mutex> lock(mutex_);bool found=false;
 for(Node* node=first_;node;node=node->next)for(auto& alias:node->aliases)if(alias.weak==weak){alias.weak=nullptr;alias.retiring=weak;found=true;}
 if(!found)return false;
 readers_.wait(lock,[&]{for(Node* node=first_;node;node=node->next)for(const auto& alias:node->aliases)if(alias.retiring==weak&&alias.readers)return false;return true;});
 for(Node* node=first_;node;node=node->next)for(auto& alias:node->aliases)if(alias.retiring==weak){alias.retiring=nullptr;alias.member=nullptr;alias.kind=MemberKind::Field;}
 Node** cursor=&first_;
 while(*cursor){Node* node=*cursor;
  if(std::none_of(node->aliases.begin(),node->aliases.end(),[](const auto& alias){return alias.weak||alias.retiring||alias.readers;})){
   *cursor=node->next;delete node;
  }else cursor=&node->next;
 }
 return true;
}
} // namespace artdcevm::final_field_aliases
