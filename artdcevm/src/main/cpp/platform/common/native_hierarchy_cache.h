#pragma once
#include "platform/common/native_hierarchy_graph.h"
#include "platform/common/native_cache_view.h"
#include "platform/common/type_cache_update.h"
namespace artdcevm::native_hierarchy_cache {
using Root=void*;
enum class PairFormat { Ordinary, Sequence };
struct Counts { uint32_t types=0,fields=0,methods=0; };
struct Witness {
 uint32_t type_index=0,field_index=0,method_index=0;
 Root expected_class=nullptr;
 void* native_field=nullptr; void* native_method=nullptr;
 bool has_public_storage=false;uintptr_t public_types=0,public_fields=0,public_methods=0;
};
// Identity and counts come from the allocator's immutable incoming StandardDEX,
// not from the cache layout being calibrated. token identifies that source
// owner's private, unpublished DexCache allocation; published caches cannot be
// granted by this interface. root remains owned until source cleanup finishes.
struct Source { Root cache_root=nullptr;const void* dex_identity=nullptr;
 const void* private_token=nullptr;size_t cache_extent=0;Counts counts; };
class SourceMapper {
 public:
 virtual ~SourceMapper()=default;
 virtual void* Get(Root)const noexcept=0;
 virtual bool Readable(const void*,size_t)const noexcept=0;
 virtual bool VerifyStaged(const native_hierarchy_graph::Graph&,
                          const std::vector<native_hierarchy_graph::Input>&,std::string*)const=0;
 virtual bool Describe(const native_hierarchy_graph::Prepared&,Source*,std::string*)const=0;
 virtual bool DescribeAllocation(const native_hierarchy_graph::Prepared&,Source*,std::string*)const{return false;}
 virtual bool RefreshAllocation(native_hierarchy_graph::Prepared*)const noexcept{return false;}
 // Resolve in actual staticOriginalFindSource owner context, parent-first,
 // returning a GC-updated root of the source verifier's actual mapped Class.
 virtual bool FindSource(const native_hierarchy_graph::Prepared&,uint32_t,Root*,std::string*)const=0;
 // Called only in the source owner's no-suspend unpublished writer window.
 // Must prove current mutator ownership + private token and actual allocations.
 virtual bool OwnsUnpublished(const native_hierarchy_graph::Prepared&,const Source&)const noexcept=0;
 virtual bool Barrier(Root cache)const noexcept=0;
 // Reload the exact owner-retained old/new/cache/parent roots after GC.
 virtual bool RefreshPrepared(native_hierarchy_graph::Prepared*)const noexcept=0;
 // Actual retained source Classes/DEX owner, cache roots, final exclusive owner,
 // reflective/resolve invalidation, deopt and interpreter-cache services proof.
 // No JNI, allocation, suspension or DEX parsing in either final callback.
 virtual bool VerifyFrozen(const native_hierarchy_graph::Graph&,
                           const native_hierarchy_graph::Prepared&,const Source&)const noexcept=0;
 virtual bool VerifyCommit(const native_hierarchy_graph::Graph&,
                           const source_commit::Plan&)const noexcept=0;
 virtual void BeginCommit(const native_hierarchy_graph::Graph&,
                          const source_commit::Plan&)const noexcept=0;
};
class Owner final:public native_hierarchy_graph::CacheOwner {
 public:
 // Public witnesses must come from independent named JNI/reflection services.
 // Requires a non-null resolved entry in each pair table to prove pair format,
 // including when a full representation wins ordinary getter precedence.
 bool Calibrate(const SourceMapper&,Root cache,size_t extent,Counts,const Witness&,std::string*);
 bool Prepare(const native_hierarchy_graph::Prepared&,std::string*);
 bool Refresh() noexcept;
 bool VerifyStaged(const native_hierarchy_graph::Graph&,const std::vector<native_hierarchy_graph::Input>&,std::string*)const override;
 bool VerifyPrepared(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&,std::string*)const override;
 bool VerifyAllocation(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&,std::string*)const override;
 bool PrepareLinked(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&p,std::string*e)override{return Prepare(p,e);}
 bool Verify(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override;
 void BeginCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override;
 private:
 struct TypeSlot {uint32_t position,index;Root expected;bool full;};
 struct Record {native_hierarchy_graph::Prepared prepared;Source source;
 type_cache::View types;native_cache::View fields,methods;std::vector<TypeSlot> slots;bool transformed=false;};
 const SourceMapper* mapper_=nullptr;dex_cache_metadata::Model model_=dex_cache_metadata::Model::Pairs;
 PairFormat format_=PairFormat::Ordinary;std::vector<Record> records_;
 Source calibration_;Witness witness_;
 bool Witnesses()const noexcept;
 bool Views(const Source&,type_cache::View*,native_cache::View*,native_cache::View*)const noexcept;
 bool Check(const Record&)const noexcept;
};
}
