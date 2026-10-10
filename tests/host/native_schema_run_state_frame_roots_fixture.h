#pragma once
// Original ART root/lock leaf boundaries for the real RootProvenance service.
// No callback asserts a proof: production Session/Census/Proof own permission.
namespace {
namespace br=abi::arm64::runtime_root_visit;
br::MutexPrefix boot_trace_lock;br::RwPrefix boot_class_lock;
void*boot_trace_slot=&boot_trace_lock;void*boot_class_slot=&boot_class_lock;
size_t boot_original_root_streams=0,boot_root_writebacks=0,boot_census_invalidations=0;
prepared_roots::Registry*boot_census_registry=nullptr;bool boot_expect_census_invalidation=false;
void BootFinish(void*h,void*self,int32_t type){if(boot_expect_census_invalidation){assert(boot_census_registry);const auto r=boot_census_registry->Final();assert(r.get&&r.get(r.owner,0)==nullptr);boot_expect_census_invalidation=false;++boot_census_invalidations;}OwnerFinish(h,self,type);}
void*boot_runtime_root=nullptr;source_commit::Plan*boot_census_plan=nullptr;void*boot_census_moved=nullptr;size_t boot_census_moves=0;
void BootStart(void*h,void*self,int32_t cause,int32_t collector){
 if(boot_census_plan){assert(!stopped&&!gc&&!disabled&&self==thread.data());auto*plan=boot_census_plan;boot_census_plan=nullptr;boot_expect_census_invalidation=true;void*from=plan->new_classes.at(0);auto*to=heap.New(160);std::memcpy(to,from,160);
  MoveRoots moving;moving.old_value=uint32_t(uintptr_t(from));moving.next_value=uint32_t(uintptr_t(to));OriginalRoots(self,&moving,0);
  for(auto*field:new_fields)if(field->declaring_class==uintptr_t(from))field->declaring_class=uint32_t(uintptr_t(to));
  for(size_t i=0;i<3;++i){void*method=reinterpret_cast<void*>(uintptr_t(static_cast<class_metadata::Class17*>(to)->methods)+8+32*i);if(Read32(method)==uintptr_t(from))Put32(method,0,uint32_t(uintptr_t(to)));}
  for(auto*a:managed_arrays)for(uint32_t i=0;i<Read32(a,8);++i)if(Read32(a,12+size_t(i)*4)==uintptr_t(from))Put32(a,12+size_t(i)*4,uint32_t(uintptr_t(to)));
  if(Read32(plan->new_classes_root,12)==uintptr_t(from))Put32(plan->new_classes_root,12,uint32_t(uintptr_t(to)));
  // The original collector also updates DexCache native GcRoot<Class>
  // arrays; moving only TLS handles would manufacture a stale-cache failure.
  for(void*raw_cache:{reinterpret_cast<void*>(uintptr_t(static_cast<class_metadata::Class17*>(to)->dex_cache)),published_cache}){auto*c=static_cast<type_cache::Dual*>(raw_cache);assert(c&&c->dex_file);auto*file=reinterpret_cast<const dex_mapping_abi::Prefix*>(uintptr_t(c->dex_file));const uint32_t types_count=Read32(file->begin,64);auto*pairs=reinterpret_cast<type_cache::Pair*>(uintptr_t(c->types));auto*full=reinterpret_cast<uint32_t*>(uintptr_t(c->types_array));if(pairs)for(size_t i=0;i<1024;++i)if(pairs[i].object==uintptr_t(from))pairs[i].object=uint32_t(uintptr_t(to));if(full)for(size_t i=0;i<types_count;++i)if(full[i]==uintptr_t(from))full[i]=uint32_t(uintptr_t(to));}
  assert(plan->new_classes[0]==from);boot_census_moved=to;relocated_new=to;session_allocated=to;++boot_census_moves;
 }
 OwnerStart(h,self,cause,collector);
}
struct BootOriginalCursor:abi::arm64::stack::Visitor13 {bool VisitFrame()override{return true;}} boot_original_cursor;
BootContext boot_original_context;
int32_t BootRootTid(){return 123;}
void BootRootMutexConstruct(void*p,const char*n,uint8_t l,bool recursive){assert(!stopped&&l==br::kGenericBottom&&!recursive);auto*m=new(p)br::MutexPrefix;m->name=n;m->level=l;m->respond=false;m->state=0;m->owner=0;m->recursion=0;m->monitor_id=0;}
void BootRootRwConstruct(void*p,const char*n,uint8_t l){assert(!stopped&&l==br::kGenericBottom);auto*m=new(p)br::RwPrefix;m->name=n;m->level=l;m->respond=false;m->state=0;m->owner=0;m->contenders=0;}
void BootRootMutexDestroy(void*p){static_cast<br::MutexPrefix*>(p)->~MutexPrefix();}
void BootRootRwDestroy(void*p){static_cast<br::RwPrefix*>(p)->~RwPrefix();}
int32_t BootRootMutexOwner(const void*p){return static_cast<const br::MutexPrefix*>(p)->owner;}
int32_t BootRootRwOwner(const void*p){const auto*m=static_cast<const br::RwPrefix*>(p);return m->state==0?0:m->state>0?-1:m->owner.load();}
bool BootRootShared(const void*p,const void*self){assert(self==thread.data());const auto*m=static_cast<const br::RwPrefix*>(p);return m->state==-1&&m->owner==123;}
void BootRootMutexLock(void*p,void*self){if(p==&boot_list_mutex){BootListLock(p,self);return;}assert(self==thread.data()&&!stopped);auto*m=static_cast<br::MutexPrefix*>(p);m->state=1;m->owner=123;}
void BootRootMutexUnlock(void*p,void*self){if(p==&boot_list_mutex){BootListUnlock(p,self);return;}assert(self==thread.data()&&!stopped);auto*m=static_cast<br::MutexPrefix*>(p);m->state=0;m->owner=0;}
void BootRootRwLock(void*p,void*self){assert(self==thread.data()&&!stopped);auto*m=static_cast<br::RwPrefix*>(p);m->state=-1;m->owner=123;}
void BootRootRwUnlock(void*p,void*self){assert(self==thread.data()&&!stopped);auto*m=static_cast<br::RwPrefix*>(p);m->state=0;m->owner=0;}
void*BootRootBarrier(void*p){return reinterpret_cast<void*>(uintptr_t(Read32(p)));}
void BootRuntimeRoots(void*r,br::Visitor*v,uint8_t flags){assert(r==&runtime&&gc&&stopped==1&&(flags==br::kAll||flags==br::kAllPrecise));++boot_original_root_streams;
 br::Info ordinary{nullptr,1,0};void**global[]{&boot_runtime_root};if(boot_runtime_root)v->VisitRoots(global,1,ordinary);
 boot_original_cursor.thread=foreign_thread.data();boot_original_cursor.quick_frame=reinterpret_cast<void**>(&boot_quick);boot_original_cursor.header=boot_header;boot_original_cursor.pc=uintptr_t(boot_header)+36;boot_original_cursor.context=&boot_original_context;
 void*declaring=Root(boot_compiled_method);br::JavaFrameInfo info{{nullptr,br::kJavaFrameRoot,19},&boot_original_cursor,br::kDeclaringClassVreg};void**one[]{&declaring};v->VisitRoots(one,1,info.base);
}
void BootReplace(void*self,const void*raw){MatchedReplace(self,raw);
 struct ReplaceRoot final:br::Visitor{const object_replacement::object_map_abi::Map&map;explicit ReplaceRoot(const void*p):map(*static_cast<const object_replacement::object_map_abi::Map*>(p)){}
 void*Next(void*p){for(auto*n=static_cast<const object_replacement::object_map_abi::Node*>(map.sentinel.next);n;n=static_cast<const object_replacement::object_map_abi::Node*>(n->next))if(n->key==p)return n->value;return p;}
 void VisitRoots(void***slots,size_t count,const br::Info&)override{for(size_t i=0;i<count;++i){void*old=*slots[i];*slots[i]=Next(old);if(old!=*slots[i])++boot_root_writebacks;}}
 void VisitRoots(uint32_t**slots,size_t count,const br::Info&)override{for(size_t i=0;i<count;++i){void*old=reinterpret_cast<void*>(uintptr_t(*slots[i]));*slots[i]=uint32_t(uintptr_t(Next(old)));if(old!=reinterpret_cast<void*>(uintptr_t(*slots[i])))++boot_root_writebacks;}}
 }visitor(raw);BootRuntimeRoots(&runtime,&visitor,br::kAll);
}
void*BootRootSymbol(const char*n){
 if(!std::strcmp(n,s::kFinishGC))return reinterpret_cast<void*>(owner_finish_proxy?owner_finish_proxy:BootFinish);
 if(!std::strcmp(n,s::kStartGC))return reinterpret_cast<void*>(owner_start_proxy?owner_start_proxy:BootStart);
 if(std::strstr(n,"ReplaceReferences"))return reinterpret_cast<void*>(BootReplace);
 if(!boot_frame_binding)return nullptr;
#define BR_FN(symbol,fn) if(!std::strcmp(n,symbol))return reinterpret_cast<void*>(fn)
 BR_FN(br::kVisit,BootRuntimeRoots);BR_FN("artReadBarrierForRootSlow",BootRootBarrier);BR_FN(br::kTid,BootRootTid);
 BR_FN(br::kMutexOwner,BootRootMutexOwner);BR_FN(br::kRwOwner,BootRootRwOwner);BR_FN(br::kShared,BootRootShared);
 BR_FN(br::kMutexConstruct,BootRootMutexConstruct);BR_FN(br::kMutexDestroy,BootRootMutexDestroy);BR_FN(br::kMutexLock,BootRootMutexLock);BR_FN(br::kMutexUnlock,BootRootMutexUnlock);
 BR_FN(br::kRwConstruct,BootRootRwConstruct);BR_FN(br::kRwDestroy,BootRootRwDestroy);BR_FN(br::kRwLock,BootRootRwLock);BR_FN(br::kRwUnlock,BootRootRwUnlock);
#undef BR_FN
 if(!std::strcmp(n,br::kClassLock))return &boot_class_slot;if(!std::strcmp(n,br::kTraceLock))return &boot_trace_slot;
 return nullptr;
}
}
