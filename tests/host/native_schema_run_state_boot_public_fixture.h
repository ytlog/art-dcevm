// Public oracles for a distinct bootstrap-loader scalar Class and the real
// abstract-vtable calibration. Included after the aggregate/public fixtures.
#include "platform/common/native_abstract_instances.h"
#include "platform/common/native_boot_method_catalog.h"
namespace {
void* boot_public_class=nullptr;void* boot_public_method=nullptr;
void* boot_abstract_classes[3]{};void* boot_abstract_methods[3]{};
void* boot_long_array_class=nullptr;
void* boot_abstract_array=nullptr;int boot_factory_id,boot_modifiers_id,boot_fixture_id;
void* BootCreatedMethod(){return boot_public_method;}
void* BootCreatedClass(){return boot_public_class;}
void* BootLongArrayClass(){return boot_long_array_class;}
void* BootReflectionLeaf(const abi::common::RunnableJniScope&scope,jobject reflected){
 assert(scope.self==current&&scope.env==&env&&Read32(current)==0&&!stopped);
 if(reflected==reinterpret_cast<jobject>(boot_public_method))return boot_public_method;
 for(auto* method:boot_abstract_methods)if(reflected==reinterpret_cast<jobject>(method))return method;
 return nullptr;
}
jclass BootFind(JNIEnv*e,const char*name){if(!std::strcmp(name,"com/github/ytlog/artdcevm/internal/AbstractVTableOracle")){Jni();return reinterpret_cast<jclass>(&boot_fixture_id);}return Find(e,name);}
jmethodID BootMethodId(JNIEnv*e,jclass c,const char*name,const char*signature){if(!std::strcmp(name,"getModifiers")){Jni();return reinterpret_cast<jmethodID>(&boot_modifiers_id);}return MethodId(e,c,name,signature);}
jmethodID BootFactoryId(JNIEnv*,jclass c,const char*name,const char*signature){Jni();assert(c==reinterpret_cast<jclass>(&boot_fixture_id)&&!std::strcmp(name,"create")&&!std::strcmp(signature,"()[Ljava/lang/Object;"));return reinterpret_cast<jmethodID>(&boot_factory_id);}
jobject BootFactory(JNIEnv*,jclass c,jmethodID id,va_list){Jni();assert(c==reinterpret_cast<jclass>(&boot_fixture_id)&&id==reinterpret_cast<jmethodID>(&boot_factory_id));return reinterpret_cast<jobject>(boot_abstract_array);}
jsize BootArrayLength(JNIEnv*e,jarray a){if(a!=reinterpret_cast<jarray>(boot_abstract_array))return GuardLength(e,a);Jni();return 6;}
jobject BootArrayElement(JNIEnv*e,jobjectArray a,jsize i){if(a!=reinterpret_cast<jobjectArray>(boot_abstract_array))return GuardGet(e,a,i);Jni();assert(i>=0&&i<6);return reinterpret_cast<jobject>(uintptr_t(Read32(a,12+size_t(i)*4)));}
jint BootModifiers(JNIEnv*,jobject c,jmethodID id,va_list){Jni();assert(id==reinterpret_cast<jmethodID>(&boot_modifiers_id));for(size_t i=0;i<3;++i)if(c==reinterpret_cast<jobject>(boot_abstract_classes[i]))return i==2?1:0x401;std::abort();}
bool BootIsMethod(jmethodID id){return id==reinterpret_cast<jmethodID>(uintptr_t(boot_public_method)|1u);}
void* BootDecodeMethod(void*manager,jmethodID id){assert(manager==&linker);return BootIsMethod(id)?boot_public_method:nullptr;}
jobject BootReflected(JNIEnv*,jclass c,jmethodID id,jboolean is_static){Jni();assert(c==reinterpret_cast<jclass>(boot_public_class)&&BootIsMethod(id)&&is_static);return reinterpret_cast<jobject>(boot_public_method);}
jvmtiError BootCapabilities(jvmtiEnv*,jvmtiCapabilities* caps){*caps={};caps->can_get_bytecodes=1;return JVMTI_ERROR_NONE;}
jvmtiError BootClassLoader(jvmtiEnv*e,jclass c,jobject*out){if(c==reinterpret_cast<jclass>(boot_public_class)){*out=nullptr;return JVMTI_ERROR_NONE;}return GuardLoader(e,c,out);}
jvmtiError BootClassMethods(jvmtiEnv*e,jclass c,jint*count,jmethodID**ids){if(c!=reinterpret_cast<jclass>(boot_public_class))return GuardMethods(e,c,count,ids);*count=1;*ids=static_cast<jmethodID*>(std::malloc(sizeof(jmethodID)));**ids=reinterpret_cast<jmethodID>(uintptr_t(boot_public_method)|1u);return JVMTI_ERROR_NONE;}
jvmtiError BootMethodName(jvmtiEnv*e,jmethodID id,char**name,char**signature,char**generic){if(!BootIsMethod(id))return GuardMethodName(e,id,name,signature,generic);*name=strdup("staticKeep");*signature=strdup("()I");if(generic)*generic=nullptr;return JVMTI_ERROR_NONE;}
jvmtiError BootMethodFlags(jvmtiEnv*e,jmethodID id,jint*flags){if(!BootIsMethod(id))return GuardMethodFlags(e,id,flags);*flags=9;return JVMTI_ERROR_NONE;}
jvmtiError BootMethodDeclaring(jvmtiEnv*,jmethodID id,jclass*out){assert(BootIsMethod(id));*out=reinterpret_cast<jclass>(boot_public_class);return JVMTI_ERROR_NONE;}
jvmtiError BootMethodBytecodes(jvmtiEnv*,jmethodID id,jint*size,unsigned char**out){assert(BootIsMethod(id));const unsigned char scalar[]{0x12,0,0x0f,0};*size=4;*out=static_cast<unsigned char*>(std::malloc(4));std::memcpy(*out,scalar,4);return JVMTI_ERROR_NONE;}
jvmtiError BootMethodLocals(jvmtiEnv*,jmethodID id,jint*out){assert(BootIsMethod(id));*out=1;return JVMTI_ERROR_NONE;}
// Parse fixed StandardDex class_data rather than infer the CodeItem pointer
// from the native method or hard-code the independent writer's output offset.
bool BootCodeOffset(const std::vector<unsigned char>&bytes,uint32_t method_index,uint32_t*out){
 if(bytes.size()<112||Read32(bytes.data(),96)!=1)return false;
 const size_t class_defs=Read32(bytes.data(),100);if(class_defs>bytes.size()||32>bytes.size()-class_defs)return false;
 size_t p=Read32(bytes.data(),class_defs+24);
 auto leb=[&](uint32_t*value){*value=0;for(unsigned shift=0;shift<35;shift+=7){if(p>=bytes.size())return false;const auto b=bytes[p++];if(shift==28&&(b&0xf0))return false;*value|=uint32_t(b&127)<<shift;if(!(b&128))return true;}return false;};
 uint32_t counts[4]{};for(auto&count:counts)if(!leb(&count))return false;
 for(uint64_t n=0;n<uint64_t(counts[0])+counts[1];++n){uint32_t ignored;if(!leb(&ignored)||!leb(&ignored))return false;}
 bool found=false;
 for(unsigned kind=2;kind<4;++kind){uint32_t index=0;for(uint32_t n=0;n<counts[kind];++n){uint32_t delta,flags,offset;if(!leb(&delta)||!leb(&flags)||!leb(&offset)||delta>UINT32_MAX-index)return false;index+=delta;if(index!=method_index)continue;if(found||flags!=9||!offset||offset%4||offset>bytes.size()||20>bytes.size()-offset)return false;*out=offset;found=true;}}
 return found;
}
void BootAbstractOracles(){
 void*long_array=heap.New(160);boot_long_array_class=long_array;void*primitive=heap.New(160);
 for(auto*c:{long_array,primitive})static_cast<class_metadata::Class17*>(c)->object.klass=uint32_t(uintptr_t(meta_class));
 static_cast<class_metadata::Class17*>(long_array)->component_type=uint32_t(uintptr_t(primitive));static_cast<class_metadata::Class17*>(primitive)->primitive_type=6;
 for(size_t i=0;i<3;++i){auto*c=static_cast<class_metadata::Class17*>(heap.New(160));boot_abstract_classes[i]=c;c->object.klass=uint32_t(uintptr_t(meta_class));c->access_flags=i==2?1:0x401;c->class_size=160;auto*method=heap.New(32);boot_abstract_methods[i]=method;Put32(method,0,uint32_t(uintptr_t(c)));Put32(method,4,i==0?0x401:1);const uint64_t pointer=uintptr_t(method);
  if(i<2){auto*array=heap.New(24);Put32(array,0,uint32_t(uintptr_t(long_array)));Put32(array,8,1);std::memcpy(static_cast<char*>(array)+16,&pointer,8);c->vtable=uint32_t(uintptr_t(array));}
  else{Put32(c,sizeof(class_metadata::Class17),1);const size_t payload=((sizeof(class_metadata::Class17)+4+7)&~size_t(7))+8;assert(payload+8<=160);std::memcpy(reinterpret_cast<char*>(c)+payload,&pointer,8);}
 }
 boot_abstract_array=heap.New(36);Put32(boot_abstract_array,8,6);for(size_t i=0;i<3;++i){Put32(boot_abstract_array,12+i*4,uint32_t(uintptr_t(boot_abstract_classes[i])));Put32(boot_abstract_array,24+i*4,uint32_t(uintptr_t(boot_abstract_methods[i])));}
}
}
class BootPublicHooks:public OwnerHooks{
 public:void* FindSymbol(const char*lib,const char*name)override{
 if(!std::strcmp(name,abi::common::kFromReflectedMethod))return reinterpret_cast<void*>(BootReflectionLeaf);
 if(std::strstr(name,"SetStatusLocked"))return reinterpret_cast<void*>(MatchedStatus);
 if(std::strstr(name,"SetObsoleteClass"))return reinterpret_cast<void*>(MatchedRetain);
 if(std::strstr(name,"SetEntryPointsForObsoleteMethod"))return reinterpret_cast<void*>(MatchedObsolete);
 if(std::strstr(name,"ReplaceReferences"))return reinterpret_cast<void*>(MatchedReplace);
 return OwnerHooks::FindSymbol(lib,name);
 }
};
namespace {
bool CalibrateBootPublic(BootPublicHooks&hooks,JNIEnv*jni,jvmtiEnv*jvmti,
 const modern_field_metadata::FieldMetadata&metadata,const method_identity::Calibration&model,
 const std::vector<unsigned char>&boot_bytes,native_boot_method_catalog::Catalog*catalog,
 native_abstract_instances::Service*abstracts,std::string*error){
 assert(jni==&env&&jvmti&&current==thread.data()&&!stopped&&Read32(current)==0);
 if(!catalog||!abstracts||boot_public_class){if(error)*error="Invalid Boot public fixture owner";return false;}
 evolution::ClassSchema schema;evolution::MappedDexMetadata mapping{boot_bytes.data(),boot_bytes.size(),boot_bytes.data(),boot_bytes.size(),0};
 if(!evolution::ReadMappedDexDeclarations(mapping,&schema,error)||schema.descriptor!="Lcom/example/BootScalar;"||schema.superclass!="Ljava/lang/Object;"||!schema.interfaces.empty()||!schema.fields.empty()||schema.methods.size()!=1||schema.methods[0].name!="staticKeep"||schema.methods[0].descriptor!="()I"||schema.methods[0].access!=9)return false;
 uint32_t offset=0;if(!BootCodeOffset(boot_bytes,schema.methods[0].dex_index,&offset))return false;
 const unsigned char scalar[]{0x12,0,0x0f,0};if(std::memcmp(boot_bytes.data()+offset+16,scalar,4)||Read32(boot_bytes.data(),offset+12)!=2)return false;
 uint16_t registers=0,ins=0;std::memcpy(&registers,boot_bytes.data()+offset,2);std::memcpy(&ins,boot_bytes.data()+offset+2,2);if(registers!=1||ins)return false;
 auto*klass=static_cast<class_metadata::Class17*>(heap.New(160));klass->object.klass=uint32_t(uintptr_t(meta_class));klass->access_flags=1;klass->super_class=uint32_t(uintptr_t(parent_class));klass->status=15u<<28;klass->class_size=160;klass->dex_class_def_index=0;
 klass->dex_type_index=int32_t(Read32(boot_bytes.data(),Read32(boot_bytes.data(),100)));auto*cache=heap.New(112);auto*file=static_cast<dex_mapping_abi::Prefix*>(heap.New(sizeof(dex_mapping_abi::Prefix)));*file={nullptr,boot_bytes.data(),boot_bytes.size(),boot_bytes.data(),boot_bytes.size()};const uint64_t file_pointer=uintptr_t(file);std::memcpy(static_cast<char*>(cache)+16,&file_pointer,8);klass->dex_cache=uint32_t(uintptr_t(cache));
 auto*methods=static_cast<unsigned char*>(heap.New(40));Put32(methods,0,1);auto*method=new(methods+8)abi::arm64::android12::Method12{};method->declaring_class=uint32_t(uintptr_t(klass));method->access_flags=9;method->dex_method_index=schema.methods[0].dex_index;method->data=uintptr_t(boot_bytes.data()+offset);klass->methods=uintptr_t(methods);boot_public_class=klass;boot_public_method=method;public_classes.push_back({klass,schema.descriptor,parent_class,{}});
 BootAbstractOracles();
 jvmtiInterface_1 funcs=*jvmti->functions;
 struct Restore{JNINativeInterface_ jni;jvmtiEnv*ti;const jvmtiInterface_1*original;uint32_t state;~Restore(){table=jni;ti->functions=original;Put32(current,0,state);}}restore{table,jvmti,jvmti->functions,Read32(current)};jvmti->functions=&funcs;
 table.FindClass=BootFind;table.GetMethodID=BootMethodId;table.GetStaticMethodID=BootFactoryId;table.CallStaticObjectMethodV=BootFactory;table.GetArrayLength=BootArrayLength;table.GetObjectArrayElement=BootArrayElement;table.CallIntMethodV=BootModifiers;table.ToReflectedMethod=BootReflected;
 funcs.GetCapabilities=BootCapabilities;funcs.GetLoadedClasses=Loaded;funcs.GetClassStatus=Status;funcs.GetClassSignature=Signature;funcs.GetClassLoader=BootClassLoader;funcs.GetClassMethods=BootClassMethods;funcs.GetMethodName=BootMethodName;funcs.GetMethodModifiers=BootMethodFlags;funcs.GetMethodDeclaringClass=BootMethodDeclaring;funcs.GetBytecodes=BootMethodBytecodes;funcs.GetMaxLocals=BootMethodLocals;funcs.Deallocate=Deallocate;
 const bool calibrated=abstracts->Calibrate(hooks,jni,metadata,model,error);Put32(current,0,restore.state);if(!calibrated)return false;
 const native_boot_method_catalog::Codec codec{&linker,BootDecodeMethod,BootReflectionLeaf};
 if(!catalog->Prepare(jni,jvmti,codec,error))return false;
 const auto*entry=catalog->Find(boot_public_method);
 return entry&&entry->declaring_signature==schema.descriptor&&entry->name=="staticKeep"&&entry->descriptor=="()I"&&entry->body==native_boot_method_catalog::Body::kPrimitiveLocal&&entry->max_locals==1&&entry->dex==std::vector<unsigned char>(scalar,scalar+4)&&abstracts->Ready();
}
}
