#pragma once
#include "platform/common/native_owned_removal.h"
#include <cstring>
#include <limits>
namespace artdcevm::rooted_schema_plan {
// The actual Holder object-array globals, never cached decoded object pointers,
// are authoritative at both refundable collection and final GC-disabled entry.
struct Reader {
 void* owner=nullptr;
 bool (*decode)(void*,jobject,void**)=nullptr;
 bool (*readable)(void*,const void*,size_t)=nullptr;
 void* (*reference)(void*,void*,size_t)=nullptr;
};
inline bool Read(const native_owned_removal::RootedGraph& roots,bool collected,
                 const Reader& reader,source_commit::Plan* result,std::string* error){
 const auto fail=[&](const char* message){if(error)*error=message;return false;};
 if(!result||!collected||!roots.mirror||roots.partitions.empty()||!reader.decode||!reader.readable||!reader.reference)
  return fail("Schema has no complete collected Holder roots");
 source_commit::Plan plan;
 if(!reader.decode(reader.owner,roots.mirror,&plan.mirror)||!plan.mirror)return fail("Schema mirror root was lost");
 const auto elements=[&](jobjectArray root,size_t count,std::vector<void*>* out,void** array){
  *array=nullptr;if(!root)return count==0;
  if(count>size_t(std::numeric_limits<int32_t>::max())||count>(SIZE_MAX-12)/4||
     !reader.decode(reader.owner,root,array)||!reader.readable(reader.owner,*array,12+count*4))return false;
  int32_t length=0;std::memcpy(&length,static_cast<char*>(*array)+8,4);
  if(length<0||size_t(length)!=count)return false;
  for(size_t i=0;i<count;++i){void* value=reader.reference(reader.owner,*array,12+i*4);
   if(!value||!reader.readable(reader.owner,value,8))return false;out->push_back(value);}
  return true;
 };
 size_t previous=0;bool first=true;
 for(const auto& part:roots.partitions){
  if(!part.class_count||!part.old_classes||!part.new_classes||(!first&&part.holder_index<=previous))return fail("Schema Holder partition order is invalid");
  first=false;previous=part.holder_index;
  void *old_array=nullptr,*new_array=nullptr,*old_objects=nullptr,*new_objects=nullptr;
  if(!elements(part.old_classes,part.class_count,&plan.old_classes,&old_array)||
     !elements(part.new_classes,part.class_count,&plan.new_classes,&new_array)||
     !elements(part.old_instances,part.instance_count,&plan.old_instances,&old_objects)||
     !elements(part.new_instances,part.instance_count,&plan.new_instances,&new_objects))return fail("Schema Holder arrays lost their exact rooted shape");
  if(!plan.new_classes_root)plan.new_classes_root=new_array;
  for(size_t n=0;n<part.class_count;++n)plan.class_root_origins.push_back({new_array,n});
 }
 *result=std::move(plan);return true;
}
// The same complete provider preparation must succeed in the last refundable
// source boolean. This helper never invokes BeginCommit or any mutation.
inline bool ValidateCollected(const source_commit::Plan& plan,
 std::unique_ptr<source_commit::Services> (*prepare)(const source_commit::Plan&,void*,std::string*),
 void* owner,std::string* error){
 if(!prepare){if(error)*error="Missing collected schema provider";return false;}
 auto service=prepare(plan,owner,error);
 if(service&&service->Validate(plan))return true;
 if(error&&error->empty())*error="Complete native schema preparation was rejected before publication";
 return false;
}

}
