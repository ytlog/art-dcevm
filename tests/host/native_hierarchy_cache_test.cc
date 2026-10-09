#include "platform/common/native_hierarchy_cache.h"
#include "platform/common/method_cache_lookup.h"
#include <cassert>
#include <cstdlib>
#include <new>
#include <cstdio>
using namespace artdcevm;
using namespace native_hierarchy_cache;
static bool prohibit_allocation=false;
void* operator new(size_t n){if(prohibit_allocation)std::abort();if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p) noexcept{std::free(p);}
void operator delete(void*p,size_t) noexcept{std::free(p);}
struct RootSlot{void*value;};
struct Tables {
 alignas(16) type_cache::Pair types[1024]{};
 alignas(16) unsigned char fields[16384]{},methods[16384]{};
 uint32_t type_full[4]{};
 std::atomic<uintptr_t> field_full[4]{},method_full[4]{};
};
struct Cache {
 alignas(8) unsigned char bytes[112]{};
 RootSlot root{bytes};Tables tables;
 void Init(dex_cache_metadata::Model model,PairFormat fmt,bool incoming){
 auto ptr=[](const void*p){return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));};
 if(model==dex_cache_metadata::Model::Pairs){auto*c=reinterpret_cast<type_cache::Legacy*>(bytes);c->types=ptr(tables.types);c->fields=ptr(tables.fields);c->methods=ptr(tables.methods);c->num_types=c->num_fields=c->num_methods=1024;}
 else{auto*c=reinterpret_cast<type_cache::Dual*>(bytes);c->types=ptr(tables.types);c->types_array=ptr(tables.type_full);c->fields=ptr(tables.fields);c->fields_array=ptr(tables.field_full);c->methods=ptr(tables.methods);c->methods_array=ptr(tables.method_full);}
 tables.types[1]={0x1000,1};tables.type_full[1]=0x1000;
 for(auto pair:{std::pair<void*,uint32_t>{tables.fields,2},{tables.methods,3}}){
 if(fmt==PairFormat::Ordinary){auto*p=static_cast<method_cache::Pair*>(pair.first);p[pair.second]={incoming?uintptr_t{0x999}:uintptr_t{0xabc}+pair.second,pair.second};}
 else{auto*p=static_cast<method_cache::SequencePair*>(pair.first);p[pair.second].key.store((uint64_t{7}<<32)|pair.second);p[pair.second].method.store(incoming?0x999:0xabc+pair.second);}
 }
 tables.field_full[2]=incoming?0x999:0xabe;tables.method_full[3]=incoming?0x999:0xabf;
 }
};
// JNI/native leaf fixture doubles are explicit. All cache objects/tables and
// fixed declaration reads/rewrite/reset below are the actual common engine.
struct Mapper final:SourceMapper {
 struct Range{const void*p;size_t n;};std::vector<Range>ranges;
 RootSlot old_class{reinterpret_cast<void*>(0x1000)},new_class{reinterpret_cast<void*>(0x2000)};
 Cache calibration,incoming;dex_cache_metadata::Model model;PairFormat format;
 bool staged=true,private_writer=true,frozen=true,commit=true,barrier=true;int barriers=0,begins=0;void* moved_cache=nullptr;
 native_hierarchy_graph::Prepared expected;Source source;
 Mapper(dex_cache_metadata::Model m,PairFormat f):model(m),format(f){
 calibration.Init(m,f,false);incoming.Init(m,f,true);
 size_t extent=m==dex_cache_metadata::Model::Pairs?108:112;
 for(auto*c:{&calibration,&incoming}){ranges.push_back({c->bytes,extent});ranges.push_back({&c->tables,sizeof(c->tables)});}
 source={&incoming.root,this,&incoming,extent,{4,4,4}};
 static evolution::ClassSchema declarations;
 expected={this,old_class.value,new_class.value,incoming.bytes,nullptr,0,&declarations};
 }
 void*Get(Root r)const noexcept override{return r?static_cast<RootSlot*>(r)->value:nullptr;}
 bool Readable(const void*p,size_t n)const noexcept override{auto a=reinterpret_cast<uintptr_t>(p);for(auto r:ranges){auto b=reinterpret_cast<uintptr_t>(r.p);if(a>=b&&a-b<=r.n&&n<=r.n-(a-b))return true;}return false;}
 bool VerifyStaged(const native_hierarchy_graph::Graph&,const std::vector<native_hierarchy_graph::Input>&,std::string*)const override{return staged;}
 bool Describe(const native_hierarchy_graph::Prepared&p,Source*out,std::string*)const override{if(p.source_owner!=this)return false;*out=source;return true;}
 bool FindSource(const native_hierarchy_graph::Prepared&p,uint32_t index,Root*out,std::string*)const override{if(p.source_owner!=this||index!=1)return false;*out=const_cast<RootSlot*>(&new_class);return true;}
 bool OwnsUnpublished(const native_hierarchy_graph::Prepared&p,const Source&s)const noexcept override{return private_writer&&p.source_owner==this&&s.private_token==&incoming&&s.dex_identity==this&&s.cache_root==&incoming.root;}
 bool Barrier(Root root)const noexcept override{auto*self=const_cast<Mapper*>(this);++self->barriers;assert(root==&incoming.root);auto* c=reinterpret_cast<const type_cache::Dual*>(incoming.bytes);
 if(model==dex_cache_metadata::Model::DualArrays&&!c->types)assert(incoming.tables.type_full[1]==reinterpret_cast<uintptr_t>(new_class.value));
 else assert(incoming.tables.types[1].object==reinterpret_cast<uintptr_t>(new_class.value));return barrier;}
 bool RefreshPrepared(native_hierarchy_graph::Prepared*p)const noexcept override{if(p->source_owner!=this)return false;p->old_class=old_class.value;p->new_class=new_class.value;p->cache=Get(source.cache_root);return true;}
 bool VerifyFrozen(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&p,const Source&s)const noexcept override{return frozen&&p.source_owner==this&&p.new_class==new_class.value&&p.cache==Get(s.cache_root)&&s.dex_identity==this&&s.private_token==&incoming;}
 bool VerifyCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override{return commit;}
 void BeginCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override{++const_cast<Mapper*>(this)->begins;}
 Witness witness(){return {1,2,3,&old_class,reinterpret_cast<void*>(0xabe),reinterpret_cast<void*>(0xabf)};}
};
void Test(dex_cache_metadata::Model model,PairFormat fmt){
 Mapper m(model,fmt);Owner owner;std::string error;native_hierarchy_graph::Graph graph;source_commit::Plan plan;
 assert(!owner.VerifyStaged(graph,{},&error));
 auto witness=m.witness();auto wrong=witness;wrong.native_method=reinterpret_cast<void*>(0x123);assert(!owner.Calibrate(m,&m.calibration.root,m.source.cache_extent,{4,4,4},wrong,&error));
 assert(owner.Calibrate(m,&m.calibration.root,m.source.cache_extent,{4,4,4},witness,&error));assert(owner.VerifyStaged(graph,{},&error));
 m.private_writer=false;assert(!owner.Prepare(m.expected,&error));assert(m.incoming.tables.types[1].object==0x1000);m.private_writer=true;
 assert(owner.Prepare(m.expected,&error));assert(m.barriers==1);assert(m.calibration.tables.types[1].object==0x1000);
 assert(owner.VerifyPrepared(graph,m.expected,&error));assert(owner.Verify(graph,plan));
 m.new_class.value=reinterpret_cast<void*>(0x3000);m.incoming.tables.types[1].object=0x3000;if(model==dex_cache_metadata::Model::DualArrays)m.incoming.tables.type_full[1]=0x3000;
 // Move the managed DexCache, keeping the ART native arrays stable.
 alignas(8) unsigned char moved[112];std::memcpy(moved,m.incoming.bytes,112);m.ranges.push_back({moved,m.source.cache_extent});m.incoming.root.value=moved;
 prohibit_allocation=true;
 assert(owner.Refresh());assert(owner.Verify(graph,plan));owner.BeginCommit(graph,plan);assert(m.begins==1);
 m.incoming.tables.types[1].index=2;assert(!owner.Verify(graph,plan));m.incoming.tables.types[1].index=1;
 m.frozen=false;assert(!owner.Verify(graph,plan));m.frozen=true;
 m.incoming.tables.field_full[2]=0x999;if(model==dex_cache_metadata::Model::DualArrays)assert(!owner.Verify(graph,plan));
 prohibit_allocation=false;
 if(fmt==PairFormat::Sequence){Mapper locked(model,fmt);Owner refused;assert(refused.Calibrate(locked,&locked.calibration.root,locked.source.cache_extent,{4,4,4},locked.witness(),&error));auto*p=reinterpret_cast<method_cache::SequencePair*>(locked.incoming.tables.methods);p[3].key.store(method_cache::kSequenceLock|3);assert(!refused.Prepare(locked.expected,&error));assert(locked.barriers==0);}
 Mapper missing(model,fmt);Owner absent;missing.ranges.pop_back();assert(absent.Calibrate(missing,&missing.calibration.root,missing.source.cache_extent,{4,4,4},missing.witness(),&error));assert(!absent.Prepare(missing.expected,&error));
 Mapper failed(model,fmt);Owner aborted;assert(aborted.Calibrate(failed,&failed.calibration.root,failed.source.cache_extent,{4,4,4},failed.witness(),&error));failed.barrier=false;assert(!aborted.Prepare(failed.expected,&error));assert(!aborted.Verify(graph,plan));
}
void FullOnlyIncoming(){
 Mapper m(dex_cache_metadata::Model::DualArrays,PairFormat::Sequence);Owner owner;std::string error;
 assert(owner.Calibrate(m,&m.calibration.root,m.source.cache_extent,{4,4,4},m.witness(),&error));
 auto* cache=reinterpret_cast<type_cache::Dual*>(m.incoming.bytes);
 cache->types=cache->fields=cache->methods=0;
 assert(owner.Prepare(m.expected,&error)&&m.incoming.tables.type_full[1]==0x2000&&m.incoming.tables.field_full[2]==0&&m.incoming.tables.method_full[3]==0);
 // Unallocated pair backing memory is untouched, and every active full entry
 // participates in the actual final proof; small user DEX needs no pairs.
 assert(m.incoming.tables.types[1].object==0x1000);
 assert(owner.VerifyPrepared({},m.expected,&error)&&owner.Verify({},{}));
 m.incoming.tables.method_full[3]=0x999;assert(!owner.Verify({},{}));
 Mapper no_pairs(dex_cache_metadata::Model::DualArrays,PairFormat::Sequence);Owner refused;
 auto* calibration=reinterpret_cast<type_cache::Dual*>(no_pairs.calibration.bytes);
 calibration->types=calibration->fields=calibration->methods=0;
 assert(!refused.Calibrate(no_pairs,&no_pairs.calibration.root,no_pairs.source.cache_extent,{4,4,4},no_pairs.witness(),&error));
 assert(error.find("matches=0")!=std::string::npos&&error.find("typePairs=0 typeFull=1")!=std::string::npos);
}
int main(){FullOnlyIncoming();for(auto m:{dex_cache_metadata::Model::Pairs,dex_cache_metadata::Model::DualArrays})for(auto f:{PairFormat::Ordinary,PairFormat::Sequence})Test(m,f);std::puts("PASS cache model/format calibration, owned source rewrite, moving roots, allocation-free final refusal");}
