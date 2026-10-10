#include "platform/common/native_method_handle_public_inputs.h"
#include <cassert>
#include <cstring>
#include <map>
#include <iostream>
using namespace artdcevm;
namespace {
JNIEnv env{};int input,input_class,array,sample_class,handle_id,member_id,kind_id,array_id;
struct Sample {int handle,member;};Sample fixtures[9]{};
bool pending=false,inject_pending=false;unsigned calls=0,fail_call=0;int array_count=9,negative_kind=-1;
std::map<jobject,unsigned> locals,globals;
bool Step(){assert(!pending);++calls;const bool fail=calls==fail_call;if(fail&&inject_pending)pending=true;return fail;}
jobject Local(jobject r,bool fail){if(fail&&!inject_pending)return nullptr;if(r)++locals[r];return r;}
jclass Class(JNIEnv*,jobject object){const bool fail=Step();return static_cast<jclass>(Local(reinterpret_cast<jobject>(object==reinterpret_cast<jobject>(&input)?&input_class:&sample_class),fail));}
jfieldID Field(JNIEnv*,jclass c,const char*n,const char*s){const bool fail=Step();void*id=nullptr;
 if(c==reinterpret_cast<jclass>(&input_class)){assert(!std::strcmp(n,"receiverHandleSamples")&&!std::strcmp(s,"[Lcom/github/ytlog/artdcevm/internal/MethodHandleReceiverOracle$Sample;"));id=&array_id;}
 else{assert(c==reinterpret_cast<jclass>(&sample_class));if(!std::strcmp(n,"handle")){assert(!std::strcmp(s,"Ljava/lang/invoke/MethodHandle;"));id=&handle_id;}else if(!std::strcmp(n,"member")){assert(!std::strcmp(s,"Ljava/lang/reflect/Member;"));id=&member_id;}else{assert(!std::strcmp(n,"kind")&&!std::strcmp(s,"I"));id=&kind_id;}}
 return fail&&!inject_pending?nullptr:reinterpret_cast<jfieldID>(id);
}
jobject Object(JNIEnv*,jobject object,jfieldID id){const bool fail=Step();jobject result=nullptr;if(object==reinterpret_cast<jobject>(&input)){assert(id==reinterpret_cast<jfieldID>(&array_id));result=reinterpret_cast<jobject>(&array);}else{auto*sample=reinterpret_cast<Sample*>(object);assert(sample>=fixtures&&sample<fixtures+9);if(id==reinterpret_cast<jfieldID>(&handle_id))result=reinterpret_cast<jobject>(&sample->handle);else{assert(id==reinterpret_cast<jfieldID>(&member_id));result=reinterpret_cast<jobject>(&sample->member);}}
 return Local(result,fail);
}
jobject Global(JNIEnv*,jobject object){const bool fail=Step();if(fail&&!inject_pending)return nullptr;++globals[object];return object;}
void Drop(std::map<jobject,unsigned>&refs,jobject object){auto i=refs.find(object);assert(i!=refs.end()&&i->second);if(!--i->second)refs.erase(i);}
void Reset(){assert(locals.empty()&&globals.empty());pending=false;calls=0;fail_call=0;inject_pending=false;array_count=9;negative_kind=-1;}
}
int main(){
 JNINativeInterface_ table{};env.functions=&table;
 table.ExceptionCheck=[](JNIEnv*){return jboolean(pending);};table.GetObjectClass=Class;table.GetFieldID=Field;table.GetObjectField=Object;table.NewGlobalRef=Global;
 table.DeleteLocalRef=[](JNIEnv*,jobject object){Drop(locals,object);};table.DeleteGlobalRef=[](JNIEnv*,jobject object){Drop(globals,object);};
 table.GetArrayLength=[](JNIEnv*,jarray object){assert(object==reinterpret_cast<jarray>(&array));bool fail=Step();return fail&&!inject_pending?-1:array_count;};
 table.GetObjectArrayElement=[](JNIEnv*,jobjectArray object,jsize index){assert(object==reinterpret_cast<jobjectArray>(&array)&&index>=0&&index<9);bool fail=Step();return Local(reinterpret_cast<jobject>(&fixtures[index]),fail);};
 table.GetIntField=[](JNIEnv*,jobject object,jfieldID id){assert(id==reinterpret_cast<jfieldID>(&kind_id));auto*sample=reinterpret_cast<Sample*>(object);auto index=sample-fixtures;assert(index>=0&&index<9);bool fail=Step();return ((fail&&!inject_pending)||negative_kind==index)?-1:jint(index);};
 unsigned boundaries=0;
 {receiver_guard::MethodHandlePublicInputs owner(&env);assert(!owner.Samples());assert(owner.Read(reinterpret_cast<jobject>(&input)));auto*samples=owner.Samples();assert(samples&&samples->size()==9&&globals.size()==18&&locals.empty());for(size_t i=0;i<9;++i)assert((*samples)[i].handle==reinterpret_cast<jobject>(&fixtures[i].handle)&&(*samples)[i].reflected_member==reinterpret_cast<jobject>(&fixtures[i].member)&&(*samples)[i].kind==i);boundaries=calls;assert(!owner.Read(reinterpret_cast<jobject>(&input))&&owner.Samples()==samples&&calls==boundaries);}
 Reset();
 for(unsigned at=1;at<=boundaries;++at)for(bool with_pending:{false,true}){
  fail_call=at;inject_pending=with_pending;
  {receiver_guard::MethodHandlePublicInputs owner(&env);assert(!owner.Read(reinterpret_cast<jobject>(&input))&&!owner.Samples()&&calls==at&&pending==with_pending&&locals.empty());}
  assert(globals.empty());Reset();
 }
 for(int count:{0,8,10,-1}){array_count=count;{receiver_guard::MethodHandlePublicInputs owner(&env);assert(!owner.Read(reinterpret_cast<jobject>(&input))&&!owner.Samples()&&locals.empty());}Reset();}
 for(int at=0;at<9;++at){negative_kind=at;{receiver_guard::MethodHandlePublicInputs owner(&env);assert(!owner.Read(reinterpret_cast<jobject>(&input))&&!owner.Samples()&&locals.empty());}Reset();}
 pending=true;{receiver_guard::MethodHandlePublicInputs owner(&env);assert(!owner.Read(reinterpret_cast<jobject>(&input))&&!owner.Samples()&&calls==0&&pending);}Reset();
 {receiver_guard::MethodHandlePublicInputs owner(nullptr);assert(!owner.Read(reinterpret_cast<jobject>(&input))&&!owner.Samples());}
 {receiver_guard::MethodHandlePublicInputs owner(&env);assert(!owner.Read(nullptr)&&!owner.Samples()&&calls==0);}Reset();
 std::cout<<"Actual MethodHandlePublicInputs public JNI producer: nine independent handle/member/kind samples, eighteen rooted globals, "<<boundaries<<" JNI boundaries x null/value-pending refusal, exact declarations, wrong length/kind and incoming exception; no partial publication, local/global balanced PASS"<<std::endl;
}
