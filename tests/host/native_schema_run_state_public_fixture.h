// Independent public JNI/JVMTI declarations for the matched StandardDex fixture.
// Named ART leaves remain in the surrounding actual aggregate fixture.
namespace {
std::vector<void*> guard_arrays;
void* guard_expected_loader=nullptr;void* guard_base_class=nullptr;void* guard_base_method=nullptr;
std::array<const char*,3> public_method_names{{"gone","keep","staticKeep"}};
std::array<uint32_t,3> public_method_flags{{0x101,1,9}};
size_t PublicMethodIndex(jmethodID id){if(id==reinterpret_cast<jmethodID>(guard_base_method))return 1;for(size_t n=0;n<3;++n)if(id==reinterpret_cast<jmethodID>(reinterpret_cast<char*>(old_method)+32*n))return n;std::abort();}
jvmtiError GuardMethods(jvmtiEnv*,jclass c,jint* count,jmethodID** ids){*count=c==reinterpret_cast<jclass>(old_class)?3:c==reinterpret_cast<jclass>(guard_base_class)?1:0;*ids=nullptr;if(*count){*ids=static_cast<jmethodID*>(std::malloc(3*sizeof(jmethodID)));for(size_t n=0;n<3;++n)(*ids)[n]=c==reinterpret_cast<jclass>(guard_base_class)?reinterpret_cast<jmethodID>(guard_base_method):reinterpret_cast<jmethodID>(reinterpret_cast<char*>(old_method)+32*n);}return JVMTI_ERROR_NONE;}
jvmtiError GuardMethodName(jvmtiEnv*,jmethodID id,char** name,char** signature,char**){const auto n=PublicMethodIndex(id);*name=strdup(public_method_names[n]);*signature=strdup("()I");return JVMTI_ERROR_NONE;}
jvmtiError GuardMethodFlags(jvmtiEnv*,jmethodID id,jint* flags){*flags=jint(public_method_flags[PublicMethodIndex(id)]);return JVMTI_ERROR_NONE;}
jvmtiError GuardFields(jvmtiEnv*,jclass c,jint* count,jfieldID** ids){*count=c==reinterpret_cast<jclass>(old_class)?4:0;*ids=nullptr;if(*count){*ids=static_cast<jfieldID*>(std::malloc(4*sizeof(jfieldID)));for(size_t n=0;n<4;++n)(*ids)[n]=reinterpret_cast<jfieldID>(old_fields[n]);}return JVMTI_ERROR_NONE;}
size_t PublicFieldIndex(jfieldID id){for(size_t n=0;n<4;++n)if(id==reinterpret_cast<jfieldID>(old_fields[n]))return n;std::abort();}
jvmtiError GuardFieldName(jvmtiEnv*,jclass,jfieldID id,char** name,char** signature,char**){const auto n=PublicFieldIndex(id);*name=strdup(names[n]);*signature=strdup(types[n]);return JVMTI_ERROR_NONE;}
jvmtiError GuardFieldFlags(jvmtiEnv*,jclass,jfieldID id,jint* flags){*flags=PublicFieldIndex(id)==0?8:1;return JVMTI_ERROR_NONE;}
jvmtiError GuardLoader(jvmtiEnv*,jclass c,jobject* out){*out=c==reinterpret_cast<jclass>(parent_class)?nullptr:reinterpret_cast<jobject>(guard_expected_loader);return JVMTI_ERROR_NONE;}
jvmtiError GuardClassFlags(jvmtiEnv*,jclass c,jint* flags){const auto& name=Public(c).descriptor;*flags=name.find("HierarchyTag;")!=std::string::npos||name.find("HierarchyMore;")!=std::string::npos?0x601:1;return JVMTI_ERROR_NONE;}
jvmtiError GuardInterfaces(jvmtiEnv*,jclass c,jint* count,jclass** values){const auto& p=Public(c);*count=jint(p.interfaces.size());*values=nullptr;if(*count){*values=static_cast<jclass*>(std::malloc(*count*sizeof(jclass)));for(size_t n=0;n<p.interfaces.size();++n)(*values)[n]=reinterpret_cast<jclass>(p.interfaces[n]);}return JVMTI_ERROR_NONE;}
jclass GuardParent(JNIEnv*,jclass c){Jni();return reinterpret_cast<jclass>(Public(c).parent);}
jboolean GuardAssignable(JNIEnv*,jclass c,jclass input){Jni();if(c==input)return true;for(auto* p=Public(c).parent;p;p=Public(p).parent)if(p==input)return true;return false;}
jboolean GuardProxy(JNIEnv*,jclass,jmethodID,const jvalue*){Jni();return false;}
jobject GuardPublicObject(JNIEnv*,jobject,jmethodID,const jvalue*){Jni();return nullptr;}
jobjectArray GuardNewArray(JNIEnv*,jsize count,jclass,jobject){Jni();auto* array=heap.New(12+size_t(count)*4);Put32(array,8,uint32_t(count));guard_arrays.push_back(array);return reinterpret_cast<jobjectArray>(array);}
bool IsGuardArray(void* a){return std::find(guard_arrays.begin(),guard_arrays.end(),a)!=guard_arrays.end();}
jsize GuardLength(JNIEnv* e,jarray a){return IsGuardArray(a)?jsize(Read32(a,8)):Length(e,a);}
jobject GuardGet(JNIEnv* e,jobjectArray a,jsize i){if(!IsGuardArray(a))return Element(e,a,i);Jni();assert(i>=0&&i<jsize(Read32(a,8)));return reinterpret_cast<jobject>(uintptr_t(Read32(a,12+size_t(i)*4)));}
void GuardSet(JNIEnv*,jobjectArray a,jsize i,jobject value){Jni();assert(IsGuardArray(a)&&i>=0&&i<jsize(Read32(a,8))&&uintptr_t(value)<=UINT32_MAX);Put32(a,12+size_t(i)*4,uint32_t(uintptr_t(value)));}
bool GuardScope(void*,JNIEnv* e,jobject root,runnable_jni::Operation operation,void* data){(void)root;assert(!stopped&&current==thread.data()&&Read32(current)==0);return operation({current,e,reinterpret_cast<JavaVM*>(&runtime)},data);}
bool GuardDecode(void*,jobject ref,void** out){*out=ref;return ref;}
bool GuardOracle(void*,const runnable_jni::Context&,jobject ref,void** out){return GuardDecode(nullptr,ref,out);}
void* GuardElement(void*,void* array,size_t index){assert(IsGuardArray(array)&&index<Read32(array,8));return reinterpret_cast<void*>(uintptr_t(Read32(array,12+index*4)));}
bool GuardStageField(void*,JNIEnv*,jclass c,jfieldID id,bool is_static,std::string*){assert(c==reinterpret_cast<jclass>(old_class));const auto n=PublicFieldIndex(id);assert(is_static==(n==0));return mirrors[n].field==old_fields[n];}
void* GuardField(void*,jclass c,jfieldID id){return c==reinterpret_cast<jclass>(old_class)?old_fields[PublicFieldIndex(id)]:nullptr;}
void* GuardNativeField(void* p,void* c,jfieldID id){return GuardField(p,reinterpret_cast<jclass>(c),id);}
void* GuardMethod(void*,jmethodID id){(void)PublicMethodIndex(id);return id;}
void* GuardMethodClass(void*,void* method){return Root(method);}
bool GuardSameMethod(void*,void* a,void* b){return SameIndex(a,b);}
bool GuardReadable(void*,const void* p,size_t bytes){size_t extent=0;return method_identity::ReadableMappingExtent(p,&extent)&&extent>=bytes;}
bool GuardExtent(void*,const void* p,size_t* extent){return method_identity::ReadableMappingExtent(p,extent);}
void ConfigureDefiningGraph(std::vector<unsigned char>& bytes,const modern_field_metadata::FieldMetadata& metadata){
 auto* file=static_cast<dex_mapping_abi::Prefix*>(heap.New(sizeof(dex_mapping_abi::Prefix)));
 *file={nullptr,bytes.data(),bytes.size(),bytes.data(),bytes.size()};
 auto* cache=heap.New(112);const uint64_t pointer=uintptr_t(file);std::memcpy(static_cast<char*>(cache)+16,&pointer,8);
 for(const auto& c:public_classes){if(c.native==old_class)continue;
  bool matched=false;const uint32_t count=Read32(bytes.data(),96);
  for(uint32_t i=0;i<count;++i){evolution::MappedDexMetadata mapping{bytes.data(),bytes.size(),bytes.data(),bytes.size(),i};evolution::ClassSchema schema;std::string error;
   assert(evolution::ReadMappedDexDeclarations(mapping,&schema,&error));if(schema.descriptor!=c.descriptor)continue;
   auto* native=static_cast<class_metadata::Class17*>(c.native);native->dex_cache=uint32_t(uintptr_t(cache));native->dex_class_def_index=int32_t(i);
   native->dex_type_index=int32_t(Read32(bytes.data(),Read32(bytes.data(),100)+size_t(i)*32));
   uint32_t flags=0;assert(metadata.DefiningClassFlags(native,&flags,&error)&&flags==schema.access);
   const auto saved=native->dex_class_def_index;native->dex_class_def_index=int32_t(count);assert(!metadata.DefiningClassFlags(native,&flags,&error));native->dex_class_def_index=saved;
   const auto saved_cache=native->dex_cache;native->dex_cache=old_class->dex_cache;native->dex_class_def_index=0;
   assert(!metadata.DefiningClassFlags(native,&flags,&error));native->dex_cache=saved_cache;native->dex_class_def_index=saved;
   // Raw ART implementation flags cannot replace the immutable source flags.
   const auto raw_flags=native->access_flags;native->access_flags^=0x10;
   assert(metadata.DefiningClassFlags(native,&flags,&error)&&flags==schema.access);native->access_flags=raw_flags;
   matched=true;break;
  }assert(matched);
 }
}
void ConfigurePublicGuard(JNINativeInterface_& jni,jvmtiInterface_1& ti){jni.NewObjectArray=GuardNewArray;jni.GetArrayLength=GuardLength;jni.SetObjectArrayElement=GuardSet;jni.GetObjectArrayElement=GuardGet;jni.GetSuperclass=GuardParent;jni.IsAssignableFrom=GuardAssignable;jni.CallStaticBooleanMethodA=GuardProxy;jni.CallObjectMethodA=GuardPublicObject;ti.GetClassModifiers=GuardClassFlags;ti.GetClassMethods=GuardMethods;ti.GetMethodName=GuardMethodName;ti.GetMethodModifiers=GuardMethodFlags;ti.GetClassFields=GuardFields;ti.GetFieldName=GuardFieldName;ti.GetFieldModifiers=GuardFieldFlags;ti.GetClassLoader=GuardLoader;ti.GetImplementedInterfaces=GuardInterfaces;}
}
namespace {
source_commit::Plan* matched_plan=nullptr;
void MatchedStatus(void* klass,uint8_t status){assert(stopped&&matched_plan&&klass==matched_plan->new_classes[0]&&status==15);Put32(klass,offsetof(class_metadata::Class17,status),uint32_t(status)<<28);events.push_back("status");}
void MatchedRetain(void* ext,void* old){assert(stopped&&matched_plan&&ext==class_ext&&old==matched_plan->old_classes[0]);events.push_back("retain");}
void MatchedObsolete(void* receiver,void* method){assert(stopped&&receiver==&linker&&matched_plan);bool owned=false;for(size_t n=0;n<3;++n)owned|=method==reinterpret_cast<char*>(old_method)+32*n;assert(owned);events.push_back("obsolete");}
void MatchedReplace(void* self,const void* raw){assert(stopped&&self==thread.data()&&matched_plan);const auto& map=*static_cast<const object_replacement::object_map_abi::Map*>(raw);assert(map.size==1);const auto* node=static_cast<const object_replacement::object_map_abi::Node*>(map.sentinel.next);assert(node&&node->key==matched_plan->old_classes[0]&&node->value==matched_plan->new_classes[0]&&!node->next);events.push_back("replace");replaced=true;for(size_t n=0;n<3;++n)reinterpret_cast<abi::arm64::android12::Method12*>(reinterpret_cast<char*>(old_method)+32*n)->declaring_class=uint32_t(uintptr_t(node->value));for(auto* field:old_fields)field->declaring_class=uint32_t(uintptr_t(node->value));Put32(matched_plan->new_classes_root,12,uint32_t(uintptr_t(node->value)));}
class MatchedHooks final:public OwnerHooks{public:void* FindSymbol(const char* lib,const char* name)override{if(std::strstr(name,"SetStatusLocked"))return reinterpret_cast<void*>(MatchedStatus);if(std::strstr(name,"SetObsoleteClass"))return reinterpret_cast<void*>(MatchedRetain);if(std::strstr(name,"SetEntryPointsForObsoleteMethod"))return reinterpret_cast<void*>(MatchedObsolete);if(std::strstr(name,"ReplaceReferences"))return reinterpret_cast<void*>(MatchedReplace);return OwnerHooks::FindSymbol(lib,name);}};
}
