#include "platform/common/source_commit.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <map>
using namespace artdcevm::source_commit;
struct Object {int type;};
struct Test final:Services {
 mutable std::map<void*,void*> pairs,retained_pairs;
 mutable std::map<void*,std::vector<void*>> roots,rewritten_roots;
 mutable std::string events;mutable std::map<void*,uint32_t> states;
 mutable bool replaced=false;bool valid=true;Object* old=nullptr;Object* replacement=nullptr;
 bool Validate(const Plan&)const override{return valid;}
 void BeginCommit(const Plan&)const override{assert(valid);events+='b';}
 uint32_t Status(void* p)const override{return states[p];}
 void SetStatus(void* p,uint32_t value)const override{states[p]=value;events+='s';}
 void CopyFields(bool statik,void* target,void*,void* source,void*)const override{assert(pairs.empty()?(target==replacement&&source==old):pairs[source]==target);events+=statik?'c':'i';}
 void* ObjectClass(void* p)const override{return p;}
 void MarkClassAndMethodsObsolete(void*,void*)const override{events+='m';}
 void UpdateReflectiveReferences(const Plan&)const override{events+='v';}
 void ForceDeoptimizeFrames()const override{events+='f';}
 void ReplaceObjectReferences()const override{assert(events.back()=='f');events+='r';replaced=true;if(!rewritten_roots.empty())roots=rewritten_roots;}
 void RestoreDeclaringClasses(const Plan&)const override{assert(replaced&&events.back()=='r');events+='o';}
 void* NewClassAfterReplacement(void* array,size_t index)const override{assert(replaced);events+='n';if(!roots.empty()){assert(roots.count(array)&&index<roots[array].size());return roots[array][index];}assert(index==0&&array==this);return replacement;}
 void RetainOldClass(void* next,void* previous)const override{assert(retained_pairs.empty()?(next==replacement&&previous==old):retained_pairs[previous]==next);events+='h';}
 void InvalidateCompiledCode()const override{events+='j';}
 void DeoptimizeEverythingForAbstractTransition()const override{events+='a';}
 void ClearInterpreterCaches()const override{events+='x';}
};
int main(){
 Object old{},replacement{};Test services;services.old=&old;services.replacement=&replacement;
 services.states[&old]=15;services.states[&replacement]=10;
 Plan plan{&old,&services,{&old},{&replacement},{&old},{&replacement},true};
 int observed=0;CommitObserver observer{&observed,[](const Plan&,void* state) noexcept {++*static_cast<int*>(state);}};
 assert(Run(plan,services,observer)&&observed==1);assert(services.events=="bscimvfronhjax");assert(services.states[&replacement]==15);
 services.events.clear();services.valid=false;assert(!Run(plan,services,observer)&&services.events.empty()&&observed==1);
 services.valid=true;plan.new_classes.clear();assert(!Run(plan,services)&&services.events.empty());
 Object old2{},next2{},moved1{},moved2{},decoy{};int array1,array2;
 Test batch;batch.pairs={{&old,&replacement},{&old2,&next2}};batch.retained_pairs={{&old,&moved1},{&old2,&moved2}};
 batch.roots={{&array1,{&replacement}},{&array2,{&decoy,&decoy,&next2}}};
 batch.rewritten_roots={{&array1,{&moved1}},{&array2,{&decoy,&decoy,&moved2}}};
 Plan multiple{&old,&array1,{&old,&old2},{&replacement,&next2},{&old,&old2},{&replacement,&next2},false};
 multiple.class_root_origins={{&array1,0},{&array2,2}};
 assert(Run(multiple,batch));assert(batch.events=="bcciimmvfronhnhjx");
 batch.events.clear();multiple.class_root_origins.pop_back();assert(!Run(multiple,batch)&&batch.events.empty());
 multiple.class_root_origins={{&array1,0},{nullptr,2}};assert(!Run(multiple,batch)&&batch.events.empty());
 puts("Source commit: original transaction order, roots reread after replacement, invalid plans rejected before mutation passed");
}
