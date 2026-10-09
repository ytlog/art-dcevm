#include "platform/common/prepared_field_copy.h"
#include <algorithm>
#include <cstring>
namespace artdcevm::field_copy {
bool PreparedPlan::Prepare(const FieldCopyAccess& ops,bool statik,void* target,void* source,PreparedPlan* output,std::string* error,const std::vector<ClassPair>& pairs){
 auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(!output||!source||!target||source==target||!ops.klass||!ops.field||!ops.read||!ops.write||!ops.transfer_monitor||!ops.barrier)return fail("Incomplete prepared field-copy source");
 PreparedPlan plan;plan.is_static_=statik;
 auto capture=[&](void* root,std::vector<Class>* classes){
  for(void* cursor=root;cursor;){
   if(classes->size()>4096||std::any_of(classes->begin(),classes->end(),[&](const auto& c){return c.identity==cursor;}))return false;
   const auto facts=ops.klass(cursor,statik);
   if(!facts.descriptor||!*facts.descriptor||facts.field_count>1000000)return false;
   Class captured{cursor,facts.parent,facts.loader,facts.descriptor,facts.is_object,{}};
   for(size_t i=0;i<facts.field_count;++i){const auto f=ops.field(cursor,statik,i);
    if(!f.field||!f.name||!*f.name||!f.descriptor||!*f.descriptor||!std::strchr("ZBCSIJFDL[",f.descriptor[0])||
       std::any_of(captured.fields.begin(),captured.fields.end(),[&](const auto& old){return old.identity==f.field||(old.name==f.name&&old.descriptor==f.descriptor);}))return false;
    captured.fields.push_back({f.field,f.name,f.descriptor});}
   classes->push_back(std::move(captured));
   if(statik||facts.is_object||!facts.parent)break;cursor=facts.parent;
  }
  return true;
 };
 if(!capture(source,&plan.source_)||!capture(target,&plan.target_))return fail("Prepared field-copy table/ancestor identity invalid");
 for(const auto& old:plan.source_){if(old.object)continue;
  const Class* destination=nullptr;
  for(const auto& next:plan.target_)if((old.identity==next.identity||(old.identity==source&&next.identity==target)||
      std::any_of(pairs.begin(),pairs.end(),[&](const auto& pair){return pair.before==old.identity&&pair.after==next.identity;}))&&old.loader==next.loader&&old.descriptor==next.descriptor){if(destination)return fail("Ambiguous native declaring Class identity");destination=&next;}
  if(!destination)return fail("Prepared field-copy ancestor lacks exact original/replacement Class correspondence");
  for(const auto& field:old.fields){plan.clear_.push_back({field.identity,nullptr,field.descriptor[0]});
   if(destination)for(const auto& next:destination->fields)if(field.name==next.name&&field.descriptor==next.descriptor)
    plan.copies_.push_back({field.identity,next.identity,field.descriptor[0]});}
 }
 if(!plan.Validate(ops))return fail("Prepared field-copy identities changed during preflight");
 *output=std::move(plan);if(error)error->clear();return true;
}
bool PreparedPlan::Validate(const FieldCopyAccess& ops)const{
 if(!ops.klass||!ops.field||source_.empty()||target_.empty())return false;
 for(const auto* group:{&source_,&target_})for(const auto& c:*group){const auto facts=ops.klass(c.identity,is_static_);
  if(facts.parent!=c.parent||facts.loader!=c.loader||!facts.descriptor||c.descriptor!=facts.descriptor||facts.is_object!=c.object||facts.field_count!=c.fields.size())return false;
  for(size_t i=0;i<c.fields.size();++i){const auto f=ops.field(c.identity,is_static_,i);const auto& expected=c.fields[i];
   if(f.field!=expected.identity||!f.name||!f.descriptor||expected.name!=f.name||expected.descriptor!=f.descriptor)return false;}
 }
 return true;
}
void PreparedPlan::CopyAndClear(const FieldCopyAccess& ops,void* target,void* source)const noexcept{
 for(const auto& copy:copies_)ops.write(target,copy.target,copy.type,ops.read(source,copy.source,copy.type));
 ops.transfer_monitor(target,source);ops.barrier(target);
 for(const auto& clear:clear_)ops.write(source,clear.source,clear.type,0);
 ops.barrier(source);
}
}
