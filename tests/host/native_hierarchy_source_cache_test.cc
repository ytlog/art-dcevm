#include "source_cache_walker_fixture.inc"
#include "platform/common/native_hierarchy_source_cache.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/common/method_cache_lookup.h"
#include <fstream>
#include <iterator>
#include <new>
static bool forbid_allocation=false;
void*operator new(size_t n){if(forbid_allocation)std::abort();if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p) noexcept{std::free(p);}
void operator delete(void*p,size_t) noexcept{std::free(p);}
using namespace artdcevm::native_hierarchy_source_cache;
namespace {
const char* fixture_descriptor="LFixture;";
const char* SourceDescriptor(void* klass,void*){return klass==parent_class?"Ljava/lang/Object;":fixture_descriptor;}
class SourceHooks final:public RootHooks {public:void*FindSymbol(const char*lib,const char*name)override{if(std::strstr(name,"Class13GetDescriptor"))return reinterpret_cast<void*>(SourceDescriptor);return RootHooks::FindSymbol(lib,name);}};
class Batch final:public BatchAccess {
 public:RecordView record;source_handle_roots::Pool&pool;mutable int finds=0;bool wrong_original=false;size_t count=0;
 explicit Batch(source_handle_roots::Pool&p):pool(p){}
 size_t Count()const noexcept override{return count;}
 bool At(size_t i,RecordView*out)const noexcept override{if(i>=count)return false;*out=record;return true;}
 void*Find(const char*d,void*loader)const noexcept override{return !loader&&!std::strcmp(d,record.declarations->descriptor.c_str())?record.new_root:nullptr;}
 void*OriginalFind(const char*d,void*loader_root)const override{
 void*loader=nullptr;assert(pool.Read(loader_root,&loader)&&!loader);++finds;
 if(!std::strcmp(d,record.declarations->descriptor.c_str()))return wrong_original?parent_class:old_class;
 if(!std::strcmp(d,"Ljava/lang/Object;"))return parent_class;
 return nullptr;
 }
};
class Proof final:public RequiredServices {
 const runnable_jni::Context& context_;const source_handle_roots::Pool& pool_;
 RecordView expected_;const void* dex_;native_hierarchy_cache::Counts counts_;
 mutable const source_handle_roots::StoppedReader* inspection_=nullptr;
 mutable bool prepared_=false;mutable native_hierarchy_cache::Source receipt_{};
 bool ExactRecord(const RecordView&r)const noexcept{
  return r.source_owner==expected_.source_owner&&r.class_index==expected_.class_index&&
   r.old_root==expected_.old_root&&r.new_root==expected_.new_root&&r.cache_root==expected_.cache_root&&
   r.parent_root==expected_.parent_root&&r.declarations==expected_.declarations&&
   r.dex.begin==expected_.dex.begin&&r.dex.size==expected_.dex.size&&
   r.dex.data_begin==expected_.dex.data_begin&&r.dex.data_size==expected_.dex.data_size&&
   r.dex.class_def_index==expected_.dex.class_def_index;
 }
 bool ExactSource(const native_hierarchy_cache::Source&s)const noexcept{
  return s.cache_root==expected_.cache_root&&s.dex_identity==dex_&&s.private_token&&
   s.cache_extent>=112&&s.counts.types==counts_.types&&s.counts.fields==counts_.fields&&s.counts.methods==counts_.methods;
 }
 public:bool live=true,private_cache=true;mutable int begins=0,prepare_calls=0;
 Proof(const runnable_jni::Context&c,const source_handle_roots::Pool&p,RecordView expected,
       const void*dex,native_hierarchy_cache::Counts counts):context_(c),pool_(p),expected_(expected),dex_(dex),counts_(counts){}
 bool EnterSemanticInspection(const source_handle_roots::StoppedReader& r)const noexcept override{
  if(inspection_||!live||!r.FinalRootWindow())return false;inspection_=&r;return true;
 }
 void LeaveSemanticInspection()const noexcept override{assert(inspection_);inspection_=nullptr;}
 bool VerifyStaged(const native_hierarchy_graph::Graph&,const std::vector<native_hierarchy_graph::Input>&)const noexcept override{return live;}
 bool PreparePrivate(const RecordView&r,const native_hierarchy_cache::Source&s,std::string*error)const override{
  ++prepare_calls;
  if(!live||!private_cache||prepared_||!ExactRecord(r)||!ExactSource(s)){
   if(error)*error="Independent fixture private cache receipt refused";return false;
  }
  void*old=nullptr;void*cache=nullptr;
  if(!pool_.Read(r.old_root,&old)||old!=old_class||!pool_.Read(r.cache_root,&cache)||cache==nullptr||
     old_class->dex_cache==uint32_t(uintptr_t(cache))){if(error)*error="Independent private cache is already published";return false;}
  receipt_=s;prepared_=true;return true;
 }
 bool VerifyPrivate(const RecordView&r,const native_hierarchy_cache::Source&s,
                    const runnable_jni::Context&c,const source_handle_roots::Pool&p)const noexcept override{
  void*cache=nullptr;
  return live&&private_cache&&prepared_&&ExactRecord(r)&&ExactSource(s)&&
   s.private_token==receipt_.private_token&&s.cache_extent==receipt_.cache_extent&&
   c.self==context_.self&&c.env==context_.env&&c.vm==context_.vm&&&p==&pool_&&
   (inspection_?pool_.ReadStopped(*inspection_,r.cache_root,&cache):pool_.Read(r.cache_root,&cache))&&cache&&old_class->dex_cache!=uint32_t(uintptr_t(cache));
 }
 bool VerifyReadonlyStaged(const native_hierarchy_graph::Graph&,const RecordView&)const noexcept override{return live;}
 bool VerifyReadonlyPrepared(const native_hierarchy_graph::Graph&,const RecordView&,const native_hierarchy_graph::Prepared&)const noexcept override{return live;}
 bool VerifyReadonly(const native_hierarchy_graph::Graph&,const RecordView&,const source_commit::Plan&,size_t)const noexcept override{return live;}
 bool VerifyFrozen(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&,const native_hierarchy_cache::Source&)const noexcept override{return live;}
 bool VerifyCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override{return live;}
 void BeginCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override{++begins;}
};
// Preserve the default-denied contract when a consumer omits preparation.
class MissingPrepare final:public RequiredServices {
 const Proof& proof_;
 public:explicit MissingPrepare(const Proof&p):proof_(p){}
 bool VerifyStaged(const native_hierarchy_graph::Graph&g,const std::vector<native_hierarchy_graph::Input>&i)const noexcept override{return proof_.VerifyStaged(g,i);}
 bool VerifyPrivate(const RecordView&r,const native_hierarchy_cache::Source&s,const runnable_jni::Context&c,const source_handle_roots::Pool&p)const noexcept override{return proof_.VerifyPrivate(r,s,c,p);}
 bool VerifyReadonlyStaged(const native_hierarchy_graph::Graph&g,const RecordView&r)const noexcept override{return proof_.VerifyReadonlyStaged(g,r);}
 bool VerifyReadonlyPrepared(const native_hierarchy_graph::Graph&g,const RecordView&r,const native_hierarchy_graph::Prepared&p)const noexcept override{return proof_.VerifyReadonlyPrepared(g,r,p);}
 bool VerifyReadonly(const native_hierarchy_graph::Graph&g,const RecordView&r,const source_commit::Plan&p,size_t i)const noexcept override{return proof_.VerifyReadonly(g,r,p,i);}
 bool VerifyFrozen(const native_hierarchy_graph::Graph&g,const native_hierarchy_graph::Prepared&p,const native_hierarchy_cache::Source&s)const noexcept override{return proof_.VerifyFrozen(g,p,s);}
 bool VerifyCommit(const native_hierarchy_graph::Graph&g,const source_commit::Plan&p)const noexcept override{return proof_.VerifyCommit(g,p);}
 void BeginCommit(const native_hierarchy_graph::Graph&g,const source_commit::Plan&p)const noexcept override{proof_.BeginCommit(g,p);}
};}
#include "source_cache_calibration_main.inc"
 // The leaf descriptor/FindClass and final runtime/cleanup proof are explicit
 // fixture doubles. Field/card/root calibration, native Class/cache reads,
 // immutable DEX parsing, root pool and write barrier are the actual services.
 std::ifstream input(SOURCE_DEX_FIXTURE,std::ios::binary);assert(input.good());
 std::vector<unsigned char> dex{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
 evolution::MappedDexMetadata mapping{dex.data(),dex.size(),dex.data(),dex.size(),0};evolution::ClassSchema schema;
 assert(evolution::ReadMappedDexDeclarations(mapping,&schema,&error));fixture_descriptor=schema.descriptor.c_str();
 native_hierarchy_cache::Counts counts;assert(Inspect(mapping,schema,&counts,&error));
 auto wrong=mapping;wrong.class_def_index=UINT32_MAX;assert(!Inspect(wrong,schema,&counts,&error));wrong=mapping;wrong.size=111;assert(!Inspect(wrong,schema,&counts,&error));
 auto wrong_schema=schema;wrong_schema.superclass="LWrong;";assert(!Inspect(mapping,wrong_schema,&counts,&error));
 struct Use{source_handle_roots::Service*service;modern_field_metadata::FieldMetadata*metadata;modern_field_storage::Service*fields;evolution::ClassSchema*schema;evolution::MappedDexMetadata mapping;} use{&service,&metadata,&fields,&schema,mapping};
 auto run=[](const runnable_jni::Context&context,void*raw){auto&d=*static_cast<Use*>(raw);std::string error;source_handle_roots::Pool pool(*d.service);
  auto*cache=static_cast<unsigned char*>(heap.New(112));auto*published=heap.New(112);old_class->dex_cache=uint32_t(uintptr_t(published));new_class->dex_cache=uint32_t(uintptr_t(cache));
  auto*file=static_cast<dex_mapping_abi::Prefix*>(heap.New(48));*file={nullptr,d.mapping.begin,0xdeadbeef,d.mapping.data_begin,d.mapping.data_size};uint64_t file_pointer=uintptr_t(file);std::memcpy(cache+16,&file_pointer,8);new_class->dex_class_def_index=0;
  Batch batch(pool);batch.record={&batch,0,pool.Add(old_class,&error),pool.Add(nullptr,&error),pool.Add(cache,&error),pool.Add(parent_class,&error),d.schema,d.mapping};
  native_hierarchy_graph::Prepared p{&batch,old_class,new_class,cache,parent_class,0,d.schema,batch.record.old_root};native_hierarchy_graph::Graph graph;native_hierarchy_graph::Node node;node.klass=old_class;node.descriptor=d.schema->descriptor;node.affected=true;graph.nodes.push_back(node);
  native_hierarchy_cache::Counts expected_counts;assert(Inspect(d.mapping,*d.schema,&expected_counts,&error));
  Proof proof(context,pool,batch.record,file,expected_counts);Mapper mapper(context,pool,*d.metadata,*d.fields,batch,&proof);Mapper absent(context,pool,*d.metadata,*d.fields,batch,nullptr);
  assert(!absent.VerifyStaged(graph,{},&error));assert(mapper.VerifyStaged(graph,{},&error));
  native_hierarchy_cache::Source source;batch.count=1;p.new_class=nullptr;assert(mapper.VerifyAllocation(graph,p,&error));batch.wrong_original=true;assert(!mapper.DescribeAllocation(p,&source,&error));batch.wrong_original=false;
  MissingPrepare missing(proof);Mapper no_prepare(context,pool,*d.metadata,*d.fields,batch,&missing);
  batch.count=0;assert(no_prepare.VerifyStaged(graph,{},&error));batch.count=1;
  assert(!no_prepare.DescribeAllocation(p,&source,&error));assert(!no_prepare.RefreshAllocation(&p)&&proof.prepare_calls==0&&source.cache_root==nullptr&&source.private_token==nullptr);
  proof.private_cache=false;assert(!mapper.DescribeAllocation(p,&source,&error)&&!mapper.RefreshAllocation(&p));proof.private_cache=true;
  assert(mapper.DescribeAllocation(p,&source,&error));assert(!mapper.DescribeAllocation(p,&source,&error));
  assert(!proof.PreparePrivate(batch.record,source,&error));
  assert(proof.VerifyPrivate(batch.record,source,context,pool));
  // A real non-Runnable Thread cannot borrow ordinary source roots. The
  // preallocation identity must refresh through the actual owned stopped view.
  {
   struct AllocationStop final:source_handle_roots::StoppedReader {
    bool enabled=true;
    bool FinalRootWindow()const noexcept override{return enabled&&stopped==1&&Read32(current)!=0;}
    bool ReadStoppedRoot(const void*,void**)const noexcept override{return false;}
   } allocation_stop;
   struct AllocationMaps{const evolution::MappedDexMetadata& dex;} maps{d.mapping};
   modern_field_metadata::ReadContext bounds{&maps,[](void*raw,const void*address,size_t*out){
    const auto& m=*static_cast<AllocationMaps*>(raw);const uintptr_t value=uintptr_t(address);
    if(value>=uintptr_t(thread.data())&&value-uintptr_t(thread.data())<thread.size()){*out=thread.size()-(value-uintptr_t(thread.data()));return true;}
    if(value>=uintptr_t(m.dex.data_begin)&&value-uintptr_t(m.dex.data_begin)<m.dex.data_size){*out=m.dex.data_size-(value-uintptr_t(m.dex.data_begin));return true;}
    return HeapExtent(&heap,address,out);
   }};
   auto* moved_cache=heap.New(112);std::memcpy(moved_cache,cache,112);
   assert(pool.Write(batch.record.cache_root,moved_cache));auto pending=p;
   const auto state=Read32(current);Put32(current,0,92u<<24);Stop(nullptr,"preallocation-owned-stop",true);
   void* unavailable=nullptr;assert(!pool.Read(batch.record.cache_root,&unavailable)&&!mapper.RefreshAllocation(&pending));
   assert(mapper.EnterSemanticInspection(allocation_stop,bounds));
   const auto calls=jni_calls;forbid_allocation=true;
   assert(mapper.RefreshAllocation(&pending)&&pending.cache==moved_cache&&pending.old_class==old_class&&!pending.new_class);
   assert(mapper.OwnsUnpublished(pending,source));
   auto foreign=pending;foreign.source_old_root=batch.record.cache_root;assert(!mapper.RefreshAllocation(&foreign));
   auto wrong=source;wrong.private_token=&batch;assert(!mapper.OwnsUnpublished(pending,wrong));
   wrong=source;wrong.dex_identity=&batch;assert(!mapper.OwnsUnpublished(pending,wrong));
   allocation_stop.enabled=false;assert(!mapper.RefreshAllocation(&pending));allocation_stop.enabled=true;
   assert(jni_calls==calls);forbid_allocation=false;
   mapper.LeaveSemanticInspection();Resume(nullptr);Put32(current,0,state);
   assert(pool.Write(batch.record.cache_root,cache));
  }

  auto wrong_source=source;wrong_source.cache_root=batch.record.old_root;assert(!proof.VerifyPrivate(batch.record,wrong_source,context,pool));
  wrong_source=source;wrong_source.dex_identity=&batch;assert(!proof.VerifyPrivate(batch.record,wrong_source,context,pool));
  wrong_source=source;wrong_source.private_token=&batch;assert(!proof.VerifyPrivate(batch.record,wrong_source,context,pool));
  wrong_source=source;++wrong_source.counts.methods;assert(!proof.VerifyPrivate(batch.record,wrong_source,context,pool));
  auto wrong_context=context;wrong_context.vm=&batch;assert(!proof.VerifyPrivate(batch.record,source,wrong_context,pool));
  wrong_context=context;wrong_context.self=&batch;assert(!proof.VerifyPrivate(batch.record,source,wrong_context,pool));
  wrong_context=context;wrong_context.env=nullptr;assert(!proof.VerifyPrivate(batch.record,source,wrong_context,pool));
  source_handle_roots::Pool other_pool(*d.service);assert(!proof.VerifyPrivate(batch.record,source,context,other_pool));
assert(pool.Write(batch.record.new_root,new_class));p.new_class=new_class;assert(mapper.Describe(p,&source,&error));assert(source.dex_identity==file&&source.cache_root==batch.record.cache_root);assert(mapper.OwnsUnpublished(p,source));proof.private_cache=false;assert(!mapper.OwnsUnpublished(p,source));proof.private_cache=true;
  uint32_t type_index=UINT32_MAX;for(uint32_t i=0;i<source.counts.types;++i){const char*descriptor=nullptr;assert(evolution::ReadMappedDexTypeDescriptor(d.mapping,i,&descriptor,&error));if(!std::strcmp(descriptor,d.schema->descriptor.c_str()))type_index=i;}
  assert(type_index!=UINT32_MAX);void*root=nullptr;assert(mapper.FindSource(p,type_index,&root,&error));assert(root==batch.record.new_root&&mapper.Get(root)==new_class&&batch.finds>=3);
  assert(!mapper.FindSource(p,source.counts.types,&root,&error));
  auto*card=cards+((uintptr_t(cache)-uintptr_t(heap.base))>>10);*card=0;assert(mapper.Barrier(source.cache_root)&&*card==0x70);
  // The named SuspendAll boundary and non-Runnable state force the real
  // production Pool stopped reader and barrier path; JNI is forbidden here.
  struct Reader final:source_handle_roots::StoppedReader{
   bool FinalRootWindow()const noexcept override{return stopped==1&&Read32(current)!=0;}
   bool ReadStoppedRoot(const void*,void**)const noexcept override{return false;}
  } reader;
  struct Maps{const unsigned char* dex;size_t size;bool deny_card=false;size_t calls=0;} maps{d.mapping.begin,d.mapping.size};
  auto extent=[](void* raw,const void* address,size_t* out){auto&m=*static_cast<Maps*>(raw);++m.calls;
   if(m.deny_card&&address>=cards&&address<cards+2048)return false;
   if(address>=thread.data()&&address<thread.data()+thread.size()){*out=thread.size()-size_t(static_cast<const unsigned char*>(address)-thread.data());return true;}
   const auto value=uintptr_t(address),start=uintptr_t(m.dex);
   if(value>=start&&value-start<m.size){*out=m.size-size_t(value-start);return true;}
   return HeapExtent(&heap,address,out);
  };
  modern_field_metadata::ReadContext stopped_maps{&maps,extent};
  assert(!mapper.EnterSemanticInspection(reader,stopped_maps));
  const auto state=Read32(current);Put32(current,0,92u<<24);Stop(nullptr,"fixture-semantic",true);
  assert(!pool.Read(source.cache_root,&root));
  assert(!mapper.EnterSemanticInspection(reader,{}));
  assert(mapper.EnterSemanticInspection(reader,stopped_maps));
  assert(!mapper.EnterSemanticInspection(reader,stopped_maps));
  const int before_jni=jni_calls;*card=0;forbid_allocation=true;
  assert(mapper.Get(source.cache_root)==cache);assert(mapper.RefreshPrepared(&p));assert(mapper.OwnsUnpublished(p,source));
  assert(mapper.Barrier(source.cache_root)&&*card==0x70&&maps.calls>0);
  maps.deny_card=true;*card=0;assert(!mapper.Barrier(source.cache_root)&&*card==0);maps.deny_card=false;
  assert(!d.fields->SourceBarrierStopped(context,pool,reader,source.cache_root,nullptr));
  assert(!mapper.Barrier(&batch)&&jni_calls==before_jni);
  assert(mapper.InspectSemantic(&mapper,[](void* raw,const native_hierarchy_cache::SourceMapper& m)noexcept{return raw==&m;}));
  forbid_allocation=false;mapper.LeaveSemanticInspection();Resume(nullptr);Put32(current,0,state);
  assert(mapper.Get(source.cache_root)==cache&&mapper.CacheReadContext()==nullptr);
  batch.wrong_original=true;assert(!mapper.FindSource(p,type_index,&root,&error));batch.wrong_original=false;
  assert(mapper.VerifyFrozen(graph,p,source));native_hierarchy_cache::Source foreign=source;foreign.private_token=&batch;assert(!mapper.OwnsUnpublished(p,foreign));
  new_class->dex_class_def_index=1;assert(!mapper.OwnsUnpublished(p,source));new_class->dex_class_def_index=0;
  auto saved=file->begin;file->begin=d.mapping.begin+1;assert(!mapper.OwnsUnpublished(p,source));file->begin=saved;
  auto*moved=static_cast<class_metadata::Class17*>(heap.New(160));std::memcpy(moved,new_class,160);assert(pool.Write(batch.record.new_root,moved));assert(mapper.RefreshPrepared(&p)&&p.new_class==moved);assert(mapper.VerifyFrozen(graph,p,source));
  source_commit::Plan plan;plan.old_classes={old_class};plan.new_classes={moved};
  // Whole typed engine -> real SourceMapper -> actual source Pool/Class/DEX
  // reader/barrier integration. Public native ID boundary values are fixtures.
  auto populate=[&](void*raw_cache){auto*c=static_cast<type_cache::Dual*>(raw_cache);
   auto*t=static_cast<type_cache::Pair*>(heap.New(1024*8));t[0].index=1;t[type_index]={uint32_t(uintptr_t(old_class)),type_index};c->types=uintptr_t(t);
   auto*full=static_cast<uint32_t*>(heap.New(source.counts.types*4));full[type_index]=uint32_t(uintptr_t(old_class));c->types_array=uintptr_t(full);
   auto*f=static_cast<method_cache::Pair*>(heap.New(1024*16));f[0]={uintptr_t(old_fields[0]),0};c->fields=uintptr_t(f);
   auto*mf=static_cast<method_cache::Pair*>(heap.New(1024*16));mf[0]={uintptr_t(old_method),0};c->methods=uintptr_t(mf);
   auto*fa=static_cast<std::atomic<uintptr_t>*>(heap.New(source.counts.fields*8));fa[0]=uintptr_t(old_fields[0]);c->fields_array=uintptr_t(fa);
   auto*ma=static_cast<std::atomic<uintptr_t>*>(heap.New(source.counts.methods*8));ma[0]=uintptr_t(old_method);c->methods_array=uintptr_t(ma);
  };
  populate(published);populate(cache);void*calibration_root=pool.Add(published,&error);
  // Independent public/native fixture boundary roles; the production owner
  // still validates every occupied entry through the real current Pool reader.
  struct Roles final:native_hierarchy_cache::CurrentRoles {
   void* cache_root;void* cache_value;void* class_root;native_hierarchy_cache::Counts counts;
   uint32_t type_index;void* field;void* method;
   Roles(void*c,void*v,void*k,native_hierarchy_cache::Counts n,uint32_t t,void*f,void*m)
    :cache_root(c),cache_value(v),class_root(k),counts(n),type_index(t),field(f),method(m){}
   bool Cache(const native_hierarchy_cache::SourceMapper&m,void*r,native_hierarchy_cache::Counts n)const noexcept override{
    return m.CacheReadContext()&&r==cache_root&&m.Get(r)==cache_value&&m.Get(class_root)&&n.types==counts.types&&n.fields==counts.fields&&n.methods==counts.methods;
   }
   bool Type(const native_hierarchy_cache::SourceMapper&m,uint32_t i,void*p)const noexcept override{return i==type_index&&p==m.Get(class_root);}
   bool Field(const native_hierarchy_cache::SourceMapper&,uint32_t i,void*p)const noexcept override{return i==0&&p==field;}
   bool Method(const native_hierarchy_cache::SourceMapper&,uint32_t i,void*p)const noexcept override{return i==0&&p==method;}
  } roles(calibration_root,published,batch.record.old_root,source.counts,type_index,old_fields[0],old_method);
  struct Inspection {Mapper& mapper;Reader& reader;modern_field_metadata::ReadContext& maps;bool deny=false;} inspection{mapper,reader,stopped_maps};
  assert(mapper.BindSemanticInspection(&inspection,[](void*raw,void*payload,native_hierarchy_cache::SemanticOperation op)noexcept{
   auto&s=*static_cast<Inspection*>(raw);if(s.deny)return false;
   const auto saved_state=Read32(current);Put32(current,0,92u<<24);Stop(nullptr,"fixture-cache-roles",true);
   const bool entered=s.mapper.EnterSemanticInspection(s.reader,s.maps);
   const bool accepted=entered&&op(payload,s.mapper);
   if(entered)s.mapper.LeaveSemanticInspection();Resume(nullptr);Put32(current,0,saved_state);return accepted;
  }));
  native_hierarchy_cache::Owner incoming;native_hierarchy_cache::Witness witness{type_index,0,0,batch.record.old_root,old_fields[0],old_method};
  witness.current_roles=&roles;
  assert(incoming.Calibrate(mapper,calibration_root,112,source.counts,witness,&error));Composite composite(mapper,incoming);
  inspection.deny=true;assert(!composite.VerifyStaged(graph,{},&error));assert(error.find("inspection refused before callback")!=std::string::npos);inspection.deny=false;
  assert(!composite.VerifyStaged(graph,{},&error));assert(composite.Prepare(graph,p,&error));assert(composite.VerifyPrepared(graph,p,&error));assert(composite.Verify(graph,plan));
  assert(reinterpret_cast<type_cache::Pair*>(static_cast<type_cache::Dual*>(static_cast<void*>(cache))->types)[type_index].object==uint32_t(uintptr_t(moved)));
  auto*published_types=reinterpret_cast<type_cache::Pair*>(static_cast<type_cache::Dual*>(published)->types);assert(published_types[type_index].object==uint32_t(uintptr_t(old_class)));
  forbid_allocation=true;assert(composite.Verify(graph,plan));forbid_allocation=false;

  old_class->dex_cache=uint32_t(uintptr_t(cache));assert(mapper.IsReadonly(p));assert(!mapper.VerifyStaged(graph,{},&error));
  Mapper readonly_mapper(context,pool,*d.metadata,*d.fields,batch,&proof);assert(!readonly_mapper.VerifyReadonlyPrepared(graph,p));batch.count=0;assert(readonly_mapper.VerifyStaged(graph,{},&error));batch.count=1;assert(pool.Write(batch.record.new_root,nullptr));auto pending=p;pending.new_class=nullptr;assert(readonly_mapper.VerifyAllocation(graph,pending,&error));assert(pool.Write(batch.record.new_root,moved));assert(readonly_mapper.VerifyReadonlyPrepared(graph,p));assert(readonly_mapper.VerifyReadonlyCommit(graph,plan));assert(!mapper.OwnsUnpublished(p,source));old_class->dex_cache=uint32_t(uintptr_t(published));assert(!readonly_mapper.VerifyReadonlyCommit(graph,plan));
  forbid_allocation=true;assert(mapper.VerifyCommit(graph,plan));proof.live=false;assert(!mapper.VerifyCommit(graph,plan)&&!mapper.VerifyFrozen(graph,p,source));forbid_allocation=false;
  return true;
 };
 assert(runnable_executor::With(&env,reinterpret_cast<jobject>(old_class),run,&use)==runnable_jni::Result::kSuccess);Publish(nullptr);
 std::cout<<"PASS actual source cache Class/DEX provenance, original resolution then rooted replacement, native barrier, GC roots and missing final proof refusal\n";
}
