#pragma once
#include "native_upcall_elf_extent_fixture.h"
#include "native_upcall_calibration_fixture.h"
void TestWholeManagedUpcall(const source_handle_roots::Service&roots,const native_source_stop::Service&source,
 const native_cache::Service&threads,const whole_process_frames::Service&scanner,
 const native_exception_entry_ownership::Proof&native_owner,size_t calibrated_card,JNIEnv*env,std::string*error){
 assert(native_upcall_elf_load(UPCALL_ELF_FIXTURE)==0);
 const auto invoke=native_upcall_elf_return_pc("art_quick_invoke_stub");assert(invoke);
 native_jni_exception_calibration::Service exception;boot_upcall_java::Calibrate(exception,env,error);
 using Fragment=abi::arm64::android16::ManagedStack;
 const auto offset=calibrated_card+offsetof(abi::arm64::android_common::HandleThreadPrefix,managed_stack);
 auto*head=reinterpret_cast<Fragment*>(foreign_thread.data()+offset);
 auto*self_head=reinterpret_cast<Fragment*>(thread.data()+offset);
 auto*empty_head=reinterpret_cast<Fragment*>(boot_empty.data()+offset);
 const Fragment original=*head,self_original=*self_head,empty_original=*empty_head;
 boot_upcall_mode=true;boot_upcall_return_pc=invoke;boot_upcall_stack={};
 auto*runtime_method=new(heap.New(sizeof(abi::arm64::android12::Method12)))abi::arm64::android12::Method12{};
 runtime_method->dex_method_index=UINT32_MAX;boot_upcall_method=runtime_method;
 std::memcpy(boot_upcall_stack.runtime.data(),&boot_upcall_method,8);
 const uintptr_t managed_pc=uintptr_t(boot_header)+36;std::memcpy(boot_upcall_stack.runtime.data()+88,&managed_pc,8);
 boot_upcall_stack.managed.method=boot_compiled_method;std::memcpy(boot_upcall_stack.managed.tail.data()+40,&invoke,8);
 *head={};boot_upcall_fragment={};boot_upcall_fragment.tagged_top_quick_frame_.tagged_sp_=uintptr_t(boot_upcall_stack.runtime.data())|1u;head->link_=&boot_upcall_fragment;*self_head={};*empty_head={};
 whole_process_frames::Service::Prepared storage;assert(scanner.Prepare(&storage));source_commit::Plan plan;
 assert(scanner.PrepareRecoveryPlan(plan,&storage));storage.managed_stack_source=&roots;
 storage.exception_calibration=&exception;storage.exception_vm=reinterpret_cast<JavaVM*>(&runtime);storage.upcall_native_owner=&native_owner;
 ExplicitMappings maps;maps.Cover(heap.base);maps.Cover(thread.data());maps.Cover(foreign_thread.data());maps.Cover(boot_empty.data());
 maps.Cover(&boot_upcall_fragment);maps.Cover(&boot_upcall_stack);maps.Cover(boot_header);maps.Cover(boot_stack_map.data());maps.Cover(&boot_dex_mask);maps.Cover(&boot_dex_map);maps.Cover(boot_location.data());
 modern_field_metadata::ReadContext read{&maps,ExplicitMappings::Extent};
 struct Work{const whole_process_frames::Service&scanner;const native_cache::Service&threads;whole_process_frames::Service::Prepared&storage;
  modern_field_metadata::ReadContext&read;const source_commit::Plan&plan;const native_exception_entry_ownership::Proof&native_owner;
  whole_process_frames::Service::Census census;whole_process_frames::Report report;bool census_only=true;bool boundary=false;};
 Work work{scanner,threads,storage,read,plan,native_owner,{}, {},true,false};
 const auto inspect=[](void*raw,native_source_stop::Session&stop)noexcept{
  auto&w=*static_cast<Work*>(raw);const auto queries=method_identity::MappingOsQueries();forbid_provider_allocation=true;
  assert(w.native_owner.ValidateNativeEntries(stop,thread.data()));
  if(w.census_only)w.census=w.scanner.CensusRecovery(stop,thread.data(),w.threads,w.storage,w.read);
  else {w.report=w.scanner.ScanRecovery(stop,thread.data(),w.threads,&jit_cache,w.storage,w.plan,w.read);
   w.boundary=false;for(size_t i=0;i<w.storage.upcalls_used;++i){const auto&upcall=*w.storage.upcalls[i];if(upcall.thread==foreign_thread.data())w.boundary=upcall.exhausted&&upcall.boundary.Validate(w.plan);}}
  assert(method_identity::MappingOsQueries()==queries);forbid_provider_allocation=false;return true;
 };
 {native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));}
 if(!work.census.accepted||work.census.threads!=3||work.census.frames!=6||work.census.managed_fragments!=2)std::cerr<<"Whole upcall Census: "<<work.census.failure<<" threads="<<work.census.threads<<" frames="<<work.census.frames<<" logical="<<work.census.logical_frames<<" fragments="<<work.census.managed_fragments<<"\n";
 assert(work.census.accepted&&work.census.threads==3&&work.census.frames==6&&work.census.logical_frames==6&&work.census.managed_fragments==2);
 assert(scanner.PrepareUpcalls(work.census,roots,&storage));work.census_only=false;
 const auto run=[&]{native_source_stop::Session stop(source);assert(stop.Inspect(thread.data(),thread.size(),&work,inspect));assert(!native_owner.ValidateNativeEntries(stop,thread.data()));assert(!gc&&!stopped&&!disabled);};
 const auto before=boot_enumerations;run();if(!work.report.accepted||!work.boundary)std::cerr<<"Whole upcall scan: "<<work.report.failure<<" boundary="<<work.boundary<<" runtime="<<work.report.runtime_frames<<" compiled="<<work.report.compiled_frames<<" transitions="<<work.report.transitions<<"\n";assert(work.report.accepted&&work.boundary&&work.report.runtime_frames==1&&work.report.compiled_frames==1&&work.report.transitions==4);
 assert(boot_enumerations>=before+4); // capture lock exits before fresh registered validation.
 for(int fault:{1,2,3}){boot_upcall_fault=fault;run();assert(!work.boundary);}
 boot_upcall_fault=0;run();assert(work.report.accepted&&work.boundary); // Reset failed boundary on fresh complete walk.
 const auto saved=boot_upcall_fragment.tagged_top_quick_frame_.tagged_sp_;boot_upcall_fragment.tagged_top_quick_frame_.tagged_sp_=saved+16;run();assert(!work.boundary);boot_upcall_fragment.tagged_top_quick_frame_.tagged_sp_=saved;
 auto lr=invoke;std::memcpy(boot_upcall_stack.managed.tail.data()+40,&managed_pc,8);boot_upcall_return_pc=managed_pc;run();assert(!work.boundary);
 std::memcpy(boot_upcall_stack.managed.tail.data()+40,&lr,8);boot_upcall_return_pc=lr;run();assert(work.report.accepted&&work.boundary);
 *head=original;*self_head=self_original;*empty_head=empty_original;boot_upcall_mode=false;boot_upcall_fault=0;native_upcall_elf_unload();
 std::cout<<"Actual registered calibrated Source Session whole runtime-to-AOT-to-transition, original FrameInfo/LR, closed JNI receipt/native entry owners and compiler ELF named extent PASS (host ART leaf model, not device ABI)\n";
}
