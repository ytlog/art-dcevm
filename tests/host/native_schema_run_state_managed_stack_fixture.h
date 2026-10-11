#pragma once
// Actual calibrated Service, registered ThreadList, and original Session stop.
// These are readonly declaration facts; scanner/CodeView retain frame authority.
void TestManagedStackReads(const source_handle_roots::Service&roots,
 const native_source_stop::Service&source,const native_cache::Service&threads,size_t calibrated_card){
 using Fragment=abi::arm64::android16::ManagedStack;
 using Snapshot=source_handle_roots::ManagedStackSnapshot;
 assert(roots.Calibrated());
 const size_t offset=calibrated_card+offsetof(abi::arm64::android_common::HandleThreadPrefix,managed_stack);
 assert(offset+sizeof(Fragment)<=foreign_thread.size());
 auto*head=reinterpret_cast<Fragment*>(foreign_thread.data()+offset);
 Fragment saved;std::memcpy(&saved,head,sizeof(saved));
 alignas(16) std::array<unsigned char,32> quick{};
 alignas(8) Fragment chain[4]{};
 alignas(8) std::array<unsigned char,512> foreign{};
 chain[0].tagged_top_quick_frame_.tagged_sp_=uintptr_t(quick.data());chain[0].link_=&chain[1];
 chain[1].tagged_top_quick_frame_.tagged_sp_=uintptr_t(quick.data())|1u;chain[1].link_=&chain[2];
 chain[2].tagged_top_quick_frame_.tagged_sp_=uintptr_t(quick.data())|2u;chain[2].link_=&chain[3];
 *head={};head->link_=&chain[0];
 ExplicitMappings maps;maps.Cover(thread.data());maps.Cover(foreign_thread.data());maps.Cover(chain);maps.Cover(foreign.data());
 struct Request{const source_handle_roots::Service&roots;const native_cache::Service&threads;
  ExplicitMappings&maps;Fragment*head;Fragment*chain;void*foreign;void*quick;size_t assertions=0;};
 Request request{roots,threads,maps,head,chain,foreign.data(),quick.data()};
 const auto read=[](void*raw,native_source_stop::Session&stop)noexcept{
  auto&r=*static_cast<Request*>(raw);modern_field_metadata::ReadContext context{&r.maps,ExplicitMappings::Extent};
  const auto os=method_identity::MappingOsQueries();forbid_provider_allocation=true;
  Snapshot value{};assert(r.roots.ReadStoppedManagedStack(stop,thread.data(),foreign_thread.data(),r.threads,context,&value));
  assert(value.address==r.head&&!value.tagged_quick&&value.link==r.chain&&!value.shadow);++r.assertions;
  const auto complete=managed_stack_source::Count(context,value.address);assert(complete.complete&&complete.fragments==5&&complete.terminal==r.chain+3);++r.assertions;
  for(size_t i=0;i<4;++i){assert(r.roots.ReadStoppedManagedFragment(stop,thread.data(),r.chain+i,context,&value));assert(value.tagged_quick==(i<3?(uintptr_t(r.quick)|(i?i:0)):0));++r.assertions;}
  source_handle_roots::Service uncalibrated;
  assert(!uncalibrated.ReadStoppedManagedStack(stop,thread.data(),foreign_thread.data(),r.threads,context,&value));
  assert(!r.roots.ReadStoppedManagedStack(stop,foreign_thread.data(),foreign_thread.data(),r.threads,context,&value));
  assert(!r.roots.ReadStoppedManagedStack(stop,thread.data(),r.foreign,r.threads,context,&value));
  boot_duplicate_thread=true;assert(!r.roots.ReadStoppedManagedStack(stop,thread.data(),foreign_thread.data(),r.threads,context,&value));boot_duplicate_thread=false;
  assert(!r.roots.ReadStoppedManagedStack(stop,thread.data(),foreign_thread.data(),r.threads,{},&value));
  r.maps.denied=foreign_thread.data();assert(!r.roots.ReadStoppedManagedStack(stop,thread.data(),foreign_thread.data(),r.threads,context,&value));r.maps.denied=nullptr;
  r.maps.reject_all=true;assert(!r.roots.ReadStoppedManagedFragment(stop,thread.data(),r.chain,context,&value));r.maps.reject_all=false;
  struct Short {static bool Extent(void*,const void*,size_t*out){*out=sizeof(Fragment)-1;return true;}};
  modern_field_metadata::ReadContext short_context{&r,Short::Extent};assert(!r.roots.ReadStoppedManagedStack(stop,thread.data(),foreign_thread.data(),r.threads,short_context,&value));assert(!r.roots.ReadStoppedManagedFragment(stop,thread.data(),r.chain,short_context,&value));
  auto original=r.chain[0];
  for(uintptr_t invalid:{uintptr_t{1},uintptr_t{3},uintptr_t(r.quick)|3u,uintptr_t(r.quick)+8u}){r.chain[0].tagged_top_quick_frame_.tagged_sp_=invalid;assert(!r.roots.ReadStoppedManagedFragment(stop,thread.data(),r.chain,context,&value));}
  r.chain[0]=original;r.chain[0].top_shadow_frame_=r.foreign;assert(!r.roots.ReadStoppedManagedFragment(stop,thread.data(),r.chain,context,&value));
  r.chain[0]=original;r.chain[0].link_=reinterpret_cast<Fragment*>(uintptr_t(r.chain)+1);assert(!r.roots.ReadStoppedManagedFragment(stop,thread.data(),r.chain,context,&value));
  r.chain[0]=original;r.chain[3].link_=r.chain;assert(!managed_stack_source::Count(context,r.head).complete);r.chain[3].link_=nullptr;
  // Fresh reads follow the actual replaced head/link, not a retained snapshot.
  r.head->link_=r.chain+3;assert(r.roots.ReadStoppedManagedStack(stop,thread.data(),foreign_thread.data(),r.threads,context,&value)&&value.link==r.chain+3);
  assert(managed_stack_source::Count(context,value.address).fragments==2);r.head->link_=r.chain;
  assert(method_identity::MappingOsQueries()==os);forbid_provider_allocation=false;return true;
 };
 native_source_stop::Session inspection(source);assert(inspection.Inspect(thread.data(),thread.size(),&request,read));
 assert(request.assertions==6&&!gc&&!stopped&&!disabled);
 Snapshot after{};const modern_field_metadata::ReadContext context{&maps,ExplicitMappings::Extent};
 assert(!roots.ReadStoppedManagedStack(inspection,thread.data(),foreign_thread.data(),threads,context,&after));
 assert(!roots.ReadStoppedManagedFragment(inspection,thread.data(),chain,context,&after));
 std::memcpy(head,&saved,sizeof(saved));
 std::cout<<"Calibrated SourceHandle Service + real Session registered ManagedStack/Fragment facts, JNI tags/empty tail/fresh head, foreign/duplicate/expired/span/cycle refusals noalloc/noOS PASS\n";
}
