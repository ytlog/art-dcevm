#pragma once
#include "platform/common/source_cache_role_catalog.h"
#include "platform/common/native_hierarchy_cache.h"
#include "platform/common/source_handle_roots.h"
#include "platform/common/art_method_metadata.h"
#include "platform/common/field_mirror_calibration.h"
#include <jvmti.h>
namespace artdcevm::source_cache_public_roles {
struct DexIdentity {const void* file=nullptr;const unsigned char* begin=nullptr;const unsigned char* data=nullptr;size_t data_size=0;int32_t class_def=-1,type_index=-1;};
struct Bindings {
 void* owner=nullptr;
 void* (*method)(void*,jmethodID)=nullptr;
 void* (*field)(void*,jfieldID)=nullptr;
 abi::common::FromReflectedMethod reflected_method=nullptr;
 field_mirror::NamedField reflected_field=nullptr;
 const modern_field_metadata::FieldMetadata* metadata=nullptr;
 method_metadata::Layout method_layout{};
 const source_handle_roots::Service* roots=nullptr;
};
// Public identities are prepared before snapshots; all movable Classes and
// reflection carriers remain owned globals until Clear. Never read cache slots.
class Public {
 public:
 bool Prepare(JNIEnv*,jvmtiEnv*,const source_cache_role_catalog::Catalog&,const std::vector<unsigned char>&,jclass,jobject,const Bindings&,std::string*);
 void Clear(JNIEnv*)noexcept;
 bool Ready()const noexcept{return ready_;}
 struct TypeRole {jclass klass=nullptr;std::string descriptor;};
 struct MemberRole {jclass declaring=nullptr;jobject reflected=nullptr;jmethodID method=nullptr;jfieldID field=nullptr;void* native=nullptr;uint32_t flags=0,native_index=0;DexIdentity dex{};std::string name,descriptor;};
 const std::vector<TypeRole>& Types()const{return types_;}
 const std::vector<MemberRole>& Fields()const{return fields_;}
 const std::vector<MemberRole>& Methods()const{return methods_;}
 const Bindings& Native()const{return bindings_;}
 jclass Anchor()const{return anchor_;}jobject Cache()const{return cache_;}jobject Loader()const{return loader_;}
 const evolution::MappedDexMetadata& Dex()const{return dex_;}
 const DexIdentity& NamespaceDex()const{return namespace_dex_;}
 private:
 static bool Observe(const runnable_jni::Context&,void*);
 std::vector<TypeRole>types_;std::vector<MemberRole>fields_,methods_;
 std::vector<unsigned char> original_;Bindings bindings_{};jclass anchor_=nullptr;jobject cache_=nullptr,loader_=nullptr;
 evolution::MappedDexMetadata dex_{};DexIdentity namespace_dex_{};bool ready_=false;
};
class Rooted final:public native_hierarchy_cache::CurrentRoles {
 public:
 bool Import(const Public&,const runnable_jni::Context&,const source_handle_roots::Service&,source_handle_roots::Pool&,std::string*);
 bool Cache(const native_hierarchy_cache::SourceMapper&,void*,native_hierarchy_cache::Counts)const noexcept override;
 bool Type(const native_hierarchy_cache::SourceMapper&,uint32_t,void*)const noexcept override;
 bool Field(const native_hierarchy_cache::SourceMapper&,uint32_t,void*)const noexcept override;
 bool Method(const native_hierarchy_cache::SourceMapper&,uint32_t,void*)const noexcept override;
 private:
 bool attempted_=false;const Public* public_=nullptr;void* cache_=nullptr,*anchor_=nullptr,*loader_=nullptr;
 std::vector<void*>types_,fields_,methods_;
};
}
