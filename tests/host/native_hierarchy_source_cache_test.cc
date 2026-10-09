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
class Proof final:public RequiredServices {public:bool live=true,private_cache=true;mutable int begins=0;
 bool VerifyStaged(const native_hierarchy_graph::Graph&,const std::vector<native_hierarchy_graph::Input>&)const noexcept override{return live;}
 bool VerifyPrivate(const RecordView&,const native_hierarchy_cache::Source&,const runnable_jni::Context&,const source_handle_roots::Pool&)const noexcept override{return live&&private_cache;}
 bool VerifyReadonlyStaged(const native_hierarchy_graph::Graph&,const RecordView&)const noexcept override{return live;}
 bool VerifyReadonlyPrepared(const native_hierarchy_graph::Graph&,const RecordView&,const native_hierarchy_graph::Prepared&)const noexcept override{return live;}
 bool VerifyReadonly(const native_hierarchy_graph::Graph&,const RecordView&,const source_commit::Plan&,size_t)const noexcept override{return live;}
 bool VerifyFrozen(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&,const native_hierarchy_cache::Source&)const noexcept override{return live;}
 bool VerifyCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override{return live;}
 void BeginCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override{++begins;}
};
}
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
  Proof proof;Mapper mapper(context,pool,*d.metadata,*d.fields,batch,&proof);Mapper absent(context,pool,*d.metadata,*d.fields,batch,nullptr);
  assert(!absent.VerifyStaged(graph,{},&error));assert(mapper.VerifyStaged(graph,{},&error));
  native_hierarchy_cache::Source source;batch.count=1;p.new_class=nullptr;assert(mapper.VerifyAllocation(graph,p,&error));batch.wrong_original=true;assert(!mapper.DescribeAllocation(p,&source,&error));batch.wrong_original=false;
  assert(mapper.DescribeAllocation(p,&source,&error));assert(!mapper.DescribeAllocation(p,&source,&error));assert(pool.Write(batch.record.new_root,new_class));p.new_class=new_class;assert(mapper.Describe(p,&source,&error));assert(source.dex_identity==file&&source.cache_root==batch.record.cache_root);assert(mapper.OwnsUnpublished(p,source));proof.private_cache=false;assert(!mapper.OwnsUnpublished(p,source));proof.private_cache=true;
  uint32_t type_index=UINT32_MAX;for(uint32_t i=0;i<source.counts.types;++i){const char*descriptor=nullptr;assert(evolution::ReadMappedDexTypeDescriptor(d.mapping,i,&descriptor,&error));if(!std::strcmp(descriptor,d.schema->descriptor.c_str()))type_index=i;}
  assert(type_index!=UINT32_MAX);void*root=nullptr;assert(mapper.FindSource(p,type_index,&root,&error));assert(root==batch.record.new_root&&mapper.Get(root)==new_class&&batch.finds>=3);
  assert(!mapper.FindSource(p,source.counts.types,&root,&error));
  auto*card=cards+((uintptr_t(cache)-uintptr_t(heap.base))>>10);*card=0;assert(mapper.Barrier(source.cache_root)&&*card==0x70);
  batch.wrong_original=true;assert(!mapper.FindSource(p,type_index,&root,&error));batch.wrong_original=false;
  assert(mapper.VerifyFrozen(graph,p,source));native_hierarchy_cache::Source foreign=source;foreign.private_token=&batch;assert(!mapper.OwnsUnpublished(p,foreign));
  new_class->dex_class_def_index=1;assert(!mapper.OwnsUnpublished(p,source));new_class->dex_class_def_index=0;
  auto saved=file->begin;file->begin=d.mapping.begin+1;assert(!mapper.OwnsUnpublished(p,source));file->begin=saved;
  auto*moved=static_cast<class_metadata::Class17*>(heap.New(160));std::memcpy(moved,new_class,160);assert(pool.Write(batch.record.new_root,moved));assert(mapper.RefreshPrepared(&p)&&p.new_class==moved);assert(mapper.VerifyFrozen(graph,p,source));
  source_commit::Plan plan;plan.old_classes={old_class};plan.new_classes={moved};
  // Whole typed engine -> real SourceMapper -> actual source Pool/Class/DEX
  // reader/barrier integration. Public native ID boundary values are fixtures.
  auto populate=[&](void*raw_cache){auto*c=static_cast<type_cache::Dual*>(raw_cache);
   auto*t=static_cast<type_cache::Pair*>(heap.New(1024*8));t[type_index]={uint32_t(uintptr_t(old_class)),type_index};c->types=uintptr_t(t);
   auto*full=static_cast<uint32_t*>(heap.New(source.counts.types*4));full[type_index]=uint32_t(uintptr_t(old_class));c->types_array=uintptr_t(full);
   auto*f=static_cast<method_cache::Pair*>(heap.New(1024*16));f[0]={uintptr_t(old_fields[0]),0};c->fields=uintptr_t(f);
   auto*mf=static_cast<method_cache::Pair*>(heap.New(1024*16));mf[0]={uintptr_t(old_method),0};c->methods=uintptr_t(mf);
   auto*fa=static_cast<std::atomic<uintptr_t>*>(heap.New(source.counts.fields*8));fa[0]=uintptr_t(old_fields[0]);c->fields_array=uintptr_t(fa);
   auto*ma=static_cast<std::atomic<uintptr_t>*>(heap.New(source.counts.methods*8));ma[0]=uintptr_t(old_method);c->methods_array=uintptr_t(ma);
  };
  populate(published);populate(cache);void*calibration_root=pool.Add(published,&error);
  native_hierarchy_cache::Owner incoming;native_hierarchy_cache::Witness witness{type_index,0,0,batch.record.old_root,old_fields[0],old_method};
  assert(incoming.Calibrate(mapper,calibration_root,112,source.counts,witness,&error));Composite composite(mapper,incoming);
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
