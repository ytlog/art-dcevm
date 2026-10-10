#include "platform/common/native_receiver_root_bounds.h"
static void RunRootBoundsTest(artdcevm::receiver_guard::EntrySource&,const artdcevm::modern_field_metadata::FieldMetadata&,const artdcevm::runnable_jni::Context&,void*,jobject,void*,artdcevm::receiver_guard::EntrySource&);
#define ARTDCEVM_ORIGINAL_INGRESS_TEST
#define RunIngressTest(source,context,member,receiver,declaring) RunRootBoundsTest(source,metadata,context,member,receiver,declaring,no_transition)
#include "native_receiver_entry_source_test.cc"
#undef RunIngressTest
#include "platform/common/modern_method_guards.cc"
static void RunRootBoundsTest(receiver_guard::EntrySource&entry,const modern_field_metadata::FieldMetadata&metadata,const runnable_jni::Context&context,void*member,jobject receiver,void*declaring,receiver_guard::EntrySource&no_transition){
 // Real calibrated metadata/EntrySource above; only ART storage and actual JNI
 // leaves are simulated. These are source-owned Class fields, not bounds mocks.
 auto*table=Allocate(20);auto*iface=Allocate(160);auto*cache=Allocate(24);
 auto*file=Allocate(sizeof(dex_mapping_abi::Prefix));auto*data=static_cast<unsigned char*>(Allocate(160));
 std::memcpy(data,"dex\n035",8);Put32(data,32,160);
 dex_mapping_abi::Prefix dex{nullptr,data,SIZE_MAX,data,160};std::memcpy(file,&dex,sizeof(dex));
 Put64(cache,offsetof(abi::arm64::android11::LegacyCache,dex_file),uintptr_t(file));
 Put32(declaring,offsetof(class_metadata::Class11,dex_cache),uint32_t(uintptr_t(cache)));
 Put32(declaring,offsetof(class_metadata::Class11,iftable),uint32_t(uintptr_t(table)));
 Put32(table,8,2);Put32(table,12,uint32_t(uintptr_t(iface)));
 receiver_guard::OriginalRootBounds bounds(entry,metadata);
 abi::common::RunnableJniScope actual{context.self,context.env,context.vm};
 modern_field_metadata::ReadContext read{};size_t n=0;
 // Ordinary map calibration is unavailable during the actual borrow. JNI and
 // native allocation trap globally; original named source leaves remain usable.
 bounds_enabled=false;no_alloc=true;
 assert(bounds.Enter(actual,member,receiver,&read));
 assert(read.extent(read.owner,actual.self,&n)&&n==4);
 assert(read.extent(read.owner,member,&n)&&n==8);
 assert(read.extent(read.owner,declaring,&n)&&n==108);
 assert(read.extent(read.owner,public_receivers[0],&n)&&n==8);
 assert(read.extent(read.owner,iface,&n)&&n==108);
 assert(read.extent(read.owner,table,&n)&&n==20);
 assert(read.extent(read.owner,data+120,&n)&&n==40);
 assert(!read.extent(read.owner,public_receivers[1],&n));
 assert(!read.extent(read.owner,static_cast<char*>(member)+8,&n));
 assert(!read.extent(read.owner,data+160,&n));
 modern_field_metadata::ReadContext nested;assert(!bounds.Enter(actual,member,receiver,&nested));
 auto access=entry.Leaves();void*out=nullptr;uint32_t flags=0;
 {receiver_guard::EntrySource::InvocationScope scope(entry,context,read);assert(scope.Valid());
  assert(access.reference(access.owner,actual.self,receiver,&out)&&out==public_receivers[0]);
  assert(access.member(access.owner,member,false,&flags,&out)&&out==declaring);
  assert(access.object_class(access.owner,public_receivers[0],&out)&&out==declaring);
  field_array::View fields;assert(metadata.Fields(declaring,false,&fields,nullptr,&read)&&fields.count==2&&fields.selected_count==1);
 }
 auto expired=read;bounds.Leave();assert(!expired.extent(expired.owner,declaring,&n));
 assert(!bounds.Enter(actual,reinterpret_cast<void*>(1),receiver,&read));
 auto old=actual;actual.vm=nullptr;assert(!bounds.Enter(actual,member,receiver,&read));actual=old;
 Put32(actual.self,0,92u<<24);assert(!bounds.Enter(actual,member,receiver,&read));Put32(actual.self,0,0);
 receiver_guard::EntrySource::OriginalRoots seeds;
 // Both instance/static already-resolved original ArtMethods are source roots.
 assert(entry.SeedOriginalRoots(actual,method_members[0],receiver,&seeds)&&seeds.declaring==declaring);
 assert(entry.SeedOriginalRoots(actual,method_members[1],receiver,&seeds)&&seeds.declaring==declaring);
 auto foreign=actual;foreign.self=&manager;assert(!entry.SeedOriginalRoots(foreign,member,receiver,&seeds));
 foreign=actual;foreign.env=nullptr;assert(!entry.SeedOriginalRoots(foreign,member,receiver,&seeds));
 foreign=actual;foreign.vm=nullptr;assert(!entry.SeedOriginalRoots(foreign,member,receiver,&seeds));
 auto before=decodes;
 assert(!no_transition.SeedOriginalRoots(actual,member,reinterpret_cast<jobject>(uintptr_t(receiver)&~uintptr_t(3)),&seeds)&&decodes==before);
 // Original weak arguments never reach the potentially waiting decoder.
 auto weak=reinterpret_cast<jobject>((uintptr_t(receiver)&~uintptr_t(3))|3);before=decodes;
 assert(!bounds.Enter(actual,member,weak,&read)&&decodes==before);
 // Declared unsupported storage cannot broaden permissions via unused_size_.
 dex.data_size=111;std::memcpy(file,&dex,sizeof(dex));assert(!bounds.Enter(actual,member,receiver,&read));
 dex.data_size=160;std::memcpy(file,&dex,sizeof(dex));Put32(table,8,3);assert(!bounds.Enter(actual,member,receiver,&read));Put32(table,8,2);
 Put32(declaring,class_metadata::k17.access_flags,0x40000);assert(!bounds.Enter(actual,member,receiver,&read));Put32(declaring,class_metadata::k17.access_flags,0);
 assert(bounds.Enter(actual,member,receiver,&read));
 Put32(actual.self,0,92u<<24);assert(!read.extent(read.owner,declaring,&n));Put32(actual.self,0,0);bounds.Leave();
 // An actual original declaring GcRoot moving between calls must rebuild the
 // inventory. No old Class pointer remains authorized by a cached map/window.
 watched_method=member;moved_class=iface;
 assert(bounds.Enter(actual,member,nullptr,&read));
 assert(read.extent(read.owner,iface,&n)&&!read.extent(read.owner,declaring,&n));
 bounds.Leave();watched_method=nullptr;moved_class=nullptr;
 no_alloc=false;bounds_enabled=true;
 // Actual calibrated modern-local source path seeds its real original slot.
 auto local=env.NewLocalRef(receiver);no_alloc=true;
 assert(bounds.Enter(actual,member,local,&read));
 if(modern_inline)assert(read.extent(read.owner,reinterpret_cast<void*>(uintptr_t(local)&~uintptr_t(3)),&n)&&n==4);
 {receiver_guard::EntrySource::InvocationScope scope(entry,context,read);assert(scope.Valid()&&access.reference(access.owner,actual.self,local,&out)&&out==public_receivers[0]);}
 bounds.Leave();no_alloc=false;env.DeleteLocalRef(local);
 // The real shared IdValues wrapper now supplies its actual borrowed scope to
 // this concrete provider. Missing independent Class relation proof refuses
 // before the original body; source root bounds alone never activate Guard.
 receiver_guard::ClassSource missing_classes;
 receiver_guard::OriginalIngress ingress(entry,missing_classes,bounds);
 receiver_guard::Guard guard(ingress.Leaves());assert(ingress.Attach(guard)&&ingress.Publish()&&receiver_guard::Bind(guard));
 retirement::RetiredMethodRegistry registry;retirement::state.registry=&registry;
 retirement::state.throw_exception=Throw;retirement::state.decode=[](void*,jmethodID id){return static_cast<void*>(id);};
 static unsigned originals=0;
 retirement::original_id_values=reinterpret_cast<void*>(+[](const abi::common::RunnableJniScope&,jobject,jmethodID,const jvalue*)->uint64_t{++originals;return 99;});
 receiver_guard::Invocation call{receiver_guard::Path::JniMethod,context.self,member,public_receivers[0]};
 auto raised=raises;no_alloc=true;bounds_enabled=false;
 assert(guard.BeginProof({call,declaring,declaring,false}));
 assert(retirement::IdValues<false>(actual,receiver,reinterpret_cast<jmethodID>(member),nullptr)==0);
 assert(raises==raised+1&&originals==0&&!guard.EndProof(false,false,true)&&!guard.Active()&&!guard.Proven());
 receiver_guard::consumer.store(nullptr,std::memory_order_release);assert(ingress.RetireForTesting());
 no_alloc=false;bounds_enabled=true;
 std::puts("Actual OriginalRootBounds + proven EntrySource/FieldMetadata: freshly rooted Class/IfTable/native-field/DEX inventory, original global/local leaves and scoped reads; unknown/interior overflow/weak/native/foreign/proxy/malformed storage/expired permission refusal, zero JNI/allocation/maps PASS; Guard activation not claimed");
}
