#include "platform/common/native_removal_policy.h"
#include <cassert>
#include <iostream>
using namespace artdcevm;
int main(){
 evolution::ClassSchema old{"LFixture;","Ljava/lang/Object;",{},1,
  {{"value","I",1},{"removed","I",1}},{{"<init>","()V",1},{"<clinit>","()V",8},{"kept","()I",1},{"removed","()I",1}}};
 std::string error;auto next=old;
 assert(!native_owned_removal::ValidateRemoval(old,next,true,&error));
 next.methods.pop_back();assert(native_owned_removal::ValidateRemoval(old,next,false,&error));
 next.fields.pop_back();assert(!native_owned_removal::ValidateRemoval(old,next,false,&error));
 assert(native_owned_removal::ValidateRemoval(old,next,true,&error));
 auto bad=next;bad.methods.erase(bad.methods.begin());assert(!native_owned_removal::ValidateRemoval(old,bad,true,&error));
 bad=next;bad.methods.erase(bad.methods.begin()+1);assert(!native_owned_removal::ValidateRemoval(old,bad,true,&error));
 bad=next;bad.methods.back().access|=8;assert(!native_owned_removal::ValidateRemoval(old,bad,true,&error));
 bad=next;bad.fields.back().descriptor="J";assert(!native_owned_removal::ValidateRemoval(old,bad,true,&error));
 bad=next;bad.superclass="LOther;";assert(!native_owned_removal::ValidateRemoval(old,bad,true,&error));
 bad=next;bad.interfaces.push_back("LIface;");assert(!native_owned_removal::ValidateRemoval(old,bad,true,&error));
 bad=next;bad.methods.push_back({"added","()I",1});assert(!native_owned_removal::ValidateRemoval(old,bad,true,&error));
 old.methods[2].access=0x20001;next.methods[2].access=0x21;
 assert(native_owned_removal::ValidateRemoval(old,next,true,&error));
 // Complete field-only evolution keeps all methods and both surviving values.
 evolution::ClassSchema field_old{"LFields;","Ljava/lang/Object;",{},1,
  {{"keep","I",1},{"staticKeep","I",9}},{{"<init>","()V",1},{"<clinit>","()V",8},{"compute","()I",1}}};
 const char* descriptors[]={"Z","B","C","S","I","J","F","D","Ljava/lang/Object;"};
 for(size_t i=0;i<9;i++) {
   field_old.fields.push_back({"instance"+std::to_string(i),descriptors[i],1});
   field_old.fields.push_back({"static"+std::to_string(i),descriptors[i],9});
 }
 auto fields=field_old;fields.fields.resize(2);
 assert(native_owned_removal::ValidateRemoval(field_old,fields,true,&error)&&error.empty());
 assert(!native_owned_removal::ValidateRemoval(field_old,fields,false,&error)&&!error.empty());
 for(uint32_t modifier:{0x8u,0x10u,0x40u,0x80u}) {
   auto changed=fields;changed.fields[0].access^=modifier;
   assert(!native_owned_removal::ValidateRemoval(field_old,changed,true,&error));
 }
 auto repeated=fields;repeated.fields.push_back(fields.fields[0]);
 assert(!native_owned_removal::ValidateRemoval(field_old,repeated,true,&error));
 repeated=fields;repeated.methods.push_back(fields.methods.back());
 assert(!native_owned_removal::ValidateRemoval(field_old,repeated,true,&error));
 auto missing=fields;missing.fields.push_back({"absent","I",1});
 assert(!native_owned_removal::ValidateRemoval(field_old,missing,true,&error));
 auto different=fields;different.fields[0].descriptor="J";
 assert(!native_owned_removal::ValidateRemoval(field_old,different,true,&error));
 auto body_only=field_old;assert(!native_owned_removal::ValidateRemoval(field_old,body_only,true,&error));
 // Descriptor identity distinguishes legal overloaded methods, rather than
 // collapsing unrelated overloads into a name-only match.
 field_old.methods.push_back({"compute","(I)I",1});
 assert(native_owned_removal::ValidateRemoval(field_old,fields,true,&error));
 std::cout<<"strict full field removal, duplicate/missing identity and immutable modifier policy passed\n";
}
