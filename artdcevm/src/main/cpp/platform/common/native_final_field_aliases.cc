#include "platform/common/native_final_field_aliases.h"
#include <algorithm>
namespace artdcevm::final_field_aliases {
Set::~Set(){while(first_){Node* next=first_->next;delete first_;first_=next;}}
void Set::Publish(std::unique_ptr<Node> node)noexcept{
 if(!node)return;
 std::lock_guard<std::mutex> lock(mutex_);node->next=first_;first_=node.release();
}
bool Set::Equivalent(void* current,void* historical)const noexcept{
 if(!current||!historical)return false;
 if(current==historical)return true;
 if(!reader_.owner||!reader_.weak||!reader_.old_owner)return false;
 std::lock_guard<std::mutex> lock(mutex_);
 for(Node* node=first_;node;node=node->next)for(const auto& alias:node->aliases)if(alias.weak){void* live=nullptr;
  if(reader_.weak(reader_.owner,alias.weak,&live)&&live==current&&
     reader_.old_owner(reader_.owner,alias.old_field)==historical)return true;}
 return false;
}
std::vector<void*> Set::WeakHandles()const{
 std::lock_guard<std::mutex> lock(mutex_);std::vector<void*> handles;
 for(Node* node=first_;node;node=node->next)for(const auto& alias:node->aliases)if(alias.weak&&std::find(handles.begin(),handles.end(),alias.weak)==handles.end())handles.push_back(alias.weak);
 return handles;
}
bool Set::Deactivate(void* weak)noexcept{
 if(!weak)return false;
 std::lock_guard<std::mutex> lock(mutex_);bool found=false;
 Node** cursor=&first_;
 while(*cursor){Node* node=*cursor;
  for(auto& alias:node->aliases)if(alias.weak==weak){alias={};found=true;}
  if(std::none_of(node->aliases.begin(),node->aliases.end(),[](const auto& alias){return alias.weak!=nullptr;})){
   *cursor=node->next;delete node;
  }else cursor=&node->next;
 }
 return found;
}
} // namespace artdcevm::final_field_aliases
