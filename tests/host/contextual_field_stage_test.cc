#include "platform/common/contextual_field_stage.h"
#include <cassert>
#include <iostream>
namespace {int releases=0,prunes=0,publications=0;void* retired=nullptr;bool native_scope=false;}
namespace artdcevm::retirement {void RetiredFieldRegistry::Retire(void* field){assert(native_scope);retired=field;}}
namespace artdcevm::contextual_field_id {
struct Proof{void* field;bool published=false;};
std::shared_ptr<Proof> Prepare(JNIEnv*,jobject field,jfieldID,std::string* error){assert(!native_scope);if(!field){if(error)*error="Unproven public namespace";return nullptr;}return std::make_shared<Proof>(Proof{field,false});}
void* NativeField(const std::shared_ptr<Proof>& p){return p?p->field:nullptr;}
void PublishRetired(const std::shared_ptr<Proof>& p,retirement::RetiredFieldRegistry&){assert(native_scope&&retired==p->field);p->published=true;++publications;}
void Release(JNIEnv*,std::shared_ptr<Proof>* p){assert(!native_scope);if(*p&&!(*p)->published)++releases;p->reset();}
void Prune(JNIEnv*,retirement::RetiredFieldRegistry&){assert(!native_scope);++prunes;}
}
int main(){using namespace artdcevm;retirement::RetiredFieldRegistry registry;int a,b,c;std::string error;
 {contextual_field_id::Stage stage{nullptr,registry};
  assert(!stage.Add(nullptr,nullptr,&error)&&!error.empty());
  assert(stage.Add(reinterpret_cast<jobject>(&a),nullptr,&error));
  assert(stage.Add(reinterpret_cast<jobject>(&b),nullptr,&error));
  assert(!stage.Add(reinterpret_cast<jobject>(&a),nullptr,&error));
  assert(!stage.Covers({&a})&&!stage.Covers({&a,&c})&&!stage.Covers({&a,&a}));
  assert(stage.Covers({&b,&a}));assert(publications==0&&retired==nullptr);
  native_scope=true;stage.Retire(&a);native_scope=false;
  assert(publications==1&&stage.proofs[0]->published&&!stage.proofs[1]->published);
 }
 assert(releases==2&&prunes==1);std::cout<<"Contextual field stage: complete table, duplicate/missing identity, native publication ordering and ordinary JNI release PASS\n";
}
