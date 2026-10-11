#pragma once
// Original public ART/JVMTI host protocol. All domain certificates, inventories,
// Source Sessions and same-Plan checks remain their production implementations.
#include "native_whole_inline_public_fixture.h"
#include <map>
namespace interface_domain_fixture {
void* cloneable=nullptr;void* serializable=nullptr;void* erroneous=nullptr;
std::vector<void*> owned_classes;
std::map<void*,uint32_t> public_access;
JNINativeInterface_ prior_jni{};jvmtiInterface_1 prior_ti{};
native_schema_guard_verifier::Bindings prior_bindings{};
whole_inline_fixture::PublicMethod* InlineNative(void*method){for(auto&m:whole_inline_fixture::methods)if(m.native==method)return &m;return nullptr;}
whole_inline_fixture::PublicMethod* InlineId(jmethodID id){for(auto&m:whole_inline_fixture::methods)if(id==reinterpret_cast<jmethodID>(uintptr_t(m.native)|1u))return &m;return nullptr;}
bool OwnedClass(void*klass){return std::find(owned_classes.begin(),owned_classes.end(),klass)!=owned_classes.end();}
void* Decode(void*manager,jmethodID id){assert(manager==&linker);if(BootIsMethod(id))return BootCreatedMethod();auto*m=InlineId(id);return m?m->native:nullptr;}
void* Reflect(const abi::common::RunnableJniScope&s,jobject object){assert(!stopped&&s.self==thread.data()&&s.env==&env);if(object==reinterpret_cast<jobject>(BootCreatedMethod()))return BootCreatedMethod();auto*m=InlineNative(object);return m?m->native:nullptr;}
jobject ToReflect(JNIEnv*e,jclass klass,jmethodID id,jboolean is_static){if(BootIsMethod(id))return BootReflected(e,klass,id,is_static);if(InlineId(id))return whole_inline_fixture::ToReflect(e,klass,id,is_static);return prior_jni.ToReflectedMethod?prior_jni.ToReflectedMethod(e,klass,id,is_static):nullptr;}
jvmtiError Loader(jvmtiEnv*,jclass klass,jobject*out){*out=reinterpret_cast<jobject>(Root(static_cast<char*>(static_cast<void*>(klass))+offsetof(class_metadata::Class17,class_loader)));return JVMTI_ERROR_NONE;}
jvmtiError ClassFlags(jvmtiEnv*ti,jclass klass,jint*out){if(OwnedClass(klass)){const auto found=public_access.find(klass);assert(found!=public_access.end());*out=jint(found->second);return JVMTI_ERROR_NONE;}return prior_ti.GetClassModifiers(ti,klass,out);}
jvmtiError ClassStatus(jvmtiEnv*ti,jclass klass,jint*out){
 if(klass==erroneous){*out=JVMTI_CLASS_STATUS_PREPARED|JVMTI_CLASS_STATUS_ERROR;return JVMTI_ERROR_NONE;}
 const auto&descriptor=Public(klass).descriptor;
 if(!descriptor.empty()&&descriptor[0]=='['){*out=JVMTI_CLASS_STATUS_ARRAY;return JVMTI_ERROR_NONE;}
 if(descriptor.size()==1){*out=JVMTI_CLASS_STATUS_PRIMITIVE;return JVMTI_ERROR_NONE;}
 return prior_ti.GetClassStatus(ti,klass,out);
}
jclass Parent(JNIEnv*e,jclass klass){if(OwnedClass(klass)){Jni();return reinterpret_cast<jclass>(Public(klass).parent);}return prior_jni.GetSuperclass(e,klass);}
jvmtiError Interfaces(jvmtiEnv*ti,jclass klass,jint*n,jclass**out){return GuardInterfaces(ti,klass,n,out);}
jvmtiError Methods(jvmtiEnv*ti,jclass klass,jint*n,jmethodID**out){
 if(klass==BootCreatedClass())return BootClassMethods(ti,klass,n,out);
 if(OwnedClass(klass))return whole_inline_fixture::Methods(ti,klass,n,out);
 return prior_ti.GetClassMethods(ti,klass,n,out);
}
jvmtiError Name(jvmtiEnv*ti,jmethodID id,char**name,char**signature,char**generic){if(BootIsMethod(id))return BootMethodName(ti,id,name,signature,generic);if(InlineId(id))return whole_inline_fixture::Name(ti,id,name,signature,generic);return prior_ti.GetMethodName(ti,id,name,signature,generic);}
jvmtiError Flags(jvmtiEnv*ti,jmethodID id,jint*out){if(BootIsMethod(id))return BootMethodFlags(ti,id,out);if(InlineId(id))return whole_inline_fixture::Flags(ti,id,out);return prior_ti.GetMethodModifiers(ti,id,out);}
jvmtiError Declaring(jvmtiEnv*ti,jmethodID id,jclass*out){if(BootIsMethod(id))return BootMethodDeclaring(ti,id,out);if(InlineId(id))return whole_inline_fixture::Declaring(ti,id,out);return prior_ti.GetMethodDeclaringClass(ti,id,out);}
jvmtiError Bytes(jvmtiEnv*ti,jmethodID id,jint*n,unsigned char**out){if(BootIsMethod(id))return BootMethodBytecodes(ti,id,n,out);if(InlineId(id))return whole_inline_fixture::Bytes(ti,id,n,out);return JVMTI_ERROR_ABSENT_INFORMATION;}
jvmtiError Locals(jvmtiEnv*ti,jmethodID id,jint*out){if(BootIsMethod(id))return BootMethodLocals(ti,id,out);if(InlineId(id))return whole_inline_fixture::Locals(ti,id,out);return JVMTI_ERROR_ABSENT_INFORMATION;}
jvmtiError CatalogMethods(jvmtiEnv*ti,jclass klass,jint*n,jmethodID**out){if(klass==BootCreatedClass()||OwnedClass(klass))return Methods(ti,klass,n,out);*n=0;*out=nullptr;return JVMTI_ERROR_NONE;}
// Comparisons consume exact native membership in independent public declarations.
// Native DEX indices are never compared across unrelated declaring namespaces.
bool MethodText(void*method,const char**name,const char**descriptor){
 if(method==BootCreatedMethod()){*name="staticKeep";*descriptor="()I";return true;}
 if(auto*m=InlineNative(method)){*name=m->declaration.name.c_str();*descriptor=m->declaration.descriptor.c_str();return true;}
 if(method==guard_base_method){*name="keep";*descriptor="()I";return true;}
 for(void*klass:{static_cast<void*>(old_class),static_cast<void*>(new_class)}){
  // methods_ is a native uint64_t, not a compressed managed root.
  uint64_t full=0;std::memcpy(&full,static_cast<char*>(klass)+offsetof(class_metadata::Class17,methods),8);
  if(full)for(size_t i=0;i<3;++i)if(method==reinterpret_cast<void*>(uintptr_t(full)+8+i*32)){*name=public_method_names[i];*descriptor="()I";return true;}
 }
 return false;
}
bool Same(void*a,void*b){const char*an=nullptr,*ad=nullptr,*bn=nullptr,*bd=nullptr;return MethodText(a,&an,&ad)&&MethodText(b,&bn,&bd)&&!std::strcmp(an,bn)&&!std::strcmp(ad,bd);}
bool GuardSame(void*,void*a,void*b){return Same(a,b);}
void* GuardDecodeMethod(void*owner,jmethodID id){if(BootIsMethod(id)||InlineId(id))return Decode(&linker,id);return prior_bindings.decode_method(owner,id);}
jobject ObjectA(JNIEnv*e,jobject object,jmethodID id,const jvalue*args){if(id==reinterpret_cast<jmethodID>(&schema_typed_component_id)){Jni();return reinterpret_cast<jobject>(Root(static_cast<char*>(static_cast<void*>(object))+offsetof(class_metadata::Class17,component_type)));}return prior_jni.CallObjectMethodA(e,object,id,args);}
jobject ObjectV(JNIEnv*e,jobject object,jmethodID id,va_list args){if(id==reinterpret_cast<jmethodID>(&schema_typed_component_id)){Jni();return reinterpret_cast<jobject>(Root(static_cast<char*>(static_cast<void*>(object))+offsetof(class_metadata::Class17,component_type)));}return prior_jni.CallObjectMethodV(e,object,id,args);}
jboolean PrimitiveV(JNIEnv*e,jobject object,jmethodID id,va_list args){if(id==reinterpret_cast<jmethodID>(&schema_typed_primitive_id)){Jni();return Read32(object,offsetof(class_metadata::Class17,primitive_type))!=0;}return prior_jni.CallBooleanMethodV(e,object,id,args);}
jboolean PrimitiveA(JNIEnv*e,jobject object,jmethodID id,const jvalue*args){if(id==reinterpret_cast<jmethodID>(&schema_typed_primitive_id)){Jni();return Read32(object,offsetof(class_metadata::Class17,primitive_type))!=0;}return prior_jni.CallBooleanMethodA(e,object,id,args);}
void ConfigureGuards(JNINativeInterface_&jni,jvmtiInterface_1&ti,native_schema_guard_verifier::Bindings&bindings){
 prior_jni=jni;prior_ti=ti;prior_bindings=bindings;
 jni.ToReflectedMethod=ToReflect;jni.GetSuperclass=Parent;jni.CallObjectMethodA=ObjectA;jni.CallObjectMethodV=ObjectV;jni.CallBooleanMethodA=PrimitiveA;jni.CallBooleanMethodV=PrimitiveV;
 ti.GetLoadedClasses=::Loaded;ti.GetClassLoader=Loader;ti.GetClassModifiers=ClassFlags;ti.GetClassStatus=ClassStatus;ti.GetImplementedInterfaces=Interfaces;
 ti.GetClassMethods=Methods;ti.GetMethodName=Name;ti.GetMethodModifiers=Flags;ti.GetMethodDeclaringClass=Declaring;ti.GetBytecodes=Bytes;ti.GetMaxLocals=Locals;
 bindings.decode_method=GuardDecodeMethod;bindings.same_method=GuardSame;
}
void RegisterArray(void*klass,const std::string&descriptor,void*parent){
 bool existing=false;for(const auto&item:public_classes)existing|=item.native==klass;
 if(!existing)public_classes.push_back({klass,descriptor,parent,{}});
 if(!OwnedClass(klass))owned_classes.push_back(klass);
 public_access[klass]=0x11;
 auto*k=static_cast<class_metadata::Class17*>(klass);k->access_flags=0x11;k->status=15u<<28;k->class_size=160;k->object.klass=uint32_t(uintptr_t(meta_class));k->super_class=uint32_t(uintptr_t(parent));
}
void PreparePublic(const std::vector<unsigned char>&inline_dex,const std::vector<unsigned char>&dispatch_dex,
 const std::vector<unsigned char>&receiver_dex,const std::vector<unsigned char>&cloneable_dex,
 const std::vector<unsigned char>&serializable_dex,const std::vector<unsigned char>&error_dex,
 const modern_field_metadata::FieldMetadata&metadata,JNIEnv*jni,jvmtiEnv*ti,native_boot_method_catalog::Catalog&catalog){
 assert(jni==&env&&!stopped&&owned_classes.empty());
 const uint32_t entry_state=Read32(thread.data());struct RestoreThreadState{uint32_t state;~RestoreThreadState(){Put32(thread.data(),0,state);}}restore_thread{entry_state};
 catalog.Clear(jni);
 native_boot_method_catalog::Catalog scratch;whole_inline_fixture::Prepare(inline_dex,dispatch_dex,receiver_dex,metadata,ti,scratch);scratch.Clear(jni);
 owned_classes=whole_inline_fixture::classes;
 void*dispatch=whole_inline_fixture::classes.at(1);void*concrete=whole_inline_fixture::classes.at(2);
 Public(dispatch).parent=nullptr;Public(concrete).interfaces={dispatch};
 cloneable=whole_inline_fixture::AddClass(cloneable_dex,metadata);serializable=whole_inline_fixture::AddClass(serializable_dex,metadata);erroneous=whole_inline_fixture::AddClass(error_dex,metadata);
 owned_classes.push_back(cloneable);owned_classes.push_back(serializable);owned_classes.push_back(erroneous);
 const std::vector<unsigned char>* declarations[]{&inline_dex,&dispatch_dex,&receiver_dex,&cloneable_dex,&serializable_dex,&error_dex};
 const void* declared_classes[]{whole_inline_fixture::classes.at(0),dispatch,concrete,cloneable,serializable,erroneous};
 for(size_t i=0;i<6;++i){evolution::ClassSchema schema;std::string error;const auto&bytes=*declarations[i];assert(evolution::ReadMappedDexDeclarations({bytes.data(),bytes.size(),bytes.data(),bytes.size(),0},&schema,&error));public_access[const_cast<void*>(declared_classes[i])]=schema.access;}
 assert(Public(cloneable).descriptor=="Ljava/lang/Cloneable;"&&Public(serializable).descriptor=="Ljava/io/Serializable;");
 for(void*marker:{cloneable,serializable}){assert((Read32(marker,offsetof(class_metadata::Class17,access_flags))&0x200u)!=0);Public(marker).parent=nullptr;assert(Public(marker).interfaces.empty());}
 Put32(erroneous,offsetof(class_metadata::Class17,status),2u<<28);
 void*long_array=BootLongArrayClass();void*primitive=Root(static_cast<char*>(long_array)+offsetof(class_metadata::Class17,component_type));
 RegisterArray(primitive,"J",nullptr);RegisterArray(long_array,"[J",parent_class);assert(Read32(primitive,offsetof(class_metadata::Class17,primitive_type))==6);
 // ART's common array IfTable contains the two bootstrap marker identities.
 auto*marker_table=heap.New(28);Put32(marker_table,0,Read32(reinterpret_cast<void*>(uintptr_t(Read32(parent_class,offsetof(class_metadata::Class17,iftable)))),0));Put32(marker_table,8,4);
 Put32(marker_table,12,uint32_t(uintptr_t(cloneable)));Put32(marker_table,20,uint32_t(uintptr_t(serializable)));
 for(auto&item:public_classes)if(!item.descriptor.empty()&&item.descriptor[0]=='['){
  item.parent=parent_class;item.interfaces={cloneable,serializable};Put32(item.native,offsetof(class_metadata::Class17,iftable),uint32_t(uintptr_t(marker_table)));
  RegisterArray(item.native,item.descriptor,parent_class);
 }
 // Public catalog enumeration is complete; only declared bootstrap fixture
 // methods enter this codec. Application classes retain their actual loader.
 const auto saved_jni=table;const auto*saved_ti=ti->functions;jvmtiInterface_1 functions=*saved_ti;
 prior_jni=table;prior_ti=functions;
 functions.GetCapabilities=BootCapabilities;functions.GetLoadedClasses=::Loaded;functions.GetClassSignature=Signature;functions.GetClassStatus=ClassStatus;functions.GetClassLoader=Loader;
 functions.GetClassMethods=CatalogMethods;functions.GetMethodName=Name;functions.GetMethodModifiers=Flags;functions.GetMethodDeclaringClass=Declaring;functions.GetBytecodes=Bytes;functions.GetMaxLocals=Locals;functions.Deallocate=Deallocate;
 table.ToReflectedMethod=ToReflect;ti->functions=&functions;std::string error;
 const bool prepared=catalog.Prepare(jni,ti,{&linker,Decode,Reflect},&error);
 table=saved_jni;ti->functions=saved_ti;
 if(!prepared){std::cerr<<"Unified interface domain catalog: "<<error<<"\n";std::abort();}
 assert(catalog.Find(BootCreatedMethod())&&catalog.Find(whole_inline_fixture::caller)&&catalog.Find(whole_inline_fixture::callee));
}
} // namespace interface_domain_fixture
