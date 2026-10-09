#include "platform/common/native_hierarchy_cache.h"
#include "platform/common/method_cache_lookup.h"
#include <limits>
#include <cstdio>
#include <cstdarg>
#include <algorithm>
namespace artdcevm::native_hierarchy_cache {
namespace {
bool Fail(std::string* e,const char* s){if(e)*e=s;return false;}
bool Span(const SourceMapper& m,const void* p,size_t n,size_t unit){
 return !n || (p && n<=SIZE_MAX/unit && m.Readable(p,n*unit));
}
bool Read(const SourceMapper& m,dex_cache_metadata::Model model,const Source& s,
 type_cache::View* t,native_cache::View* f,native_cache::View* methods){
 auto* cache=m.Get(s.cache_root);
 if(!cache || !m.Readable(cache,s.cache_extent) ||
 !type_cache::Read(model,cache,s.cache_extent,s.counts.types,t) ||
 !native_cache::Read(model,cache,s.cache_extent,native_cache::Member::Fields,s.counts.fields,f) ||
 !native_cache::Read(model,cache,s.cache_extent,native_cache::Member::Methods,s.counts.methods,methods))return false;
 return Span(m,t->pairs,t->pair_count,8)&&Span(m,t->full,t->full_count,4)&&
 Span(m,f->pairs,f->pair_count,16)&&Span(m,f->full,f->full_count,8)&&
 Span(m,methods->pairs,methods->pair_count,16)&&Span(m,methods->full,methods->full_count,8);
}
bool NativeWitness(const native_cache::View& v,PairFormat fmt,uint32_t index,void* expected){
 if(!v.pairs || index%1024>=v.pair_count || !expected)return false;
 const auto slot=index%1024;
 if(fmt==PairFormat::Ordinary){
 method_cache::Pair value;__atomic_load(static_cast<method_cache::Pair*>(v.pairs)+slot,&value,__ATOMIC_ACQUIRE);
 if(value.index!=index || value.method!=reinterpret_cast<uintptr_t>(expected))return false;
 }else{
 const auto& p=static_cast<method_cache::SequencePair*>(v.pairs)[slot];
 auto key=p.key.load(std::memory_order_acquire);
 if(key&method_cache::kSequenceLock || (key&~method_cache::kSequenceMask)!=index ||
 p.method.load(std::memory_order_acquire)!=reinterpret_cast<uintptr_t>(expected) ||key!=p.key.load(std::memory_order_relaxed))return false;
 }
 return !v.full || (index<v.full_count && v.full[index].load(std::memory_order_relaxed)==reinterpret_cast<uintptr_t>(expected));
}
bool TypeWitness(const type_cache::View& v,uint32_t index,uint32_t expected){
 if(!expected || !v.pairs || index%1024>=v.pair_count)return false;
 type_cache::Pair p;__atomic_load(v.pairs+index%1024,&p,__ATOMIC_ACQUIRE);
 return p.index==index && p.object==expected && (!v.full || (index<v.full_count && __atomic_load_n(v.full+index,__ATOMIC_RELAXED)==expected));
}
// Read-only failure evidence from the same fixed, mapping-validated views.
// No retries/spins, JNI, address search, cache writes or candidate selection.
void Append(char* text,size_t capacity,size_t*used,const char*format,...) {
 if(*used>=capacity-1)return;
 va_list args;va_start(args,format);const int n=std::vsnprintf(text+*used,capacity-*used,format,args);va_end(args);
 if(n>0)*used+=std::min(static_cast<size_t>(n),capacity-*used-1);
}
void ObserveNative(char*text,size_t capacity,size_t*used,const char*name,
 const native_cache::View&v,uint32_t index,void*expected){
 uint64_t first=0,second=0,again=0,full=0;bool pair=false,has_full=false;
 if(v.pairs&&index%1024<v.pair_count){
  const auto* value=static_cast<const method_cache::SequencePair*>(v.pairs)+index%1024;
  first=value->key.load(std::memory_order_acquire);second=value->method.load(std::memory_order_acquire);
  again=value->key.load(std::memory_order_relaxed);pair=true;
 }
 if(v.full&&index<v.full_count){full=v.full[index].load(std::memory_order_relaxed);has_full=true;}
 Append(text,capacity,used," %s{idx=%u slot=%u pair=%d words=0x%llx/0x%llx stable=%d expected=0x%llx full=%d:0x%llx ordinary=%d sequence=%d}",
  name,index,index%1024,pair,static_cast<unsigned long long>(first),static_cast<unsigned long long>(second),pair&&first==again,
  static_cast<unsigned long long>(uintptr_t(expected)),has_full,static_cast<unsigned long long>(full),
  NativeWitness(v,PairFormat::Ordinary,index,expected),NativeWitness(v,PairFormat::Sequence,index,expected));
}
void ObserveType(char*text,size_t capacity,size_t*used,const type_cache::View&v,uint32_t index,uint32_t expected){
 type_cache::Pair pair{};uint32_t full=0;bool found=false,has_full=false;
 if(v.pairs&&index%1024<v.pair_count){__atomic_load(v.pairs+index%1024,&pair,__ATOMIC_ACQUIRE);found=true;}
 if(v.full&&index<v.full_count){full=__atomic_load_n(v.full+index,__ATOMIC_RELAXED);has_full=true;}
 Append(text,capacity,used," type{idx=%u slot=%u pair=%d key=%u object=0x%x expected=0x%x full=%d:0x%x}",
  index,index%1024,found,pair.index,pair.object,expected,has_full,full);
}
bool Same(const type_cache::View&a,const type_cache::View&b){return a.pairs==b.pairs&&a.pair_count==b.pair_count&&a.full==b.full&&a.full_count==b.full_count;}
bool Same(const native_cache::View&a,const native_cache::View&b){return a.pairs==b.pairs&&a.pair_count==b.pair_count&&a.full==b.full&&a.full_count==b.full_count;}
// Unpublished tables have no concurrent writers. Refuse a held sequence lock;
// never spin/yield inside the source allocator's actual writer boundary.
bool Unlocked(const native_cache::View& v,PairFormat fmt){
 if(fmt==PairFormat::Sequence)for(uint32_t i=0;i<v.pair_count;++i)
 if(static_cast<method_cache::SequencePair*>(v.pairs)[i].key.load(std::memory_order_acquire)&method_cache::kSequenceLock)return false;
 return true;
}
void Reset(const native_cache::View& v,PairFormat fmt){
 for(uint32_t i=0;i<v.pair_count;++i){const uint64_t invalid=i==0?1:0;
 if(fmt==PairFormat::Ordinary){method_cache::Pair empty{0,invalid};__atomic_store(static_cast<method_cache::Pair*>(v.pairs)+i,&empty,__ATOMIC_RELEASE);}
 else{auto& p=static_cast<method_cache::SequencePair*>(v.pairs)[i];auto key=p.key.load(std::memory_order_relaxed);
 p.method.store(0,std::memory_order_release);p.key.store((((key&method_cache::kSequenceMask)+(uint64_t{1}<<32))&~method_cache::kSequenceLock)|invalid,std::memory_order_release);}
 }
 for(uint32_t i=0;i<v.full_count;++i)v.full[i].store(0,std::memory_order_relaxed);
}
bool Cleared(const native_cache::View& v,PairFormat fmt){
 for(uint32_t i=0;i<v.pair_count;++i){uint64_t key,value;
 if(fmt==PairFormat::Ordinary){method_cache::Pair p;__atomic_load(static_cast<method_cache::Pair*>(v.pairs)+i,&p,__ATOMIC_ACQUIRE);key=p.index;value=p.method;}
 else{const auto& p=static_cast<method_cache::SequencePair*>(v.pairs)[i];key=p.key.load(std::memory_order_acquire);value=p.method.load(std::memory_order_acquire);
 if(key&method_cache::kSequenceLock)return false;key&=~method_cache::kSequenceMask;}
 if(value || key!=(i==0?1u:0u))return false;
 }
 for(uint32_t i=0;i<v.full_count;++i)if(v.full[i].load(std::memory_order_relaxed))return false;
 return true;
}
bool Exact(const native_hierarchy_graph::Prepared&a,const native_hierarchy_graph::Prepared&b){
 return a.source_owner==b.source_owner&&a.old_class==b.old_class&&a.new_class==b.new_class&&a.cache==b.cache&&a.parent==b.parent&&a.class_index==b.class_index&&a.declarations==b.declarations;
}
}
bool Owner::Calibrate(const SourceMapper& m,Root cache,size_t extent,Counts counts,const Witness&w,std::string* error){
 if(mapper_)return Fail(error,"cache owner cannot rebind");
 const auto expected=reinterpret_cast<uintptr_t>(m.Get(w.expected_class));
 if(!cache||!expected||expected>UINT32_MAX||w.type_index>=counts.types||w.field_index>=counts.fields||w.method_index>=counts.methods)return Fail(error,"independent public cache witnesses missing");
 Source s{cache,nullptr,nullptr,extent,counts};unsigned matches=0;dex_cache_metadata::Model selected{};PairFormat format{};
 char diagnosis[2304]{};size_t used=0;
 for(auto model:{dex_cache_metadata::Model::Pairs,dex_cache_metadata::Model::DualArrays}){
 type_cache::View t;native_cache::View f,methods;
 const bool read=Read(m,model,s,&t,&f,&methods);
 const bool type=read&&TypeWitness(t,w.type_index,static_cast<uint32_t>(expected));
 unsigned formats=0;
 if(type)for(auto fmt:{PairFormat::Ordinary,PairFormat::Sequence})if(NativeWitness(f,fmt,w.field_index,w.native_field)&&NativeWitness(methods,fmt,w.method_index,w.native_method)){++matches;++formats;selected=model;format=fmt;}
 int written=std::snprintf(diagnosis+used,sizeof(diagnosis)-used,
   " %s{read=%d typePairs=%d typeFull=%d fieldPairs=%d fieldFull=%d methodPairs=%d methodFull=%d typeWitness=%d formats=%u}",
   model==dex_cache_metadata::Model::Pairs?"pairs":"dual",read,
   read&&t.pairs!=nullptr,read&&t.full!=nullptr,read&&f.pairs!=nullptr,read&&f.full!=nullptr,
   read&&methods.pairs!=nullptr,read&&methods.full!=nullptr,type,formats);
 if(written>0)used+=std::min(static_cast<size_t>(written),sizeof(diagnosis)-used-1);
 if(read){ObserveType(diagnosis,sizeof(diagnosis),&used,t,w.type_index,static_cast<uint32_t>(expected));
  ObserveNative(diagnosis,sizeof(diagnosis),&used,"field",f,w.field_index,w.native_field);
  ObserveNative(diagnosis,sizeof(diagnosis),&used,"method",methods,w.method_index,w.native_method);}

 }
 if(matches!=1){
  if(error)*error="cache declarations not uniquely calibrated by actual resolved witnesses: matches="+std::to_string(matches)+
   " IDs="+std::to_string(counts.types)+"/"+std::to_string(counts.fields)+"/"+std::to_string(counts.methods)+diagnosis;
  return false;
 }
 mapper_=&m;model_=selected;format_=format;calibration_=s;witness_=w;return true;
}
bool Owner::Witnesses()const noexcept{
 type_cache::View t;native_cache::View f,m;const auto value=mapper_?reinterpret_cast<uintptr_t>(mapper_->Get(witness_.expected_class)):0;
 return value&&value<=UINT32_MAX&&Views(calibration_,&t,&f,&m)&&TypeWitness(t,witness_.type_index,static_cast<uint32_t>(value))&&NativeWitness(f,format_,witness_.field_index,witness_.native_field)&&NativeWitness(m,format_,witness_.method_index,witness_.native_method);
}
 bool Owner::Refresh() noexcept{
 if(!mapper_)return false;
 for(auto&r:records_){auto p=r.prepared;if(!mapper_->RefreshPrepared(&p)||p.source_owner!=r.prepared.source_owner||p.class_index!=r.prepared.class_index||p.declarations!=r.prepared.declarations||mapper_->Get(r.source.cache_root)!=p.cache)return false;r.prepared=p;}
 return true;
}
 bool Owner::Views(const Source&s,type_cache::View*t,native_cache::View*f,native_cache::View*m)const noexcept{return mapper_&&Read(*mapper_,model_,s,t,f,m);}
bool Owner::Prepare(const native_hierarchy_graph::Prepared& p,std::string* error){
 if(!mapper_||!p.source_owner||!p.new_class||!p.cache||!p.declarations)return Fail(error,"source cache owner not calibrated");
 for(const auto&r:records_)if(r.prepared.source_owner==p.source_owner)return Fail(error,"source cache already transformed");
 Record r;r.prepared=p;
 if(!mapper_->Describe(p,&r.source,error)||!r.source.dex_identity||!r.source.private_token||mapper_->Get(r.source.cache_root)!=p.cache||!mapper_->OwnsUnpublished(p,r.source)||
 !Views(r.source,&r.types,&r.fields,&r.methods)||!Unlocked(r.fields,format_)||!Unlocked(r.methods,format_))return Fail(error,"unpublished incoming source cache ownership not proved");
 // Resolve all roots and allocate the complete journal before the first write.
 auto add=[&](uint32_t pos,uint32_t index,bool full){Root root=nullptr;
 if(index>=r.source.counts.types||!mapper_->RefreshPrepared(&r.prepared)||!mapper_->FindSource(r.prepared,index,&root,error)||!root)return false;
 auto value=reinterpret_cast<uintptr_t>(mapper_->Get(root));if(!value||value>UINT32_MAX)return false;
 r.slots.push_back({pos,index,root,full});return true;};
 for(uint32_t i=0;i<r.types.pair_count;++i){type_cache::Pair pair;__atomic_load(r.types.pairs+i,&pair,__ATOMIC_ACQUIRE);
 if(pair.object && (pair.index%1024!=i||!add(i,pair.index,false)))return Fail(error,"source type pair lookup not proved");}
 for(uint32_t i=0;i<r.types.full_count;++i)if(__atomic_load_n(r.types.full+i,__ATOMIC_RELAXED)&&!add(i,i,true))return Fail(error,"source full type lookup not proved");
 records_.push_back(std::move(r));auto& record=records_.back();
 // Mapping can have suspended during preparation. Recheck all current pointers
 // and owned writer context before borrowing mutable arrays.
 type_cache::View t;native_cache::View f,m;
 if(!mapper_->RefreshPrepared(&record.prepared)||!mapper_->OwnsUnpublished(record.prepared,record.source)||!Views(record.source,&t,&f,&m)||!Same(t,record.types)||!Same(f,record.fields)||!Same(m,record.methods)||!Unlocked(f,format_)||!Unlocked(m,format_)){records_.pop_back();return Fail(error,"source cache changed before writer window");}
 // Root lookup may safepoint before OwnsUnpublished establishes the writer
 // window. Prove that every occupied entry still has its journaled DEX role;
 // no newly populated slot may escape transformation.
 size_t occupied=0;
 for(uint32_t i=0;i<t.pair_count;++i){type_cache::Pair pair;__atomic_load(t.pairs+i,&pair,__ATOMIC_ACQUIRE);if(!pair.object)continue;
 bool found=false;for(const auto& slot:record.slots)if(!slot.full&&slot.position==i&&slot.index==pair.index){found=true;break;}
 if(!found){records_.pop_back();return Fail(error,"source type pair changed during preparation");}++occupied;}
 for(uint32_t i=0;i<t.full_count;++i)if(__atomic_load_n(t.full+i,__ATOMIC_RELAXED)){
 bool found=false;for(const auto& slot:record.slots)if(slot.full&&slot.position==i){found=true;break;}
 if(!found){records_.pop_back();return Fail(error,"source full type changed during preparation");}++occupied;}
 if(occupied!=record.slots.size()){records_.pop_back();return Fail(error,"source type occupancy changed during preparation");}
 for(const auto& slot:record.slots){auto value=reinterpret_cast<uintptr_t>(mapper_->Get(slot.expected));if(!value||value>UINT32_MAX){records_.pop_back();return Fail(error,"mapped source root invalid before writer window");}}
 for(const auto& slot:record.slots){auto value=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mapper_->Get(slot.expected)));
 if(slot.full)__atomic_store_n(t.full+slot.position,value,__ATOMIC_RELAXED);
 else{type_cache::Pair pair{value,slot.index};__atomic_store(t.pairs+slot.position,&pair,__ATOMIC_RELEASE);}}
 Reset(f,format_);Reset(m,format_);
 if(!mapper_->Barrier(record.source.cache_root))return Fail(error,"source DexCache write barrier failed; source operation must abort");
 record.transformed=true;return Check(record);
}
bool Owner::Check(const Record&r)const noexcept{
 type_cache::View t;native_cache::View f,m;
 if(!r.transformed||!Views(r.source,&t,&f,&m)||!Same(t,r.types)||!Same(f,r.fields)||!Same(m,r.methods)||!Cleared(f,format_)||!Cleared(m,format_))return false;
 size_t seen=0;
 for(uint32_t i=0;i<t.pair_count;++i){type_cache::Pair p;__atomic_load(t.pairs+i,&p,__ATOMIC_ACQUIRE);if(!p.object)continue;
 bool found=false;for(const auto&s:r.slots)if(!s.full&&s.position==i){auto expected=reinterpret_cast<uintptr_t>(mapper_->Get(s.expected));found=expected&&expected<=UINT32_MAX&&p.object==expected&&p.index==s.index;break;}if(!found)return false;++seen;}
 for(uint32_t i=0;i<t.full_count;++i){auto value=__atomic_load_n(t.full+i,__ATOMIC_RELAXED);if(!value)continue;
 bool found=false;for(const auto&s:r.slots)if(s.full&&s.position==i){auto expected=reinterpret_cast<uintptr_t>(mapper_->Get(s.expected));found=expected&&expected<=UINT32_MAX&&value==expected;break;}if(!found)return false;++seen;}
 return seen==r.slots.size();
}
bool Owner::VerifyStaged(const native_hierarchy_graph::Graph&g,const std::vector<native_hierarchy_graph::Input>&i,std::string* e)const{return Witnesses()&&mapper_->VerifyStaged(g,i,e);}
bool Owner::VerifyAllocation(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&p,std::string*e)const{
 if(!Witnesses()||p.new_class||!p.source_old_root)return Fail(e,"Unallocated source cache witness absent");
 Source source;type_cache::View types;native_cache::View fields,methods;
 auto actual=p;
 return mapper_->DescribeAllocation(p,&source,e)&&mapper_->RefreshAllocation(&actual)&&source.dex_identity&&source.private_token&&mapper_->Get(source.cache_root)==actual.cache&&mapper_->OwnsUnpublished(actual,source)&&Views(source,&types,&fields,&methods)&&Unlocked(fields,format_)&&Unlocked(methods,format_);
}
bool Owner::VerifyPrepared(const native_hierarchy_graph::Graph&g,const native_hierarchy_graph::Prepared&p,std::string* e)const{
 for(const auto&r:records_)if(Exact(r.prepared,p))return mapper_->VerifyFrozen(g,p,r.source)&&Check(r);
 return Fail(e,"exact transformed incoming source cache absent");
}
bool Owner::Verify(const native_hierarchy_graph::Graph&g,const source_commit::Plan&p)const noexcept{
 if(!mapper_||records_.empty()||!mapper_->VerifyCommit(g,p))return false;
 for(const auto&r:records_)if(!mapper_->VerifyFrozen(g,r.prepared,r.source)||!Check(r))return false;
 return true;
}
void Owner::BeginCommit(const native_hierarchy_graph::Graph&g,const source_commit::Plan&p)const noexcept{if(Verify(g,p))mapper_->BeginCommit(g,p);}
}
