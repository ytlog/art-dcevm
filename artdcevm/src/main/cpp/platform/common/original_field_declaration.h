#pragma once
#include <jni.h>
#include <jvmti.h>
#include <string>
#include <cstring>
#include "platform/common/field_array_view.h"
#include "platform/common/field_copy.h"
#include "platform/common/jvmti_field_flags.h"
namespace artdcevm::original_field_declaration {
// Caller owns the exact Class root for the entire request. IDs are opaque;
// neither their encoding nor a reflection visibility list supplies a pointer.
struct Declaration {std::string name,descriptor;uint32_t flags=0;};
inline bool Read(JNIEnv* env,jvmtiEnv* ti,jclass owner,jfieldID id,bool statik,Declaration* out,std::string* error){
 const auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(!env||!ti||!owner||!id||!out||env->ExceptionCheck())return fail("Original Field declaration context invalid or pending");
 struct Buffer{jvmtiEnv* ti;unsigned char* p=nullptr;~Buffer(){if(p)ti->Deallocate(p);}};
 struct Local{JNIEnv* env;jclass p=nullptr;~Local(){if(p)env->DeleteLocalRef(p);}};
 Local declared{env};auto result=ti->GetFieldDeclaringClass(owner,id,&declared.p);
 if(env->ExceptionCheck()||result!=JVMTI_ERROR_NONE||!declared.p)return fail("Original Field declaring Class unavailable");
 const bool same=env->IsSameObject(owner,declared.p);
 if(env->ExceptionCheck()||!same)return fail("Original Field declaring namespace differs");
 Buffer name{ti},descriptor{ti},generic{ti};result=ti->GetFieldName(owner,id,reinterpret_cast<char**>(&name.p),reinterpret_cast<char**>(&descriptor.p),reinterpret_cast<char**>(&generic.p));
 if(env->ExceptionCheck()||result!=JVMTI_ERROR_NONE||!name.p||!descriptor.p||!name.p[0]||!descriptor.p[0])return fail("Original Field name/descriptor unavailable");
 jint flags=0;result=ti->GetFieldModifiers(owner,id,&flags);
 if(env->ExceptionCheck()||result!=JVMTI_ERROR_NONE||((flags&8)!=0)!=statik)return fail("Original Field modifiers/storage differ");
 *out={reinterpret_cast<char*>(name.p),reinterpret_cast<char*>(descriptor.p),jvmti_field_flags::Public(uint32_t(flags))};return true;
}
// The identity leaf must be calibrated metadata under current mutator access.
// Inspect every selected physical member, including duplicates, before grant.
template<class Identity> void* Match(const field_array::View& table,void* owner,const Declaration& declaration,Identity identity,const char** failure=nullptr){
 const auto fail=[&](const char* text)->void*{if(failure)*failure=text;return nullptr;};
 if(!owner)return fail("native declaring Class absent");void* found=nullptr;
 for(uint32_t i=0;i<table.count;++i){if(!table.Selected(i))continue;void* field=table.At(i);field_copy::CopyFieldView text{};void* declaring=nullptr;
  if(!identity(field,&text,&declaring))return fail("native field text/owner leaf rejected");
  if(text.field!=field)return fail("native field text identity differs");
  if(!text.name||!text.descriptor)return fail("native field name/descriptor absent");
  if(declaring!=owner)return fail("native field declaring Class differs");
  if(declaration.name==text.name&&declaration.descriptor==text.descriptor&&declaration.flags==jvmti_field_flags::Public(field_array::Flags(field))){if(found)return fail("native exact declaration duplicated");found=field;}
 }
 if(!found)return fail("native exact name/descriptor/modifiers declaration absent");
 if(failure)*failure=nullptr;return found;
}
}
