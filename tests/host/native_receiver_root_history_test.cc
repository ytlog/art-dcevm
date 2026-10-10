#include "platform/common/native_receiver_root_bounds.h"
static bool HistoryStrong(const artdcevm::runnable_jni::Context&,jobject,void**);
namespace { void RunHistory(const artdcevm::modern_field_metadata::FieldMetadata&,const artdcevm::runnable_jni::Context&,void*,jobject,void*); }
#define ARTDCEVM_ORIGINAL_INGRESS_TEST
#define RunIngressTest(source,context,member,receiver,declaring) RunHistory(metadata,context,member,receiver,declaring)
#include "native_receiver_entry_source_test.cc"
#undef RunIngressTest
static bool HistoryStrong(const runnable_jni::Context&c,jobject root,void**out){if((c.self!=&self&&c.self!=actual_thread)||c.env!=&env||c.vm!=&vm||!root||!out)return false;*out=Resolve(root);return true;}
namespace {
std::map<void*,const char*> history_names;
std::map<void*,void*> history_edges;
void*history_positive=nullptr,*history_interface=nullptr;int interfaces_id,parent_id;bool history_expire=false;
const char*HistoryDescriptor(void*k,void*){auto i=history_names.find(k);return i==history_names.end()?Descriptor(k,nullptr):i->second;}
abi::arm64::android11::ReleaseObjectPointer HistoryLeaf(void*k){auto i=history_edges.find(k);if(history_expire)Put32(actual_thread,0,92u<<24);return {uintptr_t(i==history_edges.end()?nullptr:i->second)};}
abi::arm64::android11::ReleaseObjectPointer HistoryDirect(void*,uint32_t){std::abort();}
struct HistoryHooks:EntryHooks{void*FindSymbol(const char*l,const char*n)override{
 if(!std::strcmp(n,abi::arm64::android_common::kObsoleteClass))return reinterpret_cast<void*>(HistoryLeaf);
 if(!std::strcmp(n,abi::arm64::android_common::kDirectInterface))return reinterpret_cast<void*>(HistoryDirect);
 if(std::strstr(n,"Class13GetDescriptor"))return reinterpret_cast<void*>(HistoryDescriptor);
 return EntryHooks::FindSymbol(l,n);
}};
void HistoryDex(void*k,const char*name,uint32_t flags){
 auto*data=static_cast<unsigned char*>(Allocate(512));std::memcpy(data,"dex\n035",8);
 Put32(data,32,512);Put32(data,36,112);Put32(data,40,0x12345678);Put32(data,56,1);Put32(data,60,112);Put32(data,64,1);Put32(data,68,116);Put32(data,96,1);Put32(data,100,120);Put32(data,112,160);Put32(data,116,0);Put32(data,120,0);Put32(data,124,flags);Put32(data,128,UINT32_MAX);
 auto len=std::strlen(name);assert(len<128);data[160]=len;std::memcpy(data+161,name,len+1);
 auto*file=Allocate(sizeof(dex_mapping_abi::Prefix));dex_mapping_abi::Prefix prefix{nullptr,data,SIZE_MAX,data,512};std::memcpy(file,&prefix,sizeof(prefix));
 auto*cache=Allocate(24);Put64(cache,offsetof(abi::arm64::android11::LegacyCache,dex_file),uintptr_t(file));Put32(k,offsetof(class_metadata::Class11,dex_cache),uint32_t(uintptr_t(cache)));
}
void RunHistory(const modern_field_metadata::FieldMetadata&,const runnable_jni::Context&context,void*member,jobject receiver,void*declaring){
 HistoryHooks hooks;std::string error;modern_field_metadata::FieldMetadata metadata;
 std::array<modern_field_metadata::FieldOracle,2>oracle;
 for(size_t n=0;n<2;++n){auto*f=static_cast<Mirror*>(public_fields[n])->field;const auto&t=identities.at(f);oracle[n]={reinterpret_cast<jobject>(public_fields[n]),t.first.c_str(),t.second.c_str(),n?9u:1u,static_cast<field_array::Field*>(f)->dex_field_index};}
 assert(metadata.Bind(hooks,&error)&&metadata.Calibrate({&self,&env,&vm},reinterpret_cast<jobject>(declaring),"LBootstrap;",oracle.data(),2,&error));
 thread_suspend_service::Service state;assert(thread_suspend_service::Service::Bind(thread_suspend_service::Declaration::kModern24,[&](const char*n){return hooks.FindSymbol("libart.so",n);},&state));
 receiver_guard::EntrySource entry;receiver_guard::EntrySource::Binding binding{&manager,reinterpret_cast<JavaVM*>(&vm),Current,Strong,With,Bounds,&manager};
 receiver_guard::EntrySource::Samples samples;
 for(size_t n=0;n<2;++n){samples.methods[n]=env.NewGlobalRef(reinterpret_cast<jobject>(method_objects[n]));samples.fields[n]=env.NewGlobalRef(reinterpret_cast<jobject>(public_fields[n]));samples.receivers[n]=env.NewGlobalRef(reinterpret_cast<jobject>(public_receivers[n]));}
 assert(entry.Bind(hooks,binding,metadata,state,&error));Put32(actual_thread,0,92u<<24);assert(entry.Calibrate(&env,samples,&error));Put32(actual_thread,0,0);
 auto*object=Allocate(160),*array=Allocate(160),*iface=Allocate(160),*old=Allocate(160),*replacement=Allocate(160),*positive=Allocate(160);
 history_positive=positive;history_interface=iface;history_names[object]="Ljava/lang/Object;";history_names[iface]="LInterface;";history_names[old]="LTarget;";history_names[replacement]="LTarget;";
 Put32(declaring,0,uint32_t(uintptr_t(declaring)));Put32(array,0,uint32_t(uintptr_t(declaring)));Put32(array,offsetof(class_metadata::Class11,component_type),uint32_t(uintptr_t(object)));
 for(auto*k:{object,iface,old,replacement,positive}){Put32(k,0,uint32_t(uintptr_t(declaring)));Put32(k,class_metadata::k17.status,15u<<28);auto*t=Allocate(k==positive?20:12);Put32(t,0,uint32_t(uintptr_t(array)));Put32(k,offsetof(class_metadata::Class11,iftable),uint32_t(uintptr_t(t)));if(k==positive){Put32(t,8,2);Put32(t,12,uint32_t(uintptr_t(iface)));}}
 Put32(iface,class_metadata::k17.access_flags,0x601);Put32(old,offsetof(class_metadata::Class11,super_class),uint32_t(uintptr_t(object)));Put32(replacement,offsetof(class_metadata::Class11,super_class),uint32_t(uintptr_t(object)));
 HistoryDex(object,"Ljava/lang/Object;",0);HistoryDex(old,"LTarget;",0);HistoryDex(replacement,"LTarget;",0);
 auto table=*env.functions;table.FindClass=[](JNIEnv*,const char*n)->jclass{Jni();return reinterpret_cast<jclass>(!std::strcmp(n,"java/util/ArrayList")?history_positive:bootstrap);};
 table.GetMethodID=[](JNIEnv*,jclass,const char*n,const char*)->jmethodID{Jni();return reinterpret_cast<jmethodID>(!std::strcmp(n,"getInterfaces")?&interfaces_id:&parent_id);};
 table.CallObjectMethodV=[](JNIEnv*,jobject root,jmethodID id,va_list)->jobject{Jni();if(id==reinterpret_cast<jmethodID>(&parent_id))return nullptr;auto*k=Resolve(root);auto*a=Allocate(20);if(k==history_positive){Put32(a,8,1);Put32(a,12,uint32_t(uintptr_t(history_interface)));}return reinterpret_cast<jobject>(a);};
 table.GetArrayLength=[](JNIEnv*,jarray a)->jsize{Jni();return int32_t(*reinterpret_cast<uint32_t*>(static_cast<char*>(Resolve(a))+8));};
 table.GetObjectArrayElement=[](JNIEnv*,jobjectArray a,jsize n)->jobject{Jni();return reinterpret_cast<jobject>(Root(static_cast<char*>(Resolve(a))+12+size_t(n)*4));};
 table.DeleteLocalRef=[](JNIEnv*,jobject a){Jni();handles.erase(a);};auto*prior=env.functions;env.functions=&table;
 receiver_guard::ClassSource classes;assert(classes.Bind(hooks,metadata,Current,&error));assert(classes.CalibrateInterfaces(context,reinterpret_cast<jobject>(iface),&error));env.functions=prior;
 Put32(public_receivers[0],0,uint32_t(uintptr_t(replacement)));history_edges[replacement]=old;history_edges[old]=nullptr;
 std::array<void*,129> deep{};for(auto&k:deep){k=Allocate(160);std::memcpy(k,old,108);history_edges[k]=nullptr;}for(size_t n=1;n<deep.size();++n)history_edges[deep[n-1]]=deep[n];
 receiver_guard::OriginalRootBounds bounds(entry,metadata);assert(bounds.BindHistory(hooks)&&!bounds.BindHistory(hooks));
 abi::common::RunnableJniScope actual{context.self,context.env,context.vm};modern_field_metadata::ReadContext read;size_t extent=0;
 no_alloc=true;bounds_enabled=false;
 assert(bounds.Enter(actual,member,receiver,&read)&&read.extent(read.owner,old,&extent)&&extent==108);
 {receiver_guard::EntrySource::InvocationScope scope(entry,context,read);assert(scope.Valid());receiver_guard::ClassSource::ReadWindow window(classes,read);receiver_guard::Relations relations(window.Access());void*normalized=nullptr;assert(relations.Normalize(old,replacement,&normalized)&&normalized==replacement);}
 auto expired=read;bounds.Leave();assert(!expired.extent(expired.owner,old,&extent));
 assert(bounds.EnterField(actual,static_cast<Mirror*>(public_fields[0])->field,receiver,&read));bounds.Leave();
 history_edges[replacement]=deep[0];assert(!bounds.Enter(actual,member,receiver,&read));history_edges[replacement]=reinterpret_cast<void*>(1);assert(!bounds.Enter(actual,member,receiver,&read));history_edges[replacement]=old;
 history_names[old]="LWrong;";assert(!bounds.Enter(actual,member,receiver,&read));history_names[old]="LTarget;";
 history_edges[old]=replacement;assert(!bounds.Enter(actual,member,receiver,&read));history_edges[old]=nullptr;
 history_edges[replacement]=replacement;assert(!bounds.Enter(actual,member,receiver,&read));history_edges[replacement]=old;
 auto*loader=public_receivers[1];Put32(old,offsetof(class_metadata::Class11,class_loader),uint32_t(uintptr_t(loader)));assert(!bounds.Enter(actual,member,receiver,&read));Put32(old,offsetof(class_metadata::Class11,class_loader),0);
 Put32(old,class_metadata::k17.dex_type_index,1);assert(!bounds.Enter(actual,member,receiver,&read));Put32(old,class_metadata::k17.dex_type_index,0);
 Put32(old,class_metadata::k17.primitive_type,1);assert(!bounds.Enter(actual,member,receiver,&read));Put32(old,class_metadata::k17.primitive_type,0);
 Put32(old,class_metadata::k17.access_flags,0x40000);assert(!bounds.Enter(actual,member,receiver,&read));Put32(old,class_metadata::k17.access_flags,0);
 history_expire=true;assert(!bounds.Enter(actual,member,receiver,&read));history_expire=false;Put32(actual_thread,0,0);
 auto foreign=actual;foreign.vm=nullptr;assert(!bounds.Enter(foreign,member,receiver,&read));
 assert(bounds.Enter(actual,member,receiver,&read));bounds.Leave();no_alloc=false;bounds_enabled=true;
 Put32(public_receivers[0],0,uint32_t(uintptr_t(declaring)));for(size_t n=0;n<2;++n){env.DeleteGlobalRef(samples.methods[n]);env.DeleteGlobalRef(samples.fields[n]);env.DeleteGlobalRef(samples.receivers[n]);}
 std::puts("Original named ClassExt history roots + real calibrated EntrySource/ClassSource bounded Normalize newReceiver->oldReceiver: method/field shared inventory, cycle/self/foreign loader/malformed DEX/primitive/proxy/expired scope refusal; no JNI/new/maps PASS; Guard activation not claimed");
}
}
