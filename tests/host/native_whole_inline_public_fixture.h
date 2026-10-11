#pragma once
// Public JVMTI/reflection protocol for independently parsed immutable namespaces.
// This models ART public leaves; the production Catalog and source Pool own all
// retained identities. No catalog body/permission is injected by this fixture.
namespace whole_inline_fixture {
struct PublicMethod {void*native=nullptr;void*declaring=nullptr;evolution::Member declaration;uint32_t code=0;const std::vector<unsigned char>*bytes=nullptr;};
std::vector<PublicMethod> methods;std::vector<void*>classes;
void*callee=nullptr;void*caller=nullptr;void*handler=nullptr;void*outer=nullptr;void*receiver=nullptr;
uint32_t CodeOffset(const std::vector<unsigned char>&b,uint32_t wanted){
 size_t at=Read32(b.data(),Read32(b.data(),100)+24);
 const auto leb=[&](uint32_t*out){*out=0;for(unsigned s=0;s<35;s+=7){assert(at<b.size());const auto v=b[at++];*out|=uint32_t(v&127)<<s;if(!(v&128))return;}std::abort();};
 uint32_t count[4]{};for(auto&n:count)leb(&n);for(size_t i=0;i<size_t(count[0])+count[1];++i){uint32_t n;leb(&n);leb(&n);}
 for(unsigned section=2;section<4;++section){uint32_t index=0;for(size_t i=0;i<count[section];++i){uint32_t delta,flags,code;leb(&delta);leb(&flags);leb(&code);index+=delta;if(index==wanted)return code;}}
 std::abort();
}
PublicMethod&Method(jmethodID id){for(auto&m:methods)if(id==reinterpret_cast<jmethodID>(uintptr_t(m.native)|1u))return m;std::abort();}
void*Decode(void*manager,jmethodID id){assert(manager==&linker&&!stopped);return Method(id).native;}
void*Reflect(const abi::common::RunnableJniScope&s,jobject object){assert(s.env==&env&&s.self==thread.data()&&!stopped);for(auto&m:methods)if(object==reinterpret_cast<jobject>(m.native))return m.native;return nullptr;}
jobject ToReflect(JNIEnv*,jclass k,jmethodID id,jboolean is_static){Jni();const auto&m=Method(id);assert(k==m.declaring&&bool(is_static)==bool(m.declaration.access&8));return reinterpret_cast<jobject>(m.native);}
jvmtiError Loaded(jvmtiEnv*,jint*n,jclass**out){*n=jint(classes.size());*out=static_cast<jclass*>(std::malloc(classes.size()*sizeof(jclass)));for(size_t i=0;i<classes.size();++i)(*out)[i]=reinterpret_cast<jclass>(classes[i]);return JVMTI_ERROR_NONE;}
jvmtiError Loader(jvmtiEnv*,jclass,jobject*out){*out=nullptr;return JVMTI_ERROR_NONE;}
jvmtiError Methods(jvmtiEnv*,jclass k,jint*n,jmethodID**out){*n=0;for(const auto&m:methods)*n+=m.declaring==k;*out=static_cast<jmethodID*>(std::malloc(size_t(*n)*sizeof(jmethodID)));size_t i=0;for(const auto&m:methods)if(m.declaring==k)(*out)[i++]=reinterpret_cast<jmethodID>(uintptr_t(m.native)|1u);return JVMTI_ERROR_NONE;}
jvmtiError Name(jvmtiEnv*,jmethodID id,char**name,char**signature,char**generic){const auto&m=Method(id);*name=strdup(m.declaration.name.c_str());*signature=strdup(m.declaration.descriptor.c_str());if(generic)*generic=nullptr;return JVMTI_ERROR_NONE;}
jvmtiError Flags(jvmtiEnv*,jmethodID id,jint*out){*out=jint(Method(id).declaration.access);return JVMTI_ERROR_NONE;}
jvmtiError Declaring(jvmtiEnv*,jmethodID id,jclass*out){*out=reinterpret_cast<jclass>(Method(id).declaring);return JVMTI_ERROR_NONE;}
jvmtiError Bytes(jvmtiEnv*,jmethodID id,jint*size,unsigned char**out){const auto&m=Method(id);if(!m.code)return JVMTI_ERROR_ABSENT_INFORMATION;*size=jint(Read32(m.bytes->data(),m.code+12)*2);*out=static_cast<unsigned char*>(std::malloc(size_t(*size)));std::memcpy(*out,m.bytes->data()+m.code+16,size_t(*size));return JVMTI_ERROR_NONE;}
jvmtiError Locals(jvmtiEnv*,jmethodID id,jint*out){const auto&m=Method(id);uint16_t n=0;if(m.code)std::memcpy(&n,m.bytes->data()+m.code,2);*out=n;return JVMTI_ERROR_NONE;}
void*AddClass(const std::vector<unsigned char>&bytes,const modern_field_metadata::FieldMetadata&metadata){
 evolution::ClassSchema schema;std::string error;assert(evolution::ReadMappedDexDeclarations({bytes.data(),bytes.size(),bytes.data(),bytes.size(),0},&schema,&error));
 auto*k=new(heap.New(160))class_metadata::Class17{};k->object.klass=uint32_t(uintptr_t(meta_class));k->super_class=uint32_t(uintptr_t(parent_class));k->access_flags=schema.access;k->class_size=160;k->object_size=32;k->status=15u<<28;k->dex_class_def_index=0;k->dex_type_index=int32_t(Read32(bytes.data(),Read32(bytes.data(),100)));
 auto*cache=heap.New(112);auto*file=new(heap.New(sizeof(dex_mapping_abi::Prefix)))dex_mapping_abi::Prefix{nullptr,bytes.data(),bytes.size(),bytes.data(),bytes.size()};const uintptr_t p=uintptr_t(file);std::memcpy(static_cast<char*>(cache)+16,&p,8);k->dex_cache=uint32_t(uintptr_t(cache));
 k->iftable=Read32(parent_class,offsetof(class_metadata::Class17,iftable));auto*vt=heap.New(16);Put32(vt,0,uint32_t(uintptr_t(BootLongArrayClass())));k->vtable=uint32_t(uintptr_t(vt));
 const auto layout=method_metadata::kAndroid12Onward;auto*array=static_cast<unsigned char*>(heap.New(8+layout.size*schema.methods.size()));Put32(array,0,uint32_t(schema.methods.size()));k->methods=uintptr_t(array);
 for(size_t i=0;i<schema.methods.size();++i){const auto&decl=schema.methods[i];auto*m=new(array+8+i*layout.size)abi::arm64::android12::Method12{};m->declaring_class=uint32_t(uintptr_t(k));m->access_flags=decl.access;m->dex_method_index=decl.dex_index;m->method_index=uint16_t(i);const auto offset=CodeOffset(bytes,decl.dex_index);if(offset)m->data=uintptr_t(bytes.data()+offset);methods.push_back({m,k,decl,offset,&bytes});
  if(decl.name=="interfaceCaller")caller=m;if(decl.name=="inlineHandler")handler=m;if(decl.name=="outerSafe")outer=m;if(schema.descriptor=="Lcom/example/Receiver;"&&decl.name=="call")callee=m;}
 public_classes.push_back({k,schema.descriptor,parent_class,{}});classes.push_back(k);field_copy::CopyClassView checked;assert(metadata.ClassIdentityVerified(k,schema.descriptor.c_str(),nullptr,nullptr,nullptr,&checked,&error));return k;
}
void Prepare(const std::vector<unsigned char>&body,const std::vector<unsigned char>&interface_dex,const std::vector<unsigned char>&receiver_dex,const modern_field_metadata::FieldMetadata&metadata,jvmtiEnv*ti,native_boot_method_catalog::Catalog&catalog){
 assert(methods.empty()&&classes.empty());AddClass(body,metadata);void*interface=AddClass(interface_dex,metadata);void*concrete=AddClass(receiver_dex,metadata);assert(callee&&caller&&handler&&outer);
 const auto original_jni=table;const auto*old_ti=ti->functions;jvmtiInterface_1 functions=*old_ti;ti->functions=&functions;
 functions.GetCapabilities=BootCapabilities;functions.GetLoadedClasses=Loaded;functions.GetClassStatus=Status;functions.GetClassSignature=Signature;functions.GetClassLoader=Loader;functions.GetClassMethods=Methods;functions.GetMethodName=Name;functions.GetMethodModifiers=Flags;functions.GetMethodDeclaringClass=Declaring;functions.GetBytecodes=Bytes;functions.GetMaxLocals=Locals;functions.Deallocate=Deallocate;table.ToReflectedMethod=ToReflect;
 std::string error;assert(catalog.Prepare(&env,ti,{&linker,Decode,Reflect},&error));table=original_jni;ti->functions=old_ti;
 // The original descriptor leaf must describe the same independently fixed
 // primitive component as the abstract-vtable calibration (Primitive::kPrimLong).
 auto*primitive=Root(static_cast<char*>(BootLongArrayClass())+offsetof(class_metadata::Class17,component_type));assert(primitive&&Read32(primitive,offsetof(class_metadata::Class17,primitive_type))==6);public_classes.push_back({primitive,"J",nullptr,{}});classes.push_back(primitive);
 void*declaration=nullptr;for(const auto&m:methods)if(m.declaring==interface&&m.declaration.name=="call")declaration=m.native;assert(declaration);
 // Actual IfTable pairs, [J method-pointer array and exact source slot zero.
 auto*array=heap.New(24);Put32(array,0,uint32_t(uintptr_t(BootLongArrayClass())));Put32(array,8,1);const uintptr_t target=uintptr_t(callee);std::memcpy(static_cast<char*>(array)+16,&target,8);
 auto*table_array=heap.New(20);Put32(table_array,0,Read32(reinterpret_cast<void*>(uintptr_t(Read32(parent_class,offsetof(class_metadata::Class17,iftable)))),0));Put32(table_array,8,2);Put32(table_array,12,uint32_t(uintptr_t(interface)));Put32(table_array,16,uint32_t(uintptr_t(array)));Put32(concrete,offsetof(class_metadata::Class17,iftable),uint32_t(uintptr_t(table_array)));
 receiver=heap.New(32);Put32(receiver,0,uint32_t(uintptr_t(concrete)));
 assert(catalog.Find(caller)&&catalog.Find(handler)&&catalog.Find(outer)&&catalog.Find(declaration)&&catalog.Find(callee));
}
}
