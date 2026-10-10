#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "public_roles_fixture.inc"
#pragma clang diagnostic pop
#include "platform/common/native_interface_dispatch.h"
namespace {
void* interface_class=nullptr;std::array<void*,2> interface_methods{};
jvmtiError DispatchLoaded(jvmtiEnv*,jint*n,jclass**out){Jni();*n=1;*out=TiArray<jclass>(1);**out=reinterpret_cast<jclass>(interface_class);return JVMTI_ERROR_NONE;}
jvmtiError DispatchStatus(jvmtiEnv*,jclass,jint*out){Jni();*out=JVMTI_CLASS_STATUS_PREPARED;return JVMTI_ERROR_NONE;}
jvmtiError DispatchLoader(jvmtiEnv*,jclass,jobject*out){Jni();*out=nullptr;return JVMTI_ERROR_NONE;}
jvmtiError DispatchMethods(jvmtiEnv*,jclass,jint*n,jmethodID**out){Jni();*n=2;*out=TiArray<jmethodID>(2);for(size_t i=0;i<2;++i)(*out)[i]=reinterpret_cast<jmethodID>(uintptr_t(interface_methods[i])|1u);return JVMTI_ERROR_NONE;}
size_t MethodOrdinal(jmethodID id){for(size_t i=0;i<2;++i)if(id==reinterpret_cast<jmethodID>(uintptr_t(interface_methods[i])|1u))return i;std::abort();}
jvmtiError DispatchName(jvmtiEnv*,jmethodID id,char**name,char**desc,char**generic){Jni();*name=TiString(MethodOrdinal(id)?"staticKeep":"keep");*desc=TiString("()I");if(generic)*generic=nullptr;return JVMTI_ERROR_NONE;}
jvmtiError DispatchFlags(jvmtiEnv*,jmethodID id,jint*out){Jni();*out=0x401;(void)id;return JVMTI_ERROR_NONE;}
jvmtiError DispatchDeclaring(jvmtiEnv*,jmethodID,jclass*out){Jni();*out=reinterpret_cast<jclass>(interface_class);return JVMTI_ERROR_NONE;}
jvmtiError DispatchBytes(jvmtiEnv*,jmethodID,jint*,unsigned char**){return JVMTI_ERROR_ABSENT_INFORMATION;}
jvmtiError DispatchCaps(jvmtiEnv*,jvmtiCapabilities*out){*out={};out->can_get_bytecodes=1;return JVMTI_ERROR_NONE;}
void* DispatchCodec(void*owner,jmethodID id){assert(owner==&linker&&!stopped);return interface_methods[MethodOrdinal(id)];}
jobject DispatchReflect(JNIEnv*,jclass k,jmethodID id,jboolean is_static){Jni();const size_t i=MethodOrdinal(id);assert(k==interface_class&&!is_static);return reinterpret_cast<jobject>(public_methods[i].mirror);}
}
int main(int argc,char**argv){
 assert(argc==4);
#include "source_setup.inc"
 InterfaceFixtures();auto dex=File(argv[1]);auto interface_dex=File(argv[2]);auto receiver_dex=File(argv[3]);uint32_t invoke_index=UINT32_MAX,other_index=UINT32_MAX;
 evolution::MappedDexMetadata source_dex{dex.data(),dex.size(),dex.data(),dex.size(),0};
 for(uint32_t i=0;i<Read32(dex.data(),88);++i){evolution::MappedDexMethodReference ref;assert(evolution::ReadMappedDexMethodReference(source_dex,i,&ref,&error));if(ref.declaring_class=="LFixture;"&&ref.name=="keep")invoke_index=i;if(ref.declaring_class=="LFixture;"&&ref.name=="staticKeep")other_index=i;}assert(invoke_index!=UINT32_MAX&&other_index!=UINT32_MAX);
 interface_class=old_class;old_class->access_flags=0x601;old_class->class_loader=0;
 public_types={{"LFixture;",old_class},{"LChild;",new_class},{"J",heap.New(160)}};
 LinkDex(old_class,interface_dex);LinkDex(new_class,receiver_dex);old_class->dex_class_def_index=new_class->dex_class_def_index=0;old_class->dex_type_index=Read32(interface_dex.data(),Read32(interface_dex.data(),100));new_class->dex_type_index=Read32(receiver_dex.data(),Read32(receiver_dex.data(),100));
 auto native_index=[&](std::vector<unsigned char>& bytes,const char*owner,const char*name){evolution::MappedDexMetadata mapped{bytes.data(),bytes.size(),bytes.data(),bytes.size(),0};for(uint32_t i=0;i<Read32(bytes.data(),88);++i){evolution::MappedDexMethodReference ref;assert(evolution::ReadMappedDexMethodReference(mapped,i,&ref,&error));if(ref.declaring_class==owner&&ref.name==name)return i;}std::abort();};
 for(auto*bytes:{&dex,&interface_dex,&receiver_dex}){evolution::ClassSchema schema;assert(evolution::ReadMappedDexDeclarations({bytes->data(),bytes->size(),bytes->data(),bytes->size(),0},&schema,&error));}
 const uint32_t declaration_keep=native_index(interface_dex,"LFixture;","keep"),declaration_other=native_index(interface_dex,"LFixture;","staticKeep"),callee_keep=native_index(receiver_dex,"LChild;","keep"),callee_other=native_index(receiver_dex,"LChild;","staticKeep");
 auto*long_class=static_cast<class_metadata::Class17*>(heap.New(160));long_class->object.klass=uint32_t(uintptr_t(meta_class));long_class->component_type=uint32_t(uintptr_t(public_types["J"]));
 const auto layout=std::getenv("DISPATCH_METHOD11")?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 auto*methods=static_cast<unsigned char*>(heap.New(8+2*layout.size));Put32(methods,0,2);old_class->methods=uintptr_t(methods);
 for(size_t i=0;i<2;++i){void*m=methods+8+layout.size*i;Put32(m,0,uint32_t(uintptr_t(old_class)));Put32(m,4,0x401);Put32(m,layout.dex_method_index,i?declaration_other:declaration_keep);const uint16_t slot=uint16_t(i);const size_t offset=layout.size==40?offsetof(method_metadata::Method11,method_index):offsetof(method_metadata::Method12,method_index);std::memcpy(static_cast<unsigned char*>(m)+offset,&slot,2);interface_methods[i]=m;public_methods.push_back({m,old_class,i?"staticKeep":"keep","()I",0x401,new(heap.New(sizeof(Mirror)))Mirror{m}});}
 void*callee=heap.New(layout.size);Put32(callee,0,uint32_t(uintptr_t(new_class)));Put32(callee,4,1);Put32(callee,layout.dex_method_index,callee_keep);
 auto*pointers=heap.New(32);Put32(pointers,0,uint32_t(uintptr_t(long_class)));Put32(pointers,8,2);const uint64_t unused_target=uintptr_t(next_method);std::memcpy(static_cast<unsigned char*>(pointers)+24,&unused_target,8);const uint64_t target=uintptr_t(callee);std::memcpy(static_cast<unsigned char*>(pointers)+16,&target,8);
 auto*table_array=IfTable({old_class});Put32(table_array,16,uint32_t(uintptr_t(pointers)));new_class->iftable=uint32_t(uintptr_t(table_array));Put32(new_instance,0,uint32_t(uintptr_t(new_class)));
 jvmtiInterface_1 funcs{};jvmtiEnv ti{&funcs};funcs.GetCapabilities=DispatchCaps;funcs.GetLoadedClasses=DispatchLoaded;funcs.GetClassStatus=DispatchStatus;funcs.GetClassSignature=TiSignature;funcs.GetClassLoader=DispatchLoader;funcs.GetClassMethods=DispatchMethods;funcs.GetMethodName=DispatchName;funcs.GetMethodModifiers=DispatchFlags;funcs.GetMethodDeclaringClass=DispatchDeclaring;funcs.GetBytecodes=DispatchBytes;funcs.Deallocate=TiDelete;table.ToReflectedMethod=DispatchReflect;
 native_boot_method_catalog::Catalog catalog;native_boot_method_catalog::Codec codec{&linker,DispatchCodec,PReflectedMethod};if(!catalog.Prepare(&independent_env.env,&ti,codec,&error)){std::cerr<<"catalog: "<<error<<"\n";return 1;}assert(catalog.Complete()&&catalog.Methods().size()==2);
 Put32(thread.data(),0,0);const runnable_jni::Context context{self,&independent_env.env,&runtime};{
 source_handle_roots::Pool pool(roots);native_boot_method_catalog::Rooted rooted;assert(rooted.Import(catalog,context,roots,pool,&metadata,[](const void*m,void*method)noexcept{return static_cast<const modern_field_metadata::FieldMetadata*>(m)->MethodDeclaringClass(method);},&error));
 void*receiver_root=pool.Add(new_instance,&error);void*receiver_class_root=pool.Add(new_class,&error);void*table_root=pool.Add(table_array,&error);void*array_root=pool.Add(pointers,&error);assert(receiver_root&&receiver_class_root&&table_root&&array_root);
 FinalMapper mapper(pool);mapper.dex_begin=uintptr_t(dex.data());mapper.dex_end=mapper.dex_begin+dex.size();
 #include "dispatch_code.inc"
 auto*code=reinterpret_cast<uint16_t*>(dex.data()+kCallerCodeOffset+16);receiver_guard::CodeView caller{code,Read32(dex.data()+kCallerCodeOffset,12),1,uint32_t(Read32(dex.data(),88)),0,{dex.data(),dex.size(),dex.data(),dex.size(),0}};
 // Code is part of the explicit stopped view, independently bounded here.
 struct Read {FinalMapper*mapper;const void*code;size_t bytes;const void*denied=nullptr;size_t limit=SIZE_MAX;std::array<std::vector<unsigned char>*,2> extra;} read{&mapper,code,size_t(caller.units)*2,nullptr,SIZE_MAX,{&interface_dex,&receiver_dex}};
 modern_field_metadata::ReadContext bounded{&read,[](void*raw,const void*p,size_t*out){auto&s=*static_cast<Read*>(raw);if(p==s.denied){*out=s.limit;return s.limit!=0;}if(p==s.code){*out=s.bytes;return true;}for(auto*bytes:s.extra){const auto at=uintptr_t(p),begin=uintptr_t(bytes->data());if(at>=begin&&at<begin+bytes->size()){*out=begin+bytes->size()-at;return true;}}return s.mapper->context.extent(s.mapper,p,out);}};
 native_interface_dispatch::Source source(metadata,layout,rooted,mapper);native_interface_dispatch::Match match{};const char*failure=nullptr;
 stopped=1;const int jni_before=jni_calls;const auto os_before=method_identity::MappingOsQueries();forbid_provider_allocation=true;
 if(!source.Verify(caller,0,new_instance,callee,bounded,&match,&failure)){std::cerr<<"verify: "<<(failure?failure:"?")<<"\n";std::abort();}assert(match.interface==old_class&&match.declaration==interface_methods[0]&&match.target==callee&&match.slot==0);
 auto refuse=[&](){assert(!source.Verify(caller,0,new_instance,callee,bounded,&match,&failure)&&failure);};
 assert(!source.Verify(caller,1,new_instance,callee,bounded,&match,&failure));assert(!source.Verify(caller,0,new_instance,next_method,bounded,&match,&failure));
 const uint16_t opcode=code[0];code[0]=0x106e;refuse();code[0]=0x0178;assert(source.Verify(caller,0,new_instance,callee,bounded,&match,&failure));code[0]=opcode;
 const auto expected_index=code[1];code[1]=uint16_t(caller.method_ids);refuse();code[1]=uint16_t(other_index);refuse();code[1]=expected_index;
 const auto source_size=caller.source_dex.size;caller.source_dex.size=0;refuse();caller.source_dex.size=source_size;
 const auto proto_at=Read32(dex.data(),92)+invoke_index*8+2;uint16_t proto=0;std::memcpy(&proto,dex.data()+proto_at,2);const uint16_t other_proto=uint16_t(proto^1);std::memcpy(dex.data()+proto_at,&other_proto,2);refuse();std::memcpy(dex.data()+proto_at,&proto,2);
 for(uint32_t flags:{2u,8u,0x400u,0x10000u,0x40000u}){Put32(callee,4,1|flags);refuse();}Put32(callee,4,0);refuse();Put32(callee,4,1);
 Put32(callee,layout.dex_method_index,callee_other);refuse();Put32(interface_methods[0],layout.dex_method_index,declaration_other);refuse();Put32(interface_methods[0],layout.dex_method_index,declaration_keep);
 const uint32_t interface_cache=old_class->dex_cache,receiver_cache=new_class->dex_cache;
 old_class->dex_cache=receiver_cache;refuse();old_class->dex_cache=0;refuse();old_class->dex_cache=interface_cache;
 new_class->dex_cache=interface_cache;refuse();new_class->dex_cache=0;refuse();new_class->dex_cache=receiver_cache;
 Put32(callee,layout.dex_method_index,UINT32_MAX);refuse();Put32(callee,layout.dex_method_index,callee_keep);
 Put32(interface_methods[0],4,0x401|0x01000000);refuse();Put32(interface_methods[0],4,0x401|0x40000);refuse();Put32(interface_methods[0],4,0x409);refuse();Put32(interface_methods[0],4,0x401);
 const size_t slot_offset=layout.size==40?offsetof(method_metadata::Method11,method_index):offsetof(method_metadata::Method12,method_index);for(uint16_t wrong_slot:{1u,2u}){std::memcpy(static_cast<unsigned char*>(interface_methods[0])+slot_offset,&wrong_slot,2);refuse();}const uint16_t slot=0;std::memcpy(static_cast<unsigned char*>(interface_methods[0])+slot_offset,&slot,2);
 for(int32_t count:{-1,0,1,3}){Put32(pointers,8,uint32_t(count));refuse();}Put32(pointers,8,2);
 uint64_t zero=0;std::memcpy(static_cast<unsigned char*>(pointers)+16,&zero,8);refuse();std::memcpy(static_cast<unsigned char*>(pointers)+16,&target,8);
 const uint32_t long_class_bits=Read32(pointers,0);Put32(pointers,0,uint32_t(uintptr_t(public_array_class)));refuse();Put32(pointers,0,long_class_bits);
 long_class->class_loader=uint32_t(uintptr_t(ref_a));refuse();long_class->class_loader=0;
 old_class->class_loader=uint32_t(uintptr_t(ref_a));refuse();old_class->class_loader=0;
 old_class->access_flags=1;refuse();old_class->access_flags=0x601;
 Put32(table_array,8,3);refuse();Put32(table_array,8,2);
 read.denied=interface_dex.data();read.limit=111;refuse();read.denied=receiver_dex.data();read.limit=111;refuse();read.denied=pointers;read.limit=24;refuse();read.denied=methods;read.limit=8+layout.size;refuse();read.denied=callee;read.limit=0;refuse();read.denied=nullptr;
 mapper.active=false;refuse();mapper.active=true;
 Put32(callee,4,1|0x01000000);assert(source.Verify(caller,0,new_instance,callee,bounded,&match,&failure));
 Put32(callee,0,uint32_t(uintptr_t(old_class)));Put32(callee,layout.dex_method_index,declaration_keep);assert(source.Verify(caller,0,new_instance,callee,bounded,&match,&failure));
 Put32(callee,0,uint32_t(uintptr_t(new_class)));Put32(callee,layout.dex_method_index,callee_keep);Put32(callee,4,1);
 forbid_provider_allocation=false;stopped=0;assert(jni_calls==jni_before&&method_identity::MappingOsQueries()==os_before);
 // Actual source Handle slots, receiver/Class references and IfTable pair
 // references are updated together; evacuated copies cannot prove dispatch.
 void*moved_interface=heap.New(160);std::memcpy(moved_interface,old_class,160);void*moved_receiver_class=heap.New(160);std::memcpy(moved_receiver_class,new_class,160);void*moved_receiver=heap.New(32);std::memcpy(moved_receiver,new_instance,32);void*moved_table=heap.New(20);std::memcpy(moved_table,table_array,20);void*moved_array=heap.New(32);std::memcpy(moved_array,pointers,32);
 MoveRoots moving;for(const auto&pair:std::array<std::pair<void*,void*>,5>{{{old_class,moved_interface},{new_class,moved_receiver_class},{new_instance,moved_receiver},{table_array,moved_table},{pointers,moved_array}}}){moving.old_value=uint32_t(uintptr_t(pair.first));moving.next_value=uint32_t(uintptr_t(pair.second));OriginalRoots(self,&moving,0);}
 Put32(moved_receiver,0,uint32_t(uintptr_t(moved_receiver_class)));Put32(moved_receiver_class,offsetof(class_metadata::Class17,iftable),uint32_t(uintptr_t(moved_table)));Put32(moved_table,12,uint32_t(uintptr_t(moved_interface)));Put32(moved_table,16,uint32_t(uintptr_t(moved_array)));for(void*method:interface_methods)Put32(method,0,uint32_t(uintptr_t(moved_interface)));Put32(callee,0,uint32_t(uintptr_t(moved_receiver_class)));
 public_types["LFixture;"]=moved_interface;public_types["LChild;"]=moved_receiver_class;
 std::memset(old_class,0,160);std::memset(new_class,0,160);std::memset(new_instance,0,32);std::memset(table_array,0,20);std::memset(pointers,0,32);mapper.heap_end=uintptr_t(heap.base)+heap.used;
 stopped=1;forbid_provider_allocation=true;
 assert(mapper.Get(receiver_root)==moved_receiver&&mapper.Get(receiver_class_root)==moved_receiver_class&&mapper.Get(table_root)==moved_table&&mapper.Get(array_root)==moved_array);
 assert(source.Verify(caller,0,mapper.Get(receiver_root),callee,bounded,&match,&failure));assert(match.interface==moved_interface&&match.target==callee&&match.slot==0);
 assert(!source.Verify(caller,0,new_instance,callee,bounded,&match,&failure));mapper.active=false;assert(!source.Verify(caller,0,moved_receiver,callee,bounded,&match,&failure));mapper.active=true;

 forbid_provider_allocation=false;stopped=0;assert(jni_calls==jni_before&&method_identity::MappingOsQueries()==os_before);assert(pool.DropManagedReferences());stopped=1;assert(!source.Verify(caller,0,moved_receiver,callee,bounded,&match,&failure));stopped=0;

 }
 catalog.Clear(&env);assert(Top()==original_scope);Publish(nullptr);std::cout<<"Actual public Boot catalog + source Pool + independent DEX namespaces + exact IfTable slot + moved GC roots + no JNI/OS/allocation PASS\n";
}
