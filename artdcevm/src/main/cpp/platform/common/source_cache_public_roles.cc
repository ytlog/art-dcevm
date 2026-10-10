#include "platform/common/source_cache_public_roles.h"
#include "platform/common/runnable_executor.h"
#include "platform/common/readable_mapping_scope.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/abi/arm64/android11/dex_cache.h"
#include "platform/abi/arm64/android14/dex_cache.h"
#include <algorithm>
#include <cstring>
namespace artdcevm::source_cache_public_roles {
namespace {
struct ObserveRequest {Public* roles;std::string* error;};
// Each instance belongs to just one native traversal after its last JNI call.
// It expires before the next decoder/reflection call; readability never grants
// lifetime. Actual strong roots are borrowed again before creating this view.
struct ImmediateRead {
 method_identity::ImmediateReadableRegions regions;
 modern_field_metadata::ReadContext context{&regions,[](void*raw,const void*p,size_t*out){return static_cast<method_identity::ImmediateReadableRegions*>(raw)->Extent(p,out);}};
};
bool Fail(std::string*e,const char*s){if(e)*e=s;return false;}
// Capture only the fixed, independently calibrated ownership prefixes. The
// stopped path never reparses DEX or produces a diagnostic allocation. DexFile
// and its original mapping remain immutable while the owned Class stays rooted.
bool ReadDex(const modern_field_metadata::FieldMetadata&metadata,void*klass,const modern_field_metadata::ReadContext*context,DexIdentity*out)noexcept{
 if(!klass||!out)return false;
 size_t extent=0;class_metadata::Identity identity{};
 const auto&layout=metadata.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 if(!modern_field_metadata::ReadMappingExtent(context,klass,&extent)||!class_metadata::ReadIdentity(layout,klass,extent,&identity)||identity.class_def_index<0)return false;
 void*cache=metadata.Reference(klass,offsetof(class_metadata::Class11,dex_cache));
 static_assert(offsetof(abi::arm64::android11::LegacyCache,dex_file)==offsetof(abi::arm64::android14::DualCache,dex_file));
 constexpr size_t offset=offsetof(abi::arm64::android11::LegacyCache,dex_file);
 if(!cache||!modern_field_metadata::ReadMappingExtent(context,cache,&extent)||extent<offset+8)return false;
 uint64_t raw=0;std::memcpy(&raw,static_cast<const unsigned char*>(cache)+offset,8);auto*file=reinterpret_cast<void*>(uintptr_t(raw));dex_mapping_abi::Prefix prefix{};
 if(!file||!modern_field_metadata::ReadMappingExtent(context,file,&extent)||extent<sizeof(prefix))return false;
 std::memcpy(&prefix,file,sizeof(prefix));
 if(!prefix.begin||!prefix.data_begin||!modern_field_metadata::ReadMappingExtent(context,prefix.begin,&extent)||extent<0x70||!modern_field_metadata::ReadMappingExtent(context,prefix.data_begin,&extent)||extent<prefix.data_size)return false;
 *out={file,prefix.begin,prefix.data_begin,prefix.data_size,identity.class_def_index,identity.dex_type_index};return true;
}
bool SameDex(const DexIdentity&a,const DexIdentity&b)noexcept{return a.file==b.file&&a.begin==b.begin&&a.data==b.data&&a.data_size==b.data_size&&a.class_def==b.class_def&&a.type_index==b.type_index;}
struct Local {JNIEnv*env;jobject p;~Local(){if(p)env->DeleteLocalRef(p);}};
template<class T>struct Memory {jvmtiEnv*ti;T*p;~Memory(){if(p)ti->Deallocate(reinterpret_cast<unsigned char*>(p));}};
jclass Resolve(JNIEnv*e,const std::string&d,jobject loader,jclass class_type,jmethodID for_name){
 if(d.empty())return nullptr;
 if(d.size()==1){const char*wrapper=nullptr;switch(d[0]){case'V':wrapper="java/lang/Void";break;case'Z':wrapper="java/lang/Boolean";break;case'B':wrapper="java/lang/Byte";break;case'C':wrapper="java/lang/Character";break;case'S':wrapper="java/lang/Short";break;case'I':wrapper="java/lang/Integer";break;case'J':wrapper="java/lang/Long";break;case'F':wrapper="java/lang/Float";break;case'D':wrapper="java/lang/Double";break;default:return nullptr;}
  auto k=e->FindClass(wrapper);Local root{e,k};auto id=k?e->GetStaticFieldID(k,"TYPE","Ljava/lang/Class;"):nullptr;return id?static_cast<jclass>(e->GetStaticObjectField(k,id)):nullptr;
 }
 if(d.front()!='L'&&d.front()!='[')return nullptr;
 std::string name=d;if(name.front()=='L'){if(name.back()!=';')return nullptr;name=name.substr(1,name.size()-2);}std::replace(name.begin(),name.end(),'/','.');
 auto text=e->NewStringUTF(name.c_str());Local root{e,text};return text?static_cast<jclass>(e->CallStaticObjectMethod(class_type,for_name,text,JNI_FALSE,loader)):nullptr;
}
// Discover staticness from public declarations, then JNI performs actual member
// resolution on the immutable referenced owner (including inherited members).
bool Flags(JNIEnv*e,jvmtiEnv*ti,jclass start,const source_cache_role_catalog::Member&r,bool method,jint*out,unsigned depth=0){
 if(depth>256||!start||e->ExceptionCheck())return false;
 jint n=0;
 if(method){jmethodID*ids=nullptr;if(ti->GetClassMethods(start,&n,&ids)!=JVMTI_ERROR_NONE)return false;Memory<jmethodID>mem{ti,ids};
  for(jint i=0;i<n;++i){char*name=nullptr,*desc=nullptr;auto rc=ti->GetMethodName(ids[i],&name,&desc,nullptr);Memory<char>a{ti,name},b{ti,desc};if(rc!=JVMTI_ERROR_NONE||!name||!desc)return false;if(r.name==name&&r.descriptor==desc)return ti->GetMethodModifiers(ids[i],out)==JVMTI_ERROR_NONE;}
 }else{jfieldID*ids=nullptr;if(ti->GetClassFields(start,&n,&ids)!=JVMTI_ERROR_NONE)return false;Memory<jfieldID>mem{ti,ids};
  for(jint i=0;i<n;++i){char*name=nullptr,*desc=nullptr;auto rc=ti->GetFieldName(start,ids[i],&name,&desc,nullptr);Memory<char>a{ti,name},b{ti,desc};if(rc!=JVMTI_ERROR_NONE||!name||!desc)return false;if(r.name==name&&r.descriptor==desc)return ti->GetFieldModifiers(start,ids[i],out)==JVMTI_ERROR_NONE;}
 }
 if(r.name=="<init>"||r.name=="<clinit>")return false;
 auto parent=e->GetSuperclass(start);Local root{e,parent};if(method&&parent&&Flags(e,ti,parent,r,method,out,depth+1))return true;
 jclass*interfaces=nullptr;if(ti->GetImplementedInterfaces(start,&n,&interfaces)!=JVMTI_ERROR_NONE)return false;Memory<jclass>mem{ti,interfaces};bool found=false;
 for(jint i=0;i<n;++i){if(!found)found=Flags(e,ti,interfaces[i],r,method,out,depth+1);e->DeleteLocalRef(interfaces[i]);}return found||(!method&&parent&&Flags(e,ti,parent,r,method,out,depth+1));
}
}
void Public::Clear(JNIEnv*e)noexcept{if(e){for(auto&t:types_)if(t.klass)e->DeleteGlobalRef(t.klass);for(auto*list:{&fields_,&methods_})for(auto&m:*list){if(m.declaring)e->DeleteGlobalRef(m.declaring);if(m.reflected)e->DeleteGlobalRef(m.reflected);}for(auto root:{static_cast<jobject>(anchor_),cache_,loader_})if(root)e->DeleteGlobalRef(root);}types_.clear();fields_.clear();methods_.clear();original_.clear();anchor_=nullptr;cache_=loader_=nullptr;dex_={};ready_=false;}
bool Public::Prepare(JNIEnv*e,jvmtiEnv*ti,const source_cache_role_catalog::Catalog&catalog,const std::vector<unsigned char>&original,jclass anchor,jobject cache,const Bindings&b,std::string*error){
 Clear(e);bindings_=b;auto fail=[&](const char*s){Clear(e);return Fail(error,s);};
 if(!e||!ti||!anchor||!cache||e->ExceptionCheck()||!b.owner||!b.method||!b.field||!b.reflected_method||!b.reflected_field||!b.metadata||!b.metadata->Calibrated()||!b.roots||!b.roots->Calibrated())return fail("Public cache complete role preparation lacks independent calibrated bindings");
 try{
 original_=original;
 anchor_=static_cast<jclass>(e->NewGlobalRef(anchor));cache_=e->NewGlobalRef(cache);jobject loader=nullptr;if(ti->GetClassLoader(anchor,&loader)!=JVMTI_ERROR_NONE)return fail("Public cache loader unavailable");Local loader_root{e,loader};if(loader)loader_=e->NewGlobalRef(loader);
 if(!anchor_||!cache_||(loader&&!loader_)||e->ExceptionCheck())return fail("Public cache namespace strong roots unavailable");
 auto cls=e->FindClass("java/lang/Class");Local class_root{e,cls};auto for_name=cls?e->GetStaticMethodID(cls,"forName","(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;"):nullptr;if(!for_name||e->ExceptionCheck())return fail("Public cache Class resolution unavailable");
 for(const auto&desc:catalog.types){auto k=Resolve(e,desc,loader,cls,for_name);Local root{e,k};if(!k||e->ExceptionCheck())return fail("Immutable cache TypeId public resolution failed");char*signature=nullptr;const auto signature_result=ti->GetClassSignature(k,&signature,nullptr);Memory<char>signature_root{ti,signature};if(signature_result!=JVMTI_ERROR_NONE||!signature||desc!=signature)return fail("Immutable cache TypeId differs from independent public Class signature");auto g=static_cast<jclass>(e->NewGlobalRef(k));if(!g||e->ExceptionCheck())return fail("Immutable cache TypeId strong root failed");try{types_.push_back({g,desc});}catch(...){e->DeleteGlobalRef(g);throw;}}
 auto members=[&](const auto&references,bool method,std::vector<MemberRole>&out){for(const auto&r:references){auto k=Resolve(e,r.declaring_class,loader,cls,for_name);Local root{e,k};jint flags=0;if(!k||e->ExceptionCheck()||!Flags(e,ti,k,r,method,&flags))return false;
  MemberRole m;m.name=r.name;m.descriptor=r.descriptor;m.flags=uint32_t(flags)&0xffffu;
  if(r.declared&&m.flags!=(r.access&0xffffu))return false;
  jclass declaring=nullptr;jobject reflected=nullptr;
  if(method){
   if(r.name=="<clinit>"){jint count=0;jmethodID*ids=nullptr;if(ti->GetClassMethods(k,&count,&ids)!=JVMTI_ERROR_NONE)return false;Memory<jmethodID>mem{ti,ids};for(jint i=0;i<count;++i){char*n=nullptr,*d=nullptr;if(ti->GetMethodName(ids[i],&n,&d,nullptr)!=JVMTI_ERROR_NONE)return false;Memory<char>a{ti,n},bmem{ti,d};if(n&&d&&r.name==n&&r.descriptor==d){if(m.method)return false;m.method=ids[i];}}}
   else m.method=(flags&8)?e->GetStaticMethodID(k,r.name.c_str(),r.descriptor.c_str()):e->GetMethodID(k,r.name.c_str(),r.descriptor.c_str());
   if(!m.method||e->ExceptionCheck()||ti->GetMethodDeclaringClass(m.method,&declaring)!=JVMTI_ERROR_NONE||!declaring)return false;
   jint actual=0;if(ti->GetMethodModifiers(m.method,&actual)!=JVMTI_ERROR_NONE||(uint32_t(actual)&0xffffu)!=m.flags){e->DeleteLocalRef(declaring);return false;}
   if(r.name!="<clinit>")reflected=e->ToReflectedMethod(declaring,m.method,(flags&8)!=0);
  }else{
   m.field=(flags&8)?e->GetStaticFieldID(k,r.name.c_str(),r.descriptor.c_str()):e->GetFieldID(k,r.name.c_str(),r.descriptor.c_str());
   if(!m.field||e->ExceptionCheck()||ti->GetFieldDeclaringClass(k,m.field,&declaring)!=JVMTI_ERROR_NONE||!declaring)return false;
   jint actual=0;if(ti->GetFieldModifiers(declaring,m.field,&actual)!=JVMTI_ERROR_NONE||(uint32_t(actual)&0xffffu)!=m.flags){e->DeleteLocalRef(declaring);return false;}
   reflected=e->ToReflectedField(declaring,m.field,(flags&8)!=0);
  }
  Local declared_root{e,declaring},reflection_root{e,reflected};if(e->ExceptionCheck()||(r.name!="<clinit>"&&!reflected)||(r.declared&&!e->IsSameObject(k,declaring)))return false;
  m.declaring=static_cast<jclass>(e->NewGlobalRef(declaring));if(reflected)m.reflected=e->NewGlobalRef(reflected);
  if(!m.declaring||(reflected&&!m.reflected)||e->ExceptionCheck()){if(m.declaring)e->DeleteGlobalRef(m.declaring);if(m.reflected)e->DeleteGlobalRef(m.reflected);return false;}
  try{out.push_back(std::move(m));}catch(...){e->DeleteGlobalRef(m.declaring);if(m.reflected)e->DeleteGlobalRef(m.reflected);throw;}
 }return true;};
 if(!members(catalog.fields,false,fields_)||!members(catalog.methods,true,methods_))return fail("Immutable cache member ID public declaration resolution failed");
 ObserveRequest request{this,error};if(error)error->clear();
 auto result=runnable_executor::With(e,anchor_,Observe,&request);
 if(result!=runnable_jni::Result::kSuccess||e->ExceptionCheck()){Clear(e);if(error&&error->empty())*error="Immutable cache public/native complete role dispatch failed: result="+std::to_string(static_cast<int>(result));return false;}
 ready_=true;return true;
 }catch(...){return fail("Immutable cache public roles native allocation failed");}
}
bool Public::Observe(const runnable_jni::Context&c,void*raw){
 auto&request=*static_cast<ObserveRequest*>(raw);auto&s=*request.roles;
 auto fail=[&](const char*stage,size_t index=SIZE_MAX,const std::string&role={},const std::string&detail={}){
  if(request.error){*request.error=std::string("Immutable cache public/native role ")+stage;
   if(index!=SIZE_MAX)*request.error+=" index="+std::to_string(index);
   if(!role.empty())*request.error+=" role="+role;
   if(!detail.empty())*request.error+=" detail="+detail;
  }return false;
 };
 if(!c.env||!c.self||!c.vm||c.env->ExceptionCheck())return fail("Runnable context or pending exception");
 const auto&b=s.bindings_;abi::common::RunnableJniScope scope{c.self,c.env,c.vm};
 void*anchor=nullptr,*cache=nullptr,*loader=nullptr;
 if(!runnable_executor::DecodeStrongGlobal(c,s.anchor_,&anchor))return fail("anchor Class owned strong decode");
 if(!runnable_executor::DecodeStrongGlobal(c,s.cache_,&cache))return fail("cache owned strong decode");
 if(s.loader_&&!runnable_executor::DecodeStrongGlobal(c,s.loader_,&loader))return fail("loader owned strong decode");
 for(size_t index=0;index<s.types_.size();++index){const auto&type=s.types_[index];
  void*klass=nullptr;field_copy::CopyClassView identity{};std::string detail;
  if(!runnable_executor::DecodeStrongGlobal(c,type.klass,&klass))return fail("Type Class owned strong decode",index,type.descriptor);
  if(!b.roots->BorrowOwnedStrongGlobal(c,type.klass,&klass))return fail("Type Class current owned strong borrow",index,type.descriptor);
  ImmediateRead traversal;const auto&read=traversal.context;
  if(!b.metadata->ClassIdentityVerified(klass,type.descriptor.c_str(),nullptr,nullptr,nullptr,&identity,&detail,&read))return fail("Type native Class descriptor",index,type.descriptor,detail);
 }
 {
 if(!b.roots->BorrowOwnedStrongGlobal(c,s.anchor_,&anchor)||!b.roots->BorrowOwnedStrongGlobal(c,s.cache_,&cache)||(s.loader_&&!b.roots->BorrowOwnedStrongGlobal(c,s.loader_,&loader)))return fail("anchor/cache/loader current owned strong borrow");
 ImmediateRead traversal;const auto&read=traversal.context;
 size_t anchor_extent=0;
 const size_t anchor_prefix=b.metadata->StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy.size:class_metadata::k17.size;
 if(!traversal.regions.Extent(anchor,&anchor_extent)||anchor_extent<anchor_prefix)return fail("anchor current Class readable prefix");
 if(b.metadata->Reference(anchor,offsetof(class_metadata::Class11,dex_cache))!=cache)return fail("anchor Class exact DexCache");
 if(b.metadata->Reference(anchor,offsetof(class_metadata::Class11,class_loader))!=loader)return fail("anchor Class exact loader");
 size_t extent_dex=0;evolution::MappedDexClassIdentity identity;std::string detail;
 if(!b.metadata->DefiningClassMapping(anchor,&s.dex_,&identity,&detail,&read))return fail("anchor original defining DEX mapping",SIZE_MAX,{},detail);
 if(!s.dex_.begin||!s.dex_.size||s.dex_.size!=s.original_.size())return fail("anchor original DEX size");
 if(!traversal.regions.Extent(s.dex_.begin,&extent_dex)||extent_dex<s.dex_.size)return fail("anchor original DEX readable extent");
 if(std::memcmp(s.dex_.begin,s.original_.data(),s.dex_.size))return fail("anchor mapped DEX versus authenticated original bytes");
 if(!ReadDex(*b.metadata,anchor,&read,&s.namespace_dex_))return fail("anchor calibrated DEX ownership prefixes");
 }
 for(size_t index=0;index<s.fields_.size();++index){auto&m=s.fields_[index];const std::string role=m.name+":"+m.descriptor;
  void*declaring=nullptr,*mirror=nullptr;
  if(!runnable_executor::DecodeStrongGlobal(c,m.declaring,&declaring))return fail("Field declaring Class owned strong decode",index,role);
  if(!runnable_executor::DecodeStrongGlobal(c,m.reflected,&mirror))return fail("Field reflected carrier owned strong decode",index,role);
  void*n=b.reflected_field(mirror);
  if(!n||n!=b.field(b.owner,m.field))return fail("Field named reflected leaf versus public ID codec",index,role);
  if(!b.roots->BorrowOwnedStrongGlobal(c,m.declaring,&declaring)||!b.roots->BorrowOwnedStrongGlobal(c,m.reflected,&mirror))return fail("Field current owned declaring/carrier borrow",index,role);
  ImmediateRead traversal;const auto&read=traversal.context;
  if(b.reflected_field(mirror)!=n||b.field(b.owner,m.field)!=n)return fail("Field current reflected carrier/native ID identity",index,role);
  size_t field_extent=0;if(!traversal.regions.Extent(n,&field_extent)||field_extent<sizeof(field_array::Field))return fail("Field current native readable prefix",index,role);
  if(b.metadata->FieldDeclaringClass(n)!=declaring)return fail("Field native declaring Class",index,role);
  field_copy::CopyFieldView f;std::string detail;
  if(!b.metadata->FieldIdentity(n,&f,&detail,&read))return fail("Field native text reader",index,role,detail);
  if(!f.name||!f.descriptor||m.name!=f.name||m.descriptor!=f.descriptor)return fail("Field native name or descriptor",index,role);
  if((field_array::Flags(n)&0xffffu)!=m.flags)return fail("Field native Java modifiers",index,role);
  std::memcpy(&m.native_index,static_cast<const unsigned char*>(n)+offsetof(field_array::Field,dex_field_index),4);
  if(!ReadDex(*b.metadata,declaring,&read,&m.dex))return fail("Field calibrated declaring DEX ownership prefixes",index,role);
  m.native=n;
 }
 for(size_t index=0;index<s.methods_.size();++index){auto&m=s.methods_[index];const std::string role=m.name+m.descriptor;
  void*declaring=nullptr;
  if(!runnable_executor::DecodeStrongGlobal(c,m.declaring,&declaring))return fail("Method declaring Class owned strong decode",index,role);
  void*n=b.method(b.owner,m.method);
  if(!n)return fail("Method public ID codec",index,role);
  if(m.reflected&&b.reflected_method(scope,m.reflected)!=n)return fail("Method named reflected leaf versus public ID codec",index,role);
  if(!b.roots->BorrowOwnedStrongGlobal(c,m.declaring,&declaring))return fail("Method current owned declaring Class borrow",index,role);
  ImmediateRead traversal;const auto&read=traversal.context;
  void*current_native=b.method(b.owner,m.method);
  if(!current_native||current_native!=n)return fail("Method current public ID codec versus original/reflected identity",index,role);
  n=current_native;size_t extent=0;method_metadata::Identity id;
  if(!traversal.regions.Extent(n,&extent)||!method_metadata::ReadIdentity(b.method_layout,n,extent,&id))return fail("Method native identity declaration",index,role);
  if(b.metadata->MethodDeclaringClass(n)!=declaring)return fail("Method native declaring Class",index,role);
  if((id.access_flags&0xffffu)!=m.flags)return fail("Method native Java modifiers",index,role);
  if(!ReadDex(*b.metadata,declaring,&read,&m.dex))return fail("Method calibrated declaring DEX ownership prefixes",index,role);
  m.native_index=id.dex_method_index;
  evolution::MappedDexMetadata mapping{};evolution::MappedDexClassIdentity klass_identity{};bool matches=false;std::string detail;
  if(!b.metadata->DefiningClassMapping(declaring,&mapping,&klass_identity,&detail,&read))return fail("Method original defining DEX mapping",index,role,detail);
  if(!evolution::ReadMappedDexMethodMatches(mapping,m.native_index,m.name,m.descriptor,&matches,&detail)||!matches)return fail("Method native DEX index versus public name/signature",index,role,detail);
  m.native=n;
 }
 return true;
}
bool Rooted::Import(const Public&p,const runnable_jni::Context&c,const source_handle_roots::Service&r,source_handle_roots::Pool&pool,std::string*error){
 if(attempted_)return Fail(error,"Public cache role root import is single-attempt");
 attempted_=true;
 auto fail=[&](){types_.clear();fields_.clear();methods_.clear();cache_=anchor_=loader_=nullptr;return Fail(error,"Public cache complete role source root import rejected");};
 if(!p.Ready())return fail();
 try{
 method_identity::ScopedReadableMappings mapping;if(!mapping.Ready())return fail();
 types_.reserve(p.Types().size());fields_.reserve(p.Fields().size());methods_.reserve(p.Methods().size());
 auto add=[&](jobject g,void**out){if(!g){*out=nullptr;return true;}void*value=nullptr;if(!r.BorrowOwnedStrongGlobal(c,g,&value)||!value)return false;*out=pool.Add(value,error);return *out!=nullptr;};
 if(!add(p.Cache(),&cache_)||!add(p.Anchor(),&anchor_)||!add(p.Loader(),&loader_))return fail();
 for(const auto&t:p.Types()){void*root=nullptr;if(!add(t.klass,&root))return fail();types_.push_back(root);}
 for(const auto&m:p.Fields()){void*root=nullptr;if(!add(m.declaring,&root))return fail();fields_.push_back(root);}
 for(const auto&m:p.Methods()){void*root=nullptr;if(!add(m.declaring,&root))return fail();methods_.push_back(root);}
 public_=&p;return true;
 }catch(...){return fail();}
}
bool Rooted::Cache(const native_hierarchy_cache::SourceMapper&m,void*root,native_hierarchy_cache::Counts counts)const noexcept{
 if(!public_||!m.CacheReadContext()||counts.types!=types_.size()||counts.fields!=fields_.size()||counts.methods!=methods_.size())return false;void*cache=m.Get(cache_),*anchor=m.Get(anchor_);if(!cache||!anchor||m.Get(root)!=cache)return false;const auto&b=public_->Native();if(b.metadata->Reference(anchor,offsetof(class_metadata::Class11,dex_cache))!=cache||b.metadata->Reference(anchor,offsetof(class_metadata::Class11,class_loader))!=(loader_?m.Get(loader_):nullptr))return false;
 DexIdentity dex{};return ReadDex(*b.metadata,anchor,m.CacheReadContext(),&dex)&&SameDex(dex,public_->NamespaceDex());
}
bool Rooted::Type(const native_hierarchy_cache::SourceMapper&m,uint32_t i,void*value)const noexcept{return public_&&m.CacheReadContext()&&value&&i<types_.size()&&m.Get(types_[i])==value;}
// Public/native text was proved against each declaring Class's original DEX
// during Observe. Fresh stopped ownership plus the same native member and DEX
// index preserves that exact name/signature without parsing in the final gate.
bool Rooted::Field(const native_hierarchy_cache::SourceMapper&m,uint32_t i,void*value)const noexcept{
 if(!public_||!m.CacheReadContext()||i>=fields_.size()||!value)return false;
 const auto&r=public_->Fields()[i];const auto&b=public_->Native();void*owner=m.Get(fields_[i]);DexIdentity dex{};
 if(value!=r.native||!owner||!m.Readable(value,sizeof(field_array::Field))||b.metadata->FieldDeclaringClass(value)!=owner||!ReadDex(*b.metadata,owner,m.CacheReadContext(),&dex)||!SameDex(dex,r.dex))return false;
 uint32_t index=0;std::memcpy(&index,static_cast<const unsigned char*>(value)+offsetof(field_array::Field,dex_field_index),4);
 return index==r.native_index&&(field_array::Flags(value)&0xffffu)==r.flags;
}
bool Rooted::Method(const native_hierarchy_cache::SourceMapper&m,uint32_t i,void*value)const noexcept{
 if(!public_||!m.CacheReadContext()||i>=methods_.size()||!value)return false;
 const auto&r=public_->Methods()[i];const auto&b=public_->Native();method_metadata::Identity id;void*owner=m.Get(methods_[i]);DexIdentity dex{};
 return value==r.native&&owner&&m.Readable(value,b.method_layout.size)&&b.metadata->MethodDeclaringClass(value)==owner&&ReadDex(*b.metadata,owner,m.CacheReadContext(),&dex)&&SameDex(dex,r.dex)&&method_metadata::ReadIdentity(b.method_layout,value,b.method_layout.size,&id)&&(id.access_flags&0xffffu)==r.flags&&id.dex_method_index==r.native_index;
}
}
