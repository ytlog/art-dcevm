#include "platform/common/source_commit.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <map>
using namespace artdcevm::source_commit;
struct Object {int type;};
struct Test final:Services {
 mutable std::string events;mutable std::map<void*,uint32_t> states;
 mutable bool replaced=false;bool valid=true;Object* old=nullptr;Object* replacement=nullptr;
 bool Validate(const Plan&)const override{return valid;}
 uint32_t Status(void* p)const override{return states[p];}
 void SetStatus(void* p,uint32_t value)const override{states[p]=value;events+='s';}
 void CopyFields(bool statik,void* target,void*,void* source,void*)const override{assert(target==replacement&&source==old);events+=statik?'c':'i';}
 void* ObjectClass(void* p)const override{return p;}
 void MarkClassAndMethodsObsolete(void*,void*)const override{events+='m';}
 void UpdateReflectiveReferences(const Plan&)const override{events+='v';}
 void ForceDeoptimizeFrames()const override{events+='f';}
 void ReplaceObjectReferences()const override{assert(events.back()=='f');events+='r';replaced=true;}
 void* NewClassAfterReplacement(void* array,size_t index)const override{assert(replaced&&index==0&&array==this);events+='n';return replacement;}
 void RetainOldClass(void* next,void* previous)const override{assert(next==replacement&&previous==old);events+='h';}
 void InvalidateCompiledCode()const override{events+='j';}
 void DeoptimizeEverythingForAbstractTransition()const override{events+='a';}
 void ClearInterpreterCaches()const override{events+='x';}
};
int main(){
 Object old{},replacement{};Test services;services.old=&old;services.replacement=&replacement;
 services.states[&old]=15;services.states[&replacement]=10;
 Plan plan{&old,&services,{&old},{&replacement},{&old},{&replacement},true};
 assert(Run(plan,services));assert(services.events=="scimvfrnhjax");assert(services.states[&replacement]==15);
 services.events.clear();services.valid=false;assert(!Run(plan,services)&&services.events.empty());
 services.valid=true;plan.new_classes.clear();assert(!Run(plan,services)&&services.events.empty());
 puts("Source commit: original transaction order, roots reread after replacement, invalid plans rejected before mutation passed");
}
