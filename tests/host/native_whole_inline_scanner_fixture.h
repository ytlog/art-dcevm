#pragma once
#include "native_whole_inline_public_fixture.h"
#include "platform/common/native_boot_aot_source.h"
#include "native_source_suffix_receipt_fixture.h"
namespace whole_inline_fixture {
struct alignas(16) Stack {std::array<unsigned char,96>runtime{};BootQuick callee{},caller{},outer{};uintptr_t sentinel=0;}stack;
alignas(8)std::array<std::array<uint32_t,2>,3>headers{};
std::array<std::array<uint32_t,8>,3>maps{};
std::array<uint32_t,6>inline_rows{};std::array<uint32_t,2>dex_maps{{1,0}};
uint32_t mask=3;std::array<uint32_t,2>catalog{{1,3}};std::array<uint32_t,3>root_masks{{4,4,0}};
abi::arm64::android16::ManagedStack fragment{};uintptr_t terminal_pc=0;
bool break_adjacent=false;
size_t ordinal(const void*header){for(size_t i=0;i<headers.size();++i)if(header==headers[i].data())return i;std::abort();}
uintptr_t pc(size_t i){return uintptr_t(headers[i].data())+36;}
void Info(abi::arm64::android13::CodeInfo*info,const void*header){const auto i=ordinal(header);*info={};info->flags=1;info->frame_size=4;info->code_size=64;info->dex_regs=1;
 BootTable(info->stack_maps,maps[i].data(),sizeof(maps[i])*8,1);
 info->stack_masks.region={&root_masks[i],0,3};info->stack_masks.rows=1;info->stack_masks.columns[0]=0;info->stack_masks.columns[1]=3;
 info->dex_masks.region={&mask,0,i==2?2u:1u};info->dex_masks.rows=1;info->dex_masks.columns[0]=0;info->dex_masks.columns[1]=uint16_t(i==2?2:1);
 BootTable(info->dex_maps,dex_maps.data(),sizeof(dex_maps)*8,2);BootTable(info->dex_catalog,catalog.data(),64,1);
 if(i==2)BootTable(info->inline_infos,inline_rows.data(),sizeof(inline_rows)*8,1);
}
const void*Header(void*method,uintptr_t actual){for(size_t i=0;i<3;++i)if(actual==pc(i)&&method==(i==0?callee:i==1?caller:outer))return headers[i].data();return nullptr;}
bool Async(void*method,uintptr_t actual){assert(Header(method,actual));return method!=caller;}
bool Jit(const void*actual){return uintptr_t(actual)==pc(2);}
native_runtime_frame::FrameInfo FrameInfo(const void*raw){const auto*v=static_cast<const abi::arm64::stack::Visitor13*>(raw);assert(locked);return v->quick_frame==reinterpret_cast<void**>(stack.runtime.data())?native_runtime_frame::FrameInfo{96,0x7ff00000u,0}:native_runtime_frame::FrameInfo{64,0,0};}
void Walk(void*raw){auto*v=static_cast<abi::arm64::stack::Visitor13*>(raw);const auto emit=[&](void*method,void**quick,const void*header,uintptr_t actual,uint32_t end=0){boot_visit_method=method;v->quick_frame=quick;v->header=header;v->pc=actual;v->inline_frames.begin.row=0;v->inline_frames.end.row=end;++boot_frame_walks;return v->VisitFrame();};
 if(v->thread==thread.data()||v->thread==&boot_empty){emit(nullptr,nullptr,nullptr,0);return;}assert(v->thread==foreign_thread.data());
 if(!emit(boot_upcall_method,reinterpret_cast<void**>(stack.runtime.data()),nullptr,0))return;
 if(!emit(callee,reinterpret_cast<void**>(&stack.callee),headers[0].data(),pc(0)))return;
 if(break_adjacent&&!emit(nullptr,nullptr,nullptr,0))return;
 if(!emit(caller,reinterpret_cast<void**>(&stack.caller),headers[1].data(),pc(1)))return;
 if(!emit(handler,reinterpret_cast<void**>(&stack.outer),headers[2].data(),pc(2),1))return;
 if(!emit(outer,reinterpret_cast<void**>(&stack.outer),headers[2].data(),pc(2)))return;
 emit(nullptr,reinterpret_cast<void**>(&stack.sentinel),nullptr,terminal_pc);
}
// Original root-call temporary carriers preallocated to avoid ASan fake-stack
// regions appearing after the ordinary mapping snapshot. Every callback writes
// fresh values; this storage supplies extents, never root ownership.
struct RootTemporary {void*declaring=nullptr;void*value=nullptr;br::JavaFrameInfo declaring_info{},value_info{};void**declaring_slots[1]{};void**value_slots[1]{};}root_temporaries[3];
void Roots(br::Visitor*visitor){
 for(size_t i=0;i<3;++i){auto*quick=i==0?&stack.callee:i==1?&stack.caller:&stack.outer;
  boot_original_cursor.thread=foreign_thread.data();boot_original_cursor.quick_frame=reinterpret_cast<void**>(quick);boot_original_cursor.header=headers[i].data();boot_original_cursor.pc=pc(i);boot_original_cursor.context=&boot_original_context;
  auto&temporary=root_temporaries[i];temporary.declaring=Root(quick->method);temporary.declaring_info={{nullptr,br::kJavaFrameRoot,19},&boot_original_cursor,br::kDeclaringClassVreg};temporary.declaring_slots[0]=&temporary.declaring;visitor->VisitRoots(temporary.declaring_slots,1,temporary.declaring_info.base);
  if(i<2){temporary.value=reinterpret_cast<void*>(uintptr_t(quick->value));temporary.value_info={{nullptr,br::kJavaFrameRoot,19},&boot_original_cursor,br::kImpreciseVreg};temporary.value_slots[0]=&temporary.value;visitor->VisitRoots(temporary.value_slots,1,temporary.value_info.base);}
 }
}
struct Reader final:source_handle_roots::StoppedReader {
 source_handle_roots::Pool&pool;const native_source_stop::Session*stop=nullptr;
 explicit Reader(source_handle_roots::Pool&p):pool(p){}
 bool FinalRootWindow()const noexcept override{return stop&&stop->IsCollectedStop(thread.data());}
 bool ReadStoppedRoot(const void*token,void**out)const noexcept override{return FinalRootWindow()&&pool.ReadStopped(*this,token,out);}
};
struct NativeRegistry {source_handle_roots::Pool&pool;Reader&reader;
 static bool Insert(void*raw,void*p,size_t*out){auto&s=*static_cast<NativeRegistry*>(raw);auto*slot=s.pool.Add(p,nullptr);if(!slot)return false;*out=uintptr_t(slot);return true;}
 static bool Read(void*raw,size_t slot,void**out){auto&s=*static_cast<NativeRegistry*>(raw);return s.reader.FinalRootWindow()?s.reader.ReadStoppedRoot(reinterpret_cast<void*>(slot),out):s.pool.Read(reinterpret_cast<void*>(slot),out);}
 static uint64_t Epoch(void*raw){return static_cast<NativeRegistry*>(raw)->pool.ReadyForSourceAccess()?1:0;}
 static void Clear(void*){}
};
void Test(const std::vector<unsigned char>&bytes,const std::vector<unsigned char>&interface_dex,const std::vector<unsigned char>&receiver_dex,
 const source_handle_roots::Service&roots,const native_source_stop::Service&source,const native_cache::Service&threads,const whole_process_frames::Service&scanner,
 const native_typed_heap_walk::RootProvenance&provenance,const native_exception_entry_ownership::Proof&native_owner,
 const modern_field_metadata::FieldMetadata&metadata,const method_identity::Calibration&model,const native_abstract_instances::Service&vtables,
 const receiver_guard::SourceProducer&producer,const runnable_jni::Context&ordinary,jvmtiEnv*ti,size_t card,bool unsafe,std::string*error){
 const uint32_t entry_state=Read32(thread.data());struct RestoreSourceState{uint32_t state;~RestoreSourceState(){Put32(thread.data(),0,state);}}restore_source_state{entry_state};
 native_boot_method_catalog::Catalog public_catalog;Prepare(bytes,interface_dex,receiver_dex,metadata,ti,public_catalog);
 assert(native_upcall_elf_load(UPCALL_ELF_FIXTURE)==0);terminal_pc=native_upcall_elf_return_pc("art_quick_invoke_stub");assert(terminal_pc);
 native_jni_exception_calibration::Service exception;boot_upcall_java::Calibrate(exception,&env,error);
 OwnerRunnable(thread.data());error->clear();
 source_handle_roots::Pool pool(roots);Reader reader(pool);native_boot_method_catalog::Rooted rooted;
 if(!rooted.Import(public_catalog,ordinary,roots,pool,&metadata,[](const void*m,void*p)noexcept{return static_cast<const modern_field_metadata::FieldMetadata*>(m)->MethodDeclaringClass(p);},error)){void*thread_env=nullptr;std::memcpy(&thread_env,thread.data()+card+offsetof(abi::arm64::android_common::HandleThreadPrefix,jni_env),8);std::cerr<<"Inline root import: "<<*error<<" state="<<Read32(thread.data())<<" env="<<ordinary.env<<" actualenv="<<thread_env<<" self="<<ordinary.self<<" current="<<current<<" roots="<<public_catalog.Roots().size()<<"\n";for(auto root:public_catalog.Roots()){void*value=nullptr;std::cerr<<" root "<<root<<" borrow="<<roots.BorrowOwnedStrongGlobal(ordinary,root,&value)<<" value="<<value<<"\n";}std::abort();}
 source_commit::Plan plan;plan.mirror=old_class;plan.old_classes={old_class};plan.new_classes={new_class};
 NativeRegistry native{pool,reader};prepared_roots::Registry registry({});assert(registry.BindNative(&native,NativeRegistry::Insert,NativeRegistry::Read,NativeRegistry::Clear,NativeRegistry::Epoch));assert(registry.Reserve(16));
 native_frame_root_relocation::Proof proof;assert(proof.PrepareIdentity(plan,registry.Collect())&&registry.Freeze());const auto resolver=registry.Final();
 whole_process_frames::Service::Prepared storage;assert(scanner.Prepare(&storage)&&scanner.PrepareRecoveryPlan(plan,&storage));storage.source_code=&producer;storage.boot_catalog=&rooted;storage.boot_reader=&reader;storage.managed_stack_source=&roots;storage.exception_calibration=&exception;storage.exception_vm=reinterpret_cast<JavaVM*>(&runtime);storage.upcall_native_owner=&native_owner;
 stack={};stack.callee.method=callee;stack.caller.method=caller;stack.outer.method=outer;stack.callee.value=stack.caller.value=uint32_t(uintptr_t(receiver));
 auto*runtime_method=new(heap.New(sizeof(abi::arm64::android12::Method12)))abi::arm64::android12::Method12{};runtime_method->dex_method_index=UINT32_MAX;boot_upcall_method=runtime_method;std::memcpy(stack.runtime.data(),&boot_upcall_method,8);
 const auto pc0=pc(0),pc1=pc(1),pc2=pc(2);std::memcpy(stack.runtime.data()+88,&pc0,8);std::memcpy(stack.callee.tail.data()+40,&pc1,8);std::memcpy(stack.caller.tail.data()+40,&pc2,8);std::memcpy(stack.outer.tail.data()+40,&terminal_pc,8);
 for(size_t i=0;i<3;++i)maps[i]={{0,0,1,0,1,i==2?1u:0u,1,1}};
 inline_rows={{0,1,0,0,0,3}}; // One actual inline callback, terminal row and cumulative registers2.
 const auto offset=card+offsetof(abi::arm64::android_common::HandleThreadPrefix,managed_stack);
 using Fragment=abi::arm64::android16::ManagedStack;auto*head=reinterpret_cast<Fragment*>(foreign_thread.data()+offset);auto*self_head=reinterpret_cast<Fragment*>(thread.data()+offset);auto*empty_head=reinterpret_cast<Fragment*>(boot_empty.data()+offset);const auto before=*head,self_before=*self_head,empty_before=*empty_head;
 *head={};*self_head={};*empty_head={};fragment={};fragment.tagged_top_quick_frame_.tagged_sp_=uintptr_t(stack.runtime.data())|1u;*head=fragment;
 boot_inline_walk=Walk;boot_inline_info=Info;boot_inline_header=Header;boot_inline_async=Async;boot_inline_jit=Jit;boot_inline_frame=FrameInfo;boot_inline_roots=Roots;
 ExplicitMappings mappings;for(void*p:std::initializer_list<void*>{heap.base,thread.data(),foreign_thread.data(),boot_empty.data(),static_cast<void*>(&stack),static_cast<void*>(&headers),static_cast<void*>(&maps),static_cast<void*>(&inline_rows),static_cast<void*>(&dex_maps),static_cast<void*>(&mask),static_cast<void*>(&catalog),static_cast<void*>(&root_masks),const_cast<unsigned char*>(bytes.data()),const_cast<unsigned char*>(interface_dex.data()),const_cast<unsigned char*>(receiver_dex.data())})mappings.Cover(p);
 mappings.Cover(root_temporaries);mappings.Cover(&mappings);mappings.Cover(&boot_original_cursor);mappings.Cover(&boot_original_context);
 modern_field_metadata::ReadContext read{&mappings,ExplicitMappings::Extent};
 struct Work {const whole_process_frames::Service&scanner;const native_cache::Service&threads;whole_process_frames::Service::Prepared&storage;modern_field_metadata::ReadContext&read;Reader&reader;prepared_roots::Registry&registry;const source_commit::Plan&plan;const modern_field_metadata::FieldMetadata&metadata;const method_identity::Calibration&model;const native_abstract_instances::Service&vtables;const receiver_guard::SourceProducer&producer;const native_boot_method_catalog::Rooted&rooted;whole_process_frames::Service::Census census{};whole_process_frames::Report report{};bool census_only=true;bool receipt_checked=false;void*relocated_code=nullptr;};
 Work work{scanner,threads,storage,read,reader,registry,plan,metadata,model,vtables,producer,rooted};
 work.relocated_code=heap.New(bytes.size());std::memcpy(work.relocated_code,bytes.data(),bytes.size());
 const auto inspect=[](void*raw,native_source_stop::Session&stop)noexcept{auto&w=*static_cast<Work*>(raw);w.reader.stop=&stop;assert(w.registry.Resolve());const auto os=method_identity::MappingOsQueries();forbid_provider_allocation=true;
  if(w.census_only)w.census=w.scanner.CensusRecovery(stop,thread.data(),w.threads,w.storage,w.read);
  else {native_boot_aot_unaffected::Source continuation(w.plan,stop,thread.data(),w.rooted,w.reader,w.metadata,w.model,w.vtables,w.producer);native_interface_dispatch::Source dispatch(w.metadata,method_metadata::kAndroid12Onward,w.rooted,w.reader);if(!w.receipt_checked){TestSourceSuffixReceipt(continuation,w.plan,stop,w.rooted,w.reader,w.metadata,w.model,w.vtables,w.producer,w.read,w.storage,caller,w.relocated_code);w.receipt_checked=true;}w.storage.boot_unaffected=&continuation;w.storage.continuation=&continuation;w.storage.dispatch=&dispatch;w.report=w.scanner.ScanRecovery(stop,thread.data(),w.threads,&jit_cache,w.storage,w.plan,w.read);assert(w.report.aot_verify_failure&&!std::strcmp(w.report.aot_verify_failure,"boot-AOT-current-reference-roots-not-empty")&&w.report.aot_verify_thread==uintptr_t(foreign_thread.data())&&w.report.aot_verify_method==uintptr_t(caller)&&w.report.aot_verify_dex_pc==0);w.storage.boot_unaffected=nullptr;w.storage.continuation=nullptr;w.storage.dispatch=nullptr;}
  assert(method_identity::MappingOsQueries()==os);forbid_provider_allocation=false;w.registry.Invalidate();w.reader.stop=nullptr;return true;};
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 if(!work.census.accepted)std::cerr<<"Inline Census failure: "<<work.census.failure<<" method="<<work.census.diagnostic.failure_method<<" frame="<<work.census.diagnostic.failure_frame_bytes<<" bit="<<work.census.diagnostic.failure_stack_bit<<"\n";assert(work.census.accepted&&work.census.logical_frames>work.census.frames&&work.census.slots==2);assert(proof.ReserveFramesSlots(work.census.frames,work.census.slots));assert(storage.PrepareContinuations(work.census.logical_frames,work.census.code_units,work.census.tries,work.census.handlers));assert(scanner.PrepareUpcalls(work.census,roots,&storage));storage.frame_roots=&proof;storage.root_provenance=&provenance;storage.frame_root_resolver=&resolver;work.census_only=false;
 for(size_t attempt=0;attempt<2;++attempt){native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));if(work.report.accepted==unsafe)std::cerr<<"Inline scan unsafe="<<unsafe<<" reason="<<work.report.failure<<" method="<<work.report.failure_method<<" roots="<<work.report.root_failure<<" stream="<<work.report.root_stream_failure<<" slots="<<work.report.root_slots<<"\n";if(unsafe){assert(!work.report.accepted&&!std::strcmp(work.report.failure,"continuation-dependency-owner-required"));assert(work.report.failure_method==uintptr_t(handler));}else assert(work.report.accepted&&work.report.unaffected_aot_frames==1&&work.report.inlined_frames==1&&work.report.root_slots==2);}
 if(!unsafe){
  const auto run=[&]{native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));};
  const auto flags=Read32(callee,4);Put32(callee,4,flags|8u);run();assert(!work.report.accepted&&!std::strcmp(work.report.failure,"whole-continuation-actual-younger-receiver-unproved")&&work.report.failure_method==uintptr_t(caller)&&work.report.younger_method==uintptr_t(callee)&&work.report.receiver_state==1&&work.report.receiver_slot==0);Put32(callee,4,flags);
  dex_maps[0]=0;run();assert(!work.report.accepted&&!std::strcmp(work.report.failure,"whole-continuation-actual-younger-receiver-unproved")&&work.report.failure_method==uintptr_t(caller)&&work.report.receiver_state==5&&work.report.receiver_location_kind==-1&&work.report.receiver_slot==0);dex_maps[0]=1;
  break_adjacent=true;run();assert(!work.report.accepted&&!std::strcmp(work.report.failure,"whole-continuation-actual-younger-receiver-unproved")&&work.report.failure_method==uintptr_t(caller)&&work.report.previous_kind==uint32_t(native_callee_edge::Kind::Transition)&&work.report.receiver_state==0&&work.report.receiver_slot==0);break_adjacent=false;
  run();assert(work.report.accepted&&work.report.root_slots==2&&work.report.unaffected_aot_frames==1); // Failure did not leave a reusable sealed frame/root receipt.
 }
 boot_inline_walk=nullptr;boot_inline_info=nullptr;boot_inline_header=nullptr;boot_inline_async=nullptr;boot_inline_jit=nullptr;boot_inline_frame=nullptr;boot_inline_roots=nullptr;*head=before;*self_head=self_before;*empty_head=empty_before;native_upcall_elf_unload();public_catalog.Clear(&env);public_classes.erase(std::remove_if(public_classes.begin(),public_classes.end(),[](const auto&role){return std::find(classes.begin(),classes.end(),role.native)!=classes.end();}),public_classes.end());methods.clear();classes.clear();callee=caller=handler=outer=receiver=nullptr;
 std::cout<<"Actual complete whole logical inline "<<(unsafe?"unsafe catch refusal":"safe catch continuation")<<" + same Plan registered roots/Source namespaces/IfTable dispatch PASS\n";
}
}
