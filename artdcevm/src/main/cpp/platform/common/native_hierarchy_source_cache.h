#pragma once
#include "platform/common/native_hierarchy_cache.h"
#include "platform/common/source_handle_roots.h"
#include "platform/common/modern_field_metadata.h"
#include "platform/common/modern_field_storage.h"
namespace artdcevm::native_hierarchy_allocator {class Batch;}
namespace artdcevm::native_hierarchy_source_cache {
struct RecordView {
 void* source_owner=nullptr;uint16_t class_index=0;
 void* old_root=nullptr;void* new_root=nullptr;void* cache_root=nullptr;void* parent_root=nullptr;
 const evolution::ClassSchema* declarations=nullptr;evolution::MappedDexMetadata dex{};
};
// Implemented by the actual allocator Batch. At returns immutable source-owner
// provenance and live roots captured by the real holder/allocator hooks.
class BatchAccess {
 public:
 virtual ~BatchAccess()=default;
 virtual size_t Count()const noexcept=0;
 virtual bool At(size_t,RecordView*)const noexcept=0;
 virtual void* Find(const char* descriptor,void* loader)const noexcept=0;
 // Calibrated named original ClassLinker::FindClass, observed actual linker,
 // actual ctx.self and GC-updated loader Handle. This may safepoint; callers
 // root its borrowed return immediately. Missing native capability refuses.
 virtual void* OriginalFind(const char* descriptor,void* loader_root)const=0;
};
// Begins empty during expected graph Stage, then binds exactly once to the
// actual first-Finish allocator Batch using the very same source root Pool.
class AllocatorBatchAccess final:public BatchAccess {
 public:
 explicit AllocatorBatchAccess(source_handle_roots::Pool&p):pool_(p){}
 bool Attach(native_hierarchy_allocator::Batch&)noexcept;
 bool Detach(native_hierarchy_allocator::Batch&)noexcept;
 size_t Count()const noexcept override;
 bool At(size_t,RecordView*)const noexcept override;
 void* Find(const char*,void*)const noexcept override;
 void* OriginalFind(const char*,void*)const override;
 private:
 source_handle_roots::Pool&pool_;native_hierarchy_allocator::Batch*batch_=nullptr;bool attached_once_=false;
};
class RequiredServices:public source_handle_roots::StoppedReader {
 public:
 virtual ~RequiredServices()=default;
 bool FinalRootWindow()const noexcept override{return false;}
 bool ReadStoppedRoot(const void*,void**)const noexcept override{return false;}
 virtual bool VerifyStaged(const native_hierarchy_graph::Graph&,const std::vector<native_hierarchy_graph::Input>&)const noexcept=0;
 // Actual allocation/publication hook ownership and no-suspend writer window,
 // independently proving the cache is private to this source transaction.
 // ClassTable lookup of the new Class cannot prove cache exclusivity.
 virtual bool PreparePrivate(const RecordView&,const native_hierarchy_cache::Source&,std::string*)const{return false;}
 virtual bool VerifyPrivate(const RecordView&,const native_hierarchy_cache::Source&,
                            const runnable_jni::Context&,const source_handle_roots::Pool&)const noexcept=0;
 // Separate proof stages for published implicit-subclass caches. These
 // prove actual source linkage and unchanged member mapping before commit,
 // then formal heap GC-root/native-member/public reflection cache cleanup.
 virtual bool VerifyReadonlyStaged(const native_hierarchy_graph::Graph&,const RecordView&)const noexcept=0;
 virtual bool VerifyReadonlyPrepared(const native_hierarchy_graph::Graph&,const RecordView&,const native_hierarchy_graph::Prepared&)const noexcept=0;
 virtual bool VerifyReadonly(const native_hierarchy_graph::Graph&,const RecordView&,const source_commit::Plan&,size_t pair)const noexcept=0;
 virtual bool VerifyFrozen(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&,
                          const native_hierarchy_cache::Source&)const noexcept=0;
 virtual bool VerifyCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept=0;
 virtual void BeginCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept=0;
};
// Complete bounded immutable StandardDEX and source-class declaration reader.
// Caller establishes actual mapping/lifetime; data/container spans are checked
// before invoking the shared 035..041 parser. No API selector is involved.
bool Inspect(const evolution::MappedDexMetadata&,const evolution::ClassSchema&,
             native_hierarchy_cache::Counts*,std::string*);
class Mapper final:public native_hierarchy_cache::SourceMapper {
 public:
 Mapper(const runnable_jni::Context&,source_handle_roots::Pool&,
        const modern_field_metadata::FieldMetadata&,const modern_field_storage::Service&,
        const BatchAccess&,const RequiredServices*);
 void* Get(native_hierarchy_cache::Root)const noexcept override;
 bool Readable(const void*,size_t)const noexcept override;
 bool VerifyStaged(const native_hierarchy_graph::Graph&,const std::vector<native_hierarchy_graph::Input>&,std::string*)const override;
 bool Describe(const native_hierarchy_graph::Prepared&,native_hierarchy_cache::Source*,std::string*)const override;
 bool DescribeAllocation(const native_hierarchy_graph::Prepared&,native_hierarchy_cache::Source*,std::string*)const override;
 bool RefreshAllocation(native_hierarchy_graph::Prepared*)const noexcept override;
 bool VerifyAllocation(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&,std::string*)const;
 bool FindSource(const native_hierarchy_graph::Prepared&,uint32_t,native_hierarchy_cache::Root*,std::string*)const override;
 bool OwnsUnpublished(const native_hierarchy_graph::Prepared&,const native_hierarchy_cache::Source&)const noexcept override;
 bool Barrier(native_hierarchy_cache::Root)const noexcept override;
 bool RefreshPrepared(native_hierarchy_graph::Prepared*)const noexcept override;
 bool VerifyFrozen(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&,const native_hierarchy_cache::Source&)const noexcept override;
 bool VerifyCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override;
 void BeginCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override;
 bool IsReadonly(const native_hierarchy_graph::Prepared&)const noexcept;
 bool VerifyReadonlyPrepared(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&)const noexcept;
 bool VerifyReadonlyCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept;
 private:
 struct Entry {RecordView record;native_hierarchy_cache::Source source;void* loader_root=nullptr;};
 const runnable_jni::Context context_;source_handle_roots::Pool& pool_;
 const modern_field_metadata::FieldMetadata& fields_;const modern_field_storage::Service& storage_;
 const BatchAccess& batch_;const RequiredServices* services_;mutable std::vector<std::unique_ptr<Entry>> entries_;mutable std::vector<RecordView> readonly_;mutable const native_hierarchy_graph::Graph* staged_graph_=nullptr;
 bool Record(const native_hierarchy_graph::Prepared&,RecordView*)const noexcept;
 bool Namespace(const RecordView&,bool linked,bool readonly=false)const noexcept;
 bool Mapping(const RecordView&,const void** dex_identity)const noexcept;
 const Entry* EntryFor(const native_hierarchy_graph::Prepared&)const noexcept;
};
// Full graph cache participant: private explicit incoming caches are transformed
// by the existing typed Owner. Published implicit caches are checked read-only,
// and require their independent source-generation and final cleanup owners.
class Composite final:public native_hierarchy_graph::CacheOwner {
 public:
 Composite(Mapper&m,native_hierarchy_cache::Owner&c):mapper_(m),incoming_(c){}
 bool Prepare(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&,std::string*);
 bool Refresh()noexcept{return incoming_.Refresh();}
 bool VerifyStaged(const native_hierarchy_graph::Graph&,const std::vector<native_hierarchy_graph::Input>&,std::string*)const override;
 bool VerifyPrepared(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&,std::string*)const override;
 bool VerifyAllocation(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&,std::string*)const override;
 bool PrepareLinked(const native_hierarchy_graph::Graph&g,const native_hierarchy_graph::Prepared&p,std::string*e)override{return Prepare(g,p,e);}
 bool Verify(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override;
 void BeginCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override;
 private:Mapper&mapper_;native_hierarchy_cache::Owner&incoming_;
};
}
