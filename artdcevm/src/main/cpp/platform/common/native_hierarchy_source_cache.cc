#include "platform/common/native_hierarchy_source_cache.h"
#include "platform/common/native_hierarchy_allocator.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/common/method_identity_calibration.h"
#include <cstring>
#include <new>
namespace artdcevm::native_hierarchy_source_cache {
namespace {
bool Fail(std::string*e,const char*s){if(e)*e=s;return false;}
uint32_t U32(const void*p,size_t off){uint32_t v;std::memcpy(&v,static_cast<const unsigned char*>(p)+off,4);return v;}
bool SameMembers(const std::vector<evolution::Member>&a,const std::vector<evolution::Member>&b){if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i)if(a[i].name!=b[i].name||a[i].descriptor!=b[i].descriptor||a[i].access!=b[i].access||a[i].dex_index!=b[i].dex_index)return false;return true;}
bool SameRecord(const RecordView&a,const RecordView&b){return a.source_owner==b.source_owner&&a.class_index==b.class_index&&a.old_root==b.old_root&&a.new_root==b.new_root&&a.cache_root==b.cache_root&&a.parent_root==b.parent_root&&a.declarations==b.declarations&&a.dex.begin==b.dex.begin&&a.dex.size==b.dex.size&&a.dex.data_begin==b.dex.data_begin&&a.dex.data_size==b.dex.data_size&&a.dex.class_def_index==b.dex.class_def_index;}
 bool Mapped(const void*p,size_t n){size_t extent=0;return p&&method_identity::ReadableMappingExtent(p,&extent)&&n<=extent;}
}
bool Inspect(const evolution::MappedDexMetadata&d,const evolution::ClassSchema&expected,native_hierarchy_cache::Counts*out,std::string*e){
 try {
 std::string local_error;if(!e)e=&local_error;
 if(!out||!Mapped(d.begin,d.size)||!Mapped(d.data_begin,d.data_size)||d.size<112||std::memcmp(d.begin,"dex\n",4)||d.begin[7]||U32(d.begin,32)!=d.size)return Fail(e,"incoming StandardDEX mapping is not complete");
 evolution::ClassSchema actual;
 if(!evolution::ReadMappedDexDeclarations(d,&actual,e))return false;
 if(actual.descriptor!=expected.descriptor||actual.superclass!=expected.superclass||actual.interfaces!=expected.interfaces||actual.access!=expected.access||!SameMembers(actual.fields,expected.fields)||!SameMembers(actual.methods,expected.methods))return Fail(e,"incoming source Class declarations differ from immutable staged declarations");
 *out={U32(d.begin,64),U32(d.begin,80),U32(d.begin,88)};return true;
 }catch(const std::bad_alloc&){return Fail(e,"incoming source declarations native storage exhausted");}
}
Mapper::Mapper(const runnable_jni::Context&ctx,source_handle_roots::Pool&p,const modern_field_metadata::FieldMetadata&f,const modern_field_storage::Service&s,const BatchAccess&b,const RequiredServices*r):context_(ctx),pool_(p),fields_(f),storage_(s),batch_(b),services_(r){}
bool AllocatorBatchAccess::Attach(native_hierarchy_allocator::Batch&batch)noexcept{if(batch_||&batch.Roots()!=&pool_||!pool_.ReadyForSourceAccess())return false;batch_=&batch;return true;}
size_t AllocatorBatchAccess::Count()const noexcept{return batch_?batch_->Count():0;}
bool AllocatorBatchAccess::At(size_t n,RecordView*out)const noexcept{if(!batch_||!out)return false;native_hierarchy_allocator::Batch::RecordView r;if(!batch_->At(n,&r))return false;*out={r.source_owner,r.class_index,r.old_root,r.new_root,r.cache_root,r.parent_root,r.declarations,r.dex};return true;}
void* AllocatorBatchAccess::Find(const char*name,void*loader)const noexcept{return batch_?batch_->Find(name,loader):nullptr;}
void* AllocatorBatchAccess::OriginalFind(const char*name,void*loader)const{return batch_?batch_->OriginalFind(name,loader):nullptr;}
void* Mapper::Get(native_hierarchy_cache::Root root)const noexcept{void*value=nullptr;return pool_.Read(root,&value)?value:nullptr;}
bool Mapper::Readable(const void*p,size_t n)const noexcept{return Mapped(p,n);}
bool Mapper::Record(const native_hierarchy_graph::Prepared&p,RecordView*out)const noexcept{
 if(!out||!pool_.ReadyForSourceAccess()||!fields_.Calibrated()||!p.source_owner)return false;
 for(size_t i=0;i<batch_.Count();++i){RecordView r;if(!batch_.At(i,&r))return false;
 if(r.source_owner==p.source_owner&&r.class_index==p.class_index&&r.declarations==p.declarations&&r.dex.class_def_index==r.class_index&&Get(r.old_root)==p.old_class&&Get(r.new_root)==p.new_class&&Get(r.cache_root)==p.cache&&Get(r.parent_root)==p.parent){*out=r;return true;}}
 return false;
}
bool Mapper::Namespace(const RecordView&r,bool linked,bool readonly)const noexcept{
 if(!r.declarations||!r.source_owner||!r.old_root||!r.cache_root)return false;
 void*old=Get(r.old_root),*cache=Get(r.cache_root);field_copy::CopyClassView before;
 if(!old||!cache||!fields_.ClassIdentity(old,&before,nullptr)||std::strcmp(before.descriptor,r.declarations->descriptor.c_str()))return false;
 // Incoming cache must belong to a distinct source DEX, rather than the
 // already published original cache. The batch provenance owns this allocation.
 if((fields_.Reference(old,offsetof(class_metadata::Class11,dex_cache))==cache)!=readonly)return false;
 if(!linked)return true;
 void*replacement=Get(r.new_root);field_copy::CopyClassView after;
 const auto&layout=fields_.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 if(!replacement||replacement==old||!Mapped(replacement,layout.size)||!fields_.ClassIdentity(replacement,&after,nullptr)||std::strcmp(before.descriptor,after.descriptor)||before.loader!=after.loader||after.parent!=Get(r.parent_root)||fields_.Reference(replacement,offsetof(class_metadata::Class11,dex_cache))!=cache||U32(replacement,layout.dex_class_def_index)!=r.class_index)return false;
 if(after.parent){field_copy::CopyClassView parent;if(!fields_.ClassIdentity(after.parent,&parent,nullptr)||r.declarations->superclass!=parent.descriptor)return false;}
 else if(!r.declarations->superclass.empty())return false;
 return true;
}
bool Mapper::Mapping(const RecordView&r,const void**identity)const noexcept{
 void*cache=Get(r.cache_root);if(!identity||!Mapped(cache,24))return false;
 uint64_t native;std::memcpy(&native,static_cast<const unsigned char*>(cache)+16,8);auto*file=reinterpret_cast<const void*>(static_cast<uintptr_t>(native));
 if(!Mapped(file,sizeof(dex_mapping_abi::Prefix)))return false;
 dex_mapping_abi::Prefix prefix;std::memcpy(&prefix,file,sizeof(prefix));
 if(prefix.begin!=r.dex.begin||prefix.data_begin!=r.dex.data_begin||prefix.data_size!=r.dex.data_size||!Mapped(r.dex.begin,r.dex.size)||!Mapped(r.dex.data_begin,r.dex.data_size)||r.dex.size<112)return false;
 // The size/unused_size source slot is deliberately never used as an extent.
 const uintptr_t begin=uintptr_t(r.dex.begin),data=uintptr_t(r.dex.data_begin);
 if(begin<data||begin-data>r.dex.data_size||r.dex.size>r.dex.data_size-(begin-data))return false;
 *identity=file;return true;
}
const Mapper::Entry* Mapper::EntryFor(void*owner)const noexcept{for(const auto&e:entries_)if(e->record.source_owner==owner)return e.get();return nullptr;}
bool Mapper::VerifyStaged(const native_hierarchy_graph::Graph&g,const std::vector<native_hierarchy_graph::Input>&inputs,std::string*e)const{
 // This is the independent expected graph phase, before any allocation record.
 // The real source namespace/cache proof belongs to VerifyAllocation below.
 if(!services_||!pool_.ReadyForSourceAccess()||!fields_.Calibrated()||batch_.Count()||!entries_.empty()||!readonly_.empty()||!services_->VerifyStaged(g,inputs))return Fail(e,"expected source cache graph must precede actual allocation records");
 if(staged_graph_&&staged_graph_!=&g)return Fail(e,"expected cache graph owner changed");
 staged_graph_=&g;return true;
}
bool Mapper::VerifyAllocation(const native_hierarchy_graph::Graph&g,const native_hierarchy_graph::Prepared&p,std::string*e)const{
 RecordView r;const void*identity=nullptr;native_hierarchy_cache::Counts counts;
 if(!services_||staged_graph_!=&g||p.new_class||!p.source_old_root||!Record(p,&r)||r.old_root!=p.source_old_root||Get(r.new_root)||!Mapping(r,&identity)||!Inspect(r.dex,*r.declarations,&counts,e))return Fail(e,"actual unallocated cache source does not belong to expected graph");
 size_t matches=0;for(const auto&node:g.nodes)if(node.affected&&node.klass==Get(r.old_root)&&node.descriptor==r.declarations->descriptor)++matches;
 if(matches!=1)return Fail(e,"unallocated source cache is outside exact affected graph");
 const bool readonly=fields_.Reference(Get(r.old_root),offsetof(class_metadata::Class11,dex_cache))==Get(r.cache_root);
 if(!Namespace(r,false,readonly))return Fail(e,"actual unallocated source cache namespace differs");
 if(!readonly)return true; // The typed private engine additionally proves ownership/views.
 for(const auto&saved:readonly_)if(SameRecord(saved,r))return Fail(e,"readonly preallocation source was already consumed");
 if(!services_->VerifyReadonlyStaged(g,r))return Fail(e,"published source cache has no independent preallocation proof");
 try{readonly_.push_back(r);return true;}catch(const std::bad_alloc&){return Fail(e,"readonly preallocation witness storage exhausted");}
}
bool Mapper::DescribeAllocation(const native_hierarchy_graph::Prepared&p,native_hierarchy_cache::Source*out,std::string*e)const{
 RecordView r;if(!out||!services_||!staged_graph_||p.new_class||!Record(p,&r)||p.source_old_root!=r.old_root||Get(r.new_root)||!Namespace(r,false))return Fail(e,"unallocated source cache has no exact actual allocator record");
 if(EntryFor(p.source_owner))return Fail(e,"private preallocation source was already consumed");
 try{
 auto entry=std::make_unique<Entry>();entry->record=r;const void*identity=nullptr;size_t extent=0;
 if(!Mapping(r,&identity)||!Inspect(r.dex,*r.declarations,&entry->source.counts,e)||!method_identity::ReadableMappingExtent(p.cache,&extent))return false;
 field_copy::CopyClassView old;if(!fields_.ClassIdentity(Get(r.old_root),&old,e))return false;
 entry->loader_root=pool_.Add(old.loader,e);if(!entry->loader_root)return false;
 void*original=batch_.OriginalFind(r.declarations->descriptor.c_str(),entry->loader_root);
 if(!original||original!=Get(r.old_root)||!Namespace(r,false)||Get(r.new_root))return Fail(e,"preallocation original ClassTable namespace changed");
 entry->source.cache_root=r.cache_root;entry->source.dex_identity=identity;entry->source.private_token=entry.get();entry->source.cache_extent=extent;
 entries_.push_back(std::move(entry));*out=entries_.back()->source;return true;
 }catch(const std::bad_alloc&){return Fail(e,"preallocation cache source witness storage exhausted");}
}
bool Mapper::Describe(const native_hierarchy_graph::Prepared&p,native_hierarchy_cache::Source*out,std::string*e)const{
 RecordView r;if(!out||!services_||!p.new_class||!Record(p,&r)||!Namespace(r,true))return Fail(e,"linked source cache has no exact actual allocator record");
 const auto*entry=EntryFor(p.source_owner);
 if(!entry||!SameRecord(r,entry->record))return Fail(e,"linked cache lacks its immutable preallocation source witness");
 *out=entry->source;return true;
}
bool Mapper::RefreshAllocation(native_hierarchy_graph::Prepared*p)const noexcept{
 if(!p||p->new_class||!p->source_old_root||!services_||!pool_.ReadyForSourceAccess())return false;
 const auto*entry=EntryFor(p->source_owner);if(!entry||entry->record.old_root!=p->source_old_root)return false;
 for(size_t i=0;i<batch_.Count();++i){RecordView r;if(!batch_.At(i,&r))return false;
  if(SameRecord(r,entry->record)&&!Get(r.new_root)){p->old_class=Get(r.old_root);p->cache=Get(r.cache_root);p->parent=Get(r.parent_root);return p->old_class&&p->cache;}}
 return false;
}
bool Mapper::FindSource(const native_hierarchy_graph::Prepared&p,uint32_t index,native_hierarchy_cache::Root*out,std::string*e)const{
 std::string local_error;if(!e)e=&local_error;
 const auto*entry=EntryFor(p.source_owner);RecordView r;
 if(!out||!entry||!Record(p,&r)||index>=entry->source.counts.types)return Fail(e,"source verifier type index is outside immutable incoming DEX");
 const char*descriptor=nullptr;if(!evolution::ReadMappedDexTypeDescriptor(r.dex,index,&descriptor,e))return false;
 void*source=batch_.OriginalFind(descriptor,entry->loader_root);if(!source)return Fail(e,"original source Class resolution failed");
 // Preserve ART's original resolution side effects/errors, then immediately
 // root its actual Class before further native work can allocate or suspend.
 void*original_root=nullptr;try{original_root=pool_.Add(source,e);}catch(const std::bad_alloc&){return Fail(e,"source mapped Class root storage exhausted");}if(!original_root)return false;
 void* resolved_loader=fields_.Reference(Get(original_root),offsetof(class_metadata::Class11,class_loader));
 if(descriptor[0]=='L'){field_copy::CopyClassView original_identity;if(!fields_.ClassIdentity(Get(original_root),&original_identity,e)||std::strcmp(descriptor,original_identity.descriptor))return Fail(e,"original source lookup returned a different native Class identity");}
 void*mapped=batch_.Find(descriptor,resolved_loader);
 if(mapped){if(!Get(mapped))return Fail(e,"source replacement mapping lacks a retained Class root");
 if(descriptor[0]=='L'){field_copy::CopyClassView mapped_identity;if(!fields_.ClassIdentity(Get(mapped),&mapped_identity,e)||std::strcmp(descriptor,mapped_identity.descriptor)||mapped_identity.loader!=resolved_loader)return Fail(e,"mapped source Class namespace differs from original resolution");}
 *out=mapped;return true;}
 // An array whose component is replaced needs an actual source-created array
 // mapping. Returning an old array Class would silently retain old ancestry.
 if(descriptor[0]=='['){const char*component=descriptor;while(*component=='[')++component;if(*component=='L'&&batch_.Find(component,resolved_loader))return Fail(e,"affected array Class has no actual source replacement mapping");}
 *out=original_root;return true;
}
bool Mapper::OwnsUnpublished(const native_hierarchy_graph::Prepared&p,const native_hierarchy_cache::Source&s)const noexcept{
 const auto*entry=EntryFor(p.source_owner);RecordView r;const void*identity=nullptr;
 if(!entry||entry!=s.private_token||!Record(p,&r)||!SameRecord(r,entry->record)||r.cache_root!=s.cache_root||entry->source.dex_identity!=s.dex_identity||!Namespace(r,p.new_class!=nullptr)||!Mapping(r,&identity)||identity!=s.dex_identity)return false;
 // No fallible resolver in the borrowed writer window. Describe proved exact
 // original lookup; RequireServices must prove its source/ClassTable lock and
 // retained publication/cleanup lifetime in the final owner as well.
 return services_&&Get(entry->loader_root)==fields_.Reference(Get(r.old_root),offsetof(class_metadata::Class11,class_loader))&&services_->VerifyPrivate(r,s,context_,pool_);
}
bool Mapper::Barrier(native_hierarchy_cache::Root root)const noexcept{return services_&&storage_.SourceBarrier(context_,pool_,root);}
bool Mapper::RefreshPrepared(native_hierarchy_graph::Prepared*p)const noexcept{
 if(!p)return false;const auto*entry=EntryFor(p->source_owner);
 if(!entry||entry->record.class_index!=p->class_index||entry->record.declarations!=p->declarations)return false;
 for(size_t i=0;i<batch_.Count();++i){RecordView r;if(!batch_.At(i,&r))return false;if(SameRecord(r,entry->record)){p->old_class=Get(r.old_root);p->new_class=Get(r.new_root);p->cache=Get(r.cache_root);p->parent=Get(r.parent_root);return p->old_class&&p->new_class&&p->cache;}}
 return false;
}
bool Mapper::VerifyFrozen(const native_hierarchy_graph::Graph&g,const native_hierarchy_graph::Prepared&p,const native_hierarchy_cache::Source&s)const noexcept{return services_&&OwnsUnpublished(p,s)&&services_->VerifyFrozen(g,p,s);}
bool Mapper::VerifyCommit(const native_hierarchy_graph::Graph&g,const source_commit::Plan&p)const noexcept{return services_&&pool_.ReadyForSourceAccess()&&services_->VerifyCommit(g,p);}
void Mapper::BeginCommit(const native_hierarchy_graph::Graph&g,const source_commit::Plan&p)const noexcept{if(VerifyCommit(g,p))services_->BeginCommit(g,p);}
bool Mapper::IsReadonly(const native_hierarchy_graph::Prepared&p)const noexcept{
 RecordView r;return Record(p,&r)&&Get(r.cache_root)&&fields_.Reference(Get(r.old_root),offsetof(class_metadata::Class11,dex_cache))==Get(r.cache_root);
}
bool Mapper::VerifyReadonlyPrepared(const native_hierarchy_graph::Graph&g,const native_hierarchy_graph::Prepared&p)const noexcept{
 RecordView r;const void*identity=nullptr;
 if(!services_||!Record(p,&r)||!Namespace(r,true,true)||!Mapping(r,&identity))return false;
 bool found=false;for(const auto&saved:readonly_)if(SameRecord(r,saved)){found=true;break;}
 return found&&services_->VerifyReadonlyPrepared(g,r,p);
}
bool Mapper::VerifyReadonlyCommit(const native_hierarchy_graph::Graph&g,const source_commit::Plan&p)const noexcept{
 if(!services_||!pool_.ReadyForSourceAccess()||p.old_classes.size()!=p.new_classes.size())return false;
 size_t readonly_seen=0;
 for(size_t i=0;i<batch_.Count();++i){RecordView r;if(!batch_.At(i,&r))return false;
 void*old=Get(r.old_root),*replacement=Get(r.new_root),*cache=Get(r.cache_root);
 if(!old||!replacement||!cache)return false;
 if(fields_.Reference(old,offsetof(class_metadata::Class11,dex_cache))!=cache)continue;
 ++readonly_seen;
 const void*identity=nullptr;if(!Namespace(r,true,true)||!Mapping(r,&identity))return false;
 bool found=false;for(const auto&saved:readonly_)if(SameRecord(r,saved)){found=true;break;}if(!found)return false;
 size_t match=SIZE_MAX;for(size_t pair=0;pair<p.old_classes.size();++pair)if(p.old_classes[pair]==old&&p.new_classes[pair]==replacement){if(match!=SIZE_MAX)return false;match=pair;}
 if(match==SIZE_MAX||!services_->VerifyReadonly(g,r,p,match))return false;
 }
 return readonly_seen==readonly_.size();
}
bool Composite::Prepare(const native_hierarchy_graph::Graph&g,const native_hierarchy_graph::Prepared&p,std::string*e){
 if(mapper_.IsReadonly(p))return mapper_.VerifyReadonlyPrepared(g,p)||Fail(e,"published implicit source cache lacks readonly linkage proof");
 return incoming_.Prepare(p,e);
}
bool Composite::VerifyStaged(const native_hierarchy_graph::Graph&g,const std::vector<native_hierarchy_graph::Input>&i,std::string*e)const{return incoming_.VerifyStaged(g,i,e);}
bool Composite::VerifyAllocation(const native_hierarchy_graph::Graph&g,const native_hierarchy_graph::Prepared&p,std::string*e)const{
 if(!mapper_.VerifyAllocation(g,p,e))return false;
 return mapper_.IsReadonly(p)||incoming_.VerifyAllocation(g,p,e);
}
bool Composite::VerifyPrepared(const native_hierarchy_graph::Graph&g,const native_hierarchy_graph::Prepared&p,std::string*e)const{
 if(mapper_.IsReadonly(p))return mapper_.VerifyReadonlyPrepared(g,p)||Fail(e,"published implicit source cache lacks readonly preparation proof");
 return incoming_.VerifyPrepared(g,p,e);
}
bool Composite::Verify(const native_hierarchy_graph::Graph&g,const source_commit::Plan&p)const noexcept{return incoming_.Verify(g,p)&&mapper_.VerifyReadonlyCommit(g,p);}
void Composite::BeginCommit(const native_hierarchy_graph::Graph&g,const source_commit::Plan&p)const noexcept{if(Verify(g,p))incoming_.BeginCommit(g,p);}

}
