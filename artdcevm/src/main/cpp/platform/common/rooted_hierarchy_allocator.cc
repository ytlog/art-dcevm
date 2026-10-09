#include "platform/common/rooted_hierarchy_allocator.h"
namespace artdcevm::rooted_hierarchy {
void* Allocator::Allocate(Access& ops,void* owner,Root old_class,Root cache,uint16_t index){
 if(failed_)return nullptr;
 auto fail=[&](const char* message)->void*{failed_=true;ops.Fail(message);return nullptr;};
 if(targets_.empty()||!owner||!ops.Get(old_class)||!ops.Get(cache))return fail("Missing rooted hierarchy allocation inputs");
 struct Frame {void* owner;Root old_class,cache,result,parent=nullptr,dependency=nullptr;uint16_t index;unsigned stage=0;};
 std::vector<Frame> work;
 auto enqueue=[&](void* selected_owner,Root old,Root selected_cache,uint16_t selected_index,Root* output){
  for(const auto& replacement:replacements_)if(ops.Get(replacement.old_class)==ops.Get(old)){
   if(!ops.Get(replacement.new_class)){failed_=true;ops.Fail("Cycle in the requested batch superclass graph");return false;}
   *output=replacement.new_class;return true;
  }
  Root stable_old=ops.Copy(old),stable_cache=ops.Copy(selected_cache),result=ops.Empty();
  if(!stable_old||!stable_cache||!result||!ops.Get(stable_old)||!ops.Get(stable_cache)){
   failed_=true;ops.Fail("Unable to retain hierarchy allocation roots");return false;
  }
  replacements_.push_back({stable_old,result});
  work.push_back({selected_owner,stable_old,stable_cache,result,nullptr,nullptr,selected_index,0});
  *output=result;return true;
 };
 Root final_result=nullptr;
 if(!enqueue(owner,old_class,cache,index,&final_result))return nullptr;
 // An explicit work stack avoids exhausting the ART native stack for a deep
 // loaded inheritance chain. All stack entries contain stable GC-updated slots.
 while(!work.empty()){
  const size_t position=work.size()-1;
  if(work[position].stage==0){
   const auto current=work[position];
   const std::string requested=ops.RequestedParent(current.cache,current.index);
   Root loader=ops.Loader(current.old_class),parent=requested.empty()?nullptr:ops.Resolve(requested,loader);
   if(!parent||!ops.Get(parent))return fail("Cannot resolve a superclass during batch allocation");
   size_t explicit_parent=targets_.size(),structural_root=targets_.size();
   for(size_t i=0;i<targets_.size();++i){
    if(ops.Get(targets_[i].klass)==ops.Get(parent)){
     if(explicit_parent!=targets_.size())return fail("Duplicate rooted parent target");
     explicit_parent=i;
    }
    if(targets_[i].structural&&ops.Subclass(parent,targets_[i].klass)){
     if(structural_root!=targets_.size())return fail("Ambiguous structural allocation island");
     structural_root=i;
    }
   }
   Root selected=ops.Copy(parent);
   if(!selected)return fail("Unable to retain selected superclass");
   work[position].parent=selected;work[position].stage=2;
   if(structural_root!=targets_.size()){
    const bool direct=explicit_parent!=targets_.size();
    Root parent_cache=direct?targets_[explicit_parent].cache:ops.Cache(parent);
    uint16_t parent_index=direct?0:ops.ClassIndex(parent);
    void* parent_owner=targets_[direct?explicit_parent:structural_root].owner;
    Root dependency=nullptr;
    if(!enqueue(parent_owner,parent,parent_cache,parent_index,&dependency))return nullptr;
    // enqueue may reallocate the native work vector; use the saved index.
    work[position].dependency=dependency;work[position].stage=1;
   }
   continue;
  }
  auto current=work[position];
  if(current.stage==1){
   void* parent=ops.Get(current.dependency);
   if(!parent)return fail("Missing completed rooted parent allocation");
   ops.Set(current.parent,parent);work[position].stage=2;
   continue;
  }
  void* allocated=ops.Allocate(current.owner,current.old_class,current.parent,current.cache,current.index,current.result);
  if(!allocated){failed_=true;return nullptr;} // Preserve source failure and early capture.
  Root observed=ops.Empty();
  if(!observed)return fail("Unable to retain returned hierarchy allocation Class");
  ops.Set(observed,allocated);
  if(!ops.ValidateReplacement(current.old_class,observed,current.parent,current.cache))return fail("Linked replacement has wrong Class, loader or selected superclass identity");
  // Do not overwrite independently captured temporaries with an unvalidated
  // return value: failure cleanup must never retire an unrelated published Class.
  ops.Set(current.result,ops.Get(observed));work.pop_back();
 }
 return ops.Get(final_result);
}
}
