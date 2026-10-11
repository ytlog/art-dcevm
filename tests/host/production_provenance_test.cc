#include "platform/common/production_provenance.h"
#include <cassert>
#include <iostream>
#include <map>
#include <string>
using namespace artdcevm::production_bootstrap;
namespace {
struct Class {std::string descriptor;int loader,generation=1;bool live=true;};
struct Reference {Class* object;int generation;bool weak;};
std::map<jobject,Class*> classes;std::map<jobject,Reference*> references;
int weak_created=0,weak_deleted=0,local_created=0,local_deleted=0,strong_created=0;
Class* Resolve(jobject value){auto klass=classes.find(value);if(klass!=classes.end())return klass->second;
 auto found=references.find(value);if(found==references.end())return nullptr;
 auto& ref=*found->second;return ref.object->live&&ref.object->generation==ref.generation?ref.object:nullptr;}
jboolean Exception(JNIEnv*){return JNI_FALSE;}
jweak NewWeak(JNIEnv*,jobject value){auto* c=Resolve(value);if(!c)return nullptr;auto* r=new Reference{c,c->generation,true};auto result=reinterpret_cast<jweak>(r);references[result]=r;++weak_created;return result;}
jobject NewLocal(JNIEnv*,jobject value){auto* c=Resolve(value);if(!c)return nullptr;auto* r=new Reference{c,c->generation,false};auto result=reinterpret_cast<jobject>(r);references[result]=r;++local_created;return result;}
void DeleteWeak(JNIEnv*,jweak value){assert(references.at(value)->weak);delete references.at(value);references.erase(value);++weak_deleted;}
void DeleteLocal(JNIEnv*,jobject value){assert(!references.at(value)->weak);delete references.at(value);references.erase(value);++local_deleted;}
jboolean Same(JNIEnv*,jobject a,jobject b){return Resolve(a)==Resolve(b)?JNI_TRUE:JNI_FALSE;}
jobject NewStrong(JNIEnv*,jobject){++strong_created;return nullptr;}
}
int main(){
 JNINativeInterface_ functions{};functions.ExceptionCheck=Exception;functions.NewWeakGlobalRef=NewWeak;functions.DeleteWeakGlobalRef=DeleteWeak;functions.NewLocalRef=NewLocal;functions.DeleteLocalRef=DeleteLocal;functions.IsSameObject=Same;functions.NewGlobalRef=NewStrong;JNIEnv env{&functions};
 Class first{"Lsame/Name;",1},second{"Lsame/Name;",2};auto a=reinterpret_cast<jclass>(&first),b=reinterpret_cast<jclass>(&second);classes[a]=&first;classes[b]=&second;
 Provenance records;std::vector<unsigned char> bytes{99};
 {auto pending=records.Prepare(&env,{{a,{1,2}}});assert(pending);pending->Commit();}
 assert(records.Size()==1);
 assert(!records.Find(&env,b,&bytes)&&bytes==std::vector<unsigned char>{99});
 {auto pending=records.Prepare(&env,{{b,{3,4}}});assert(pending);pending->Commit();}
 assert(records.Size()==2);
 assert(records.Find(&env,a,&bytes)&&bytes==std::vector<unsigned char>({1,2}));
 // Preparation and cancellation must leave both installed inputs unchanged.
 {auto pending=records.Prepare(&env,{{a,{9,9}},{b,{8,8}}});assert(pending);
  assert(records.Find(&env,a,&bytes)&&bytes==std::vector<unsigned char>({1,2}));
  assert(!records.Prepare(&env,{{a,{7,7}}}));}
 assert(records.Find(&env,b,&bytes)&&bytes==std::vector<unsigned char>({3,4}));
 assert(!records.Prepare(&env,{{a,{9,9}},{a,{8,8}}}));
 assert(records.Find(&env,a,&bytes)&&bytes==std::vector<unsigned char>({1,2}));
 {auto pending=records.Prepare(&env,{{a,{5,6}}});assert(pending);pending->Commit();}
 assert(records.Size()==2);
 assert(records.Find(&env,a,&bytes)&&bytes==std::vector<unsigned char>({5,6}));
 first.live=false;records.Sweep(&env);assert(records.Size()==1&&weak_deleted==1);
 assert(!records.Find(&env,a,&bytes)&&bytes==std::vector<unsigned char>({5,6}));
 // A newly loaded Class may reuse a collected object's address. Cleared weak
 // handles never identify that new generation or publish the previous DEX.
 first.generation++;first.live=true;
 assert(!records.Find(&env,a,&bytes));
 {auto pending=records.Prepare(&env,{{a,{7,8}}});assert(pending);pending->Commit();}
 assert(records.Find(&env,b,&bytes)&&bytes==std::vector<unsigned char>({3,4}));
 const auto retained=bytes;records.Invalidate();assert(!records.Trusted());
 assert(!records.Find(&env,b,&bytes)&&bytes==retained);
 assert(!records.Prepare(&env,{{a,{9,10}}}));
 records.Clear(&env);assert(records.Size()==0&&references.empty()&&!records.Trusted());
 assert(weak_created==weak_deleted&&local_created==local_deleted&&strong_created==0);
 std::cout<<"Weak production provenance: loader identity, multi-generation replacement, cleared weak pruning, address reuse, bounded pins and zero permanent strong references passed\n";
}
