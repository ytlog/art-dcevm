#include "platform/common/native_hierarchy_allocator.h"
#include "platform/common/native_hierarchy_parent.h"
#include "platform/common/class_metadata_abi.h"
#include "platform/common/dex_mapping_abi.h"
#include "platform/common/method_identity_calibration.h"
#include "platform/abi/arm64/android11/dex_cache.h"
#include <deque>
#include <cstring>
#include <new>
#include <algorithm>
namespace artdcevm::native_hierarchy_allocator {
namespace {
using Obj=abi::arm64::android11::ReleaseObjectPointer;
bool Mapped(const void* p,size_t n)noexcept{size_t extent=0;return p&&method_identity::ReadableMappingExtent(p,&extent)&&extent>=n;}
template<class T>T Read(const void* p,size_t n){T value;std::memcpy(&value,static_cast<const char*>(p)+n,sizeof(value));return value;}
void Error(std::string* e,const char* text)noexcept{if(e)try{*e=text;}catch(const std::bad_alloc&){} }
const class_metadata::Layout& Layout(const modern_field_metadata::FieldMetadata& m){return m.StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;}
bool Dex(void* cache,uint16_t index,evolution::MappedDexMetadata* output) {
 constexpr size_t offset=offsetof(abi::arm64::android11::LegacyCache,dex_file);
 if(!Mapped(cache,offset+8))return false;
 auto* file=reinterpret_cast<void*>(uintptr_t(Read<uint64_t>(cache,offset)));
 if(!Mapped(file,sizeof(dex_mapping_abi::Prefix)))return false;
 auto prefix=Read<dex_mapping_abi::Prefix>(file,0);
 if(!Mapped(prefix.begin,0x70)||std::memcmp(prefix.begin,"dex\n",4)||prefix.begin[7])return false;
 const uint32_t size=Read<uint32_t>(prefix.begin,32);
 if(size<0x70||!Mapped(prefix.begin,size)||!prefix.data_begin||!prefix.data_size||!Mapped(prefix.data_begin,prefix.data_size))return false;
 *output={prefix.begin,size,prefix.data_begin,prefix.data_size,index};return true;
}
bool SameSchema(const evolution::ClassSchema& a,const evolution::ClassSchema& b)noexcept{
 if(a.descriptor!=b.descriptor||a.superclass!=b.superclass||a.interfaces!=b.interfaces||a.access!=b.access||a.fields.size()!=b.fields.size()||a.methods.size()!=b.methods.size())return false;
 const auto same=[](const auto& x,const auto& y){for(size_t n=0;n<x.size();++n)if(x[n].name!=y[n].name||x[n].descriptor!=y[n].descriptor||x[n].access!=y[n].access||x[n].dex_index!=y[n].dex_index)return false;return true;};
 return same(a.fields,b.fields)&&same(a.methods,b.methods);
}
struct Capture {Service* service;source_handle_roots::Pool* pool;void* root;void* allocated;runnable_jni::Context context;Capture* previous;};
thread_local Capture* capture=nullptr;
thread_local Batch* active_batch=nullptr;
struct ScopedCapture {Capture value;ScopedCapture(Service* s,source_handle_roots::Pool* p,void* r,void* allocated,const runnable_jni::Context& c):value{s,p,r,allocated,c,capture}{capture=&value;}~ScopedCapture(){capture=value.previous;}};
bool Class(const modern_field_metadata::FieldMetadata& m,void* p,field_copy::CopyClassView* out){return m.ClassIdentity(p,out,nullptr);}
}
bool Service::Bind(HookEngine& h,const modern_field_metadata::FieldMetadata& m,source_handle_roots::Service& roots,const runtime_services::Snapshot& snapshot,Service* out,std::string* error){
 if(!out||!m.Calibrated()||!snapshot.receivers.class_linker){Error(error,"Hierarchy allocator lacks observed ClassLinker and metadata");return false;}
 Service candidate;candidate.metadata_=&m;candidate.roots_=&roots;candidate.hooks_=&h;candidate.class_linker_=snapshot.receivers.class_linker;
 candidate.enter_=reinterpret_cast<Enter>(h.FindSymbol("libart.so","_ZN3art7Monitor12MonitorEnterEPNS_6ThreadENS_6ObjPtrINS_6mirror6ObjectEEEb"));
 candidate.exit_=reinterpret_cast<Exit>(h.FindSymbol("libart.so","_ZN3art7Monitor11MonitorExitEPNS_6ThreadENS_6ObjPtrINS_6mirror6ObjectEEE"));
 if(!candidate.enter_||!candidate.exit_){Error(error,"Missing fixed source Class monitor functions");return false;}
 *out=candidate;return true;
}
bool Service::SetOriginals(const Functions& f,std::string* e){
 if(Current()||!f.allocate||!f.find||!f.ensure_ids||!f.retire_status||!f.failure||!f.load_class){Error(e,"Hierarchy originals or no-throw source failure bridge are incomplete");return false;}
 functions_=f;originals_=true;observed_=false;return true;
}
bool Service::CalibrateRetirement(const runnable_jni::Context& c,jobject fixture,std::string* error){
 if(!roots_||!roots_->Calibrated()||!originals_||!c.env||!fixture){Error(error,"Monitor proof requires live source roots and owned public fixture");return false;}
 try{
  void* raw=nullptr;if(!roots_->BorrowStrongGlobal(c,fixture,&raw))return false;
  source_handle_roots::Pool pool(*roots_);void* slot=pool.Add(raw,error);if(!slot)return false;
  auto thread=c.env->FindClass("java/lang/Thread");if(!thread)return false;
  auto holds=c.env->GetStaticMethodID(thread,"holdsLock","(Ljava/lang/Object;)Z");
  if(!holds||c.env->ExceptionCheck()){c.env->DeleteLocalRef(thread);return false;}
  bool before=c.env->CallStaticBooleanMethod(thread,holds,fixture);bool ok=!c.env->ExceptionCheck()&&!before;
  void* value=nullptr;ok=ok&&pool.Read(slot,&value);
  if(!ok){c.env->DeleteLocalRef(thread);return false;}
  auto entered=enter_(c.self,{uintptr_t(value)},false);void* current=nullptr;
  const bool entered_ok=pool.Read(slot,&current)&&entered.reference==uintptr_t(current);
  const bool held=c.env->CallStaticBooleanMethod(thread,holds,fixture);
  ok=entered_ok&&held&&!c.env->ExceptionCheck();
  if(!pool.Read(slot,&current)||!exit_(c.self,{uintptr_t(current)}))std::abort();
  const bool after=c.env->CallStaticBooleanMethod(thread,holds,fixture);
  ok=ok&&!after&&!c.env->ExceptionCheck();c.env->DeleteLocalRef(thread);
  monitor_=ok;return ok;
 }catch(const std::bad_alloc&){Error(error,"Monitor calibration allocation failed");return false;}
}
void Service::CaptureAllocated(const void* handle){
 if(auto* current=Current()){current->CaptureAllocated(handle);return;}
 if(capture&&capture->service==this){void* value=nullptr;
  if(roots_->BorrowHandle(capture->context,handle,&value)&&value&&Mapped(value,Layout(*metadata_).size))
   capture->pool->Write(capture->allocated,value);
 }
}
void Service::CaptureLinked(const void* handle){
 if(auto* current=Current()){current->CaptureLinked(handle);return;}
 if(capture&&capture->service==this){void* value=nullptr;
  if(roots_->BorrowHandle(capture->context,handle,&value)&&value&&Mapped(value,Layout(*metadata_).size))
   capture->pool->Write(capture->root,value);
 }
}
void* Service::ObserveOriginal(const runnable_jni::Context& c,jobject strong,void* owner,const void* old,const void* parent,const void* cache,uint16_t index,std::string* error){
 if(!originals_||!owner||!roots_->ObserveAllocatorHandle(c,strong,old,error))return nullptr;
 if(!monitor_)return reinterpret_cast<void*>(functions_.allocate(owner,const_cast<void*>(old),const_cast<void*>(parent),const_cast<void*>(cache),index).reference);
 source_handle_roots::Pool pool(*roots_);void* forwarded=nullptr;
 try{
  void* old_value=nullptr;void* parent_value=nullptr;void* cache_value=nullptr;
  if(!roots_->BorrowHandle(c,old,&old_value)||!roots_->BorrowHandle(c,parent,&parent_value)||!roots_->BorrowHandle(c,cache,&cache_value))return nullptr;
  auto old_root=pool.Add(old_value,error),parent_root=pool.Add(parent_value,error),cache_root=pool.Add(cache_value,error),new_root=pool.Add(nullptr,error),allocated_root=pool.Add(nullptr,error);
  if(!old_root||!parent_root||!cache_root||!new_root||!allocated_root){functions_.failure(owner,JVMTI_ERROR_OUT_OF_MEMORY,"Hierarchy observer roots allocation failed");return nullptr;}
  field_copy::CopyClassView original_identity{};
  if(!Class(*metadata_,old_value,&original_identity))return nullptr;
  auto loader=pool.Add(original_identity.loader,error);if(!loader){functions_.failure(owner,JVMTI_ERROR_OUT_OF_MEMORY,"Hierarchy observer loader root allocation failed");return nullptr;}
  void* result=nullptr;
  {ScopedCapture guard(this,&pool,new_root,allocated_root,c);forwarded=result=reinterpret_cast<void*>(functions_.allocate(owner,old_root,parent_root,cache_root,index).reference);}
  void* captured=nullptr;pool.Read(new_root,&captured);
  void* allocated=nullptr;pool.Read(allocated_root,&allocated);
  if(!result){
   if(captured&&!Retire(c,pool,old_root,new_root,cache_root,loader))std::abort();
   if(allocated&&allocated!=captured&&!Retire(c,pool,old_root,allocated_root,cache_root,loader))std::abort();
   return nullptr;
  }
  field_copy::CopyClassView before{},after{};void* current_old=nullptr;void* current_cache=nullptr;
  pool.Read(old_root,&current_old);pool.Read(cache_root,&current_cache);
  bool ok=captured==result&&Class(*metadata_,current_old,&before)&&Class(*metadata_,captured,&after)&&
   before.loader==after.loader&&!std::strcmp(before.descriptor,after.descriptor)&&
   metadata_->Reference(captured,offsetof(class_metadata::Class11,dex_cache))==current_cache;
  if(ok){auto found=functions_.find(class_linker_,c.self,before.descriptor,loader);pool.Read(old_root,&current_old);ok=found.reference==uintptr_t(current_old);}
  if(allocated&&allocated!=captured){class_metadata::State retired{};const auto& layout=Layout(*metadata_);
   ok=ok&&class_metadata::ReadState(layout,allocated,layout.size,&retired)&&retired.status==1;
  }
  if(ok&&allocated&&monitor_)observed_=true;
  // This was the original source operation, not an extra calibration clone.
  // Its result and original error semantics are forwarded unchanged.
  return result;
 }catch(const std::bad_alloc&){if(forwarded)return forwarded;functions_.failure(owner,JVMTI_ERROR_OUT_OF_MEMORY,"Hierarchy observer allocation failed");return nullptr;}
}
void Service::LoadClass(void* linker,void* self,const void* dex,const void* def,const void* handle)noexcept {
 if(!originals_)std::abort();
 // Unrelated Class loads preserve the original source behaviour. A real
 // source allocation frame is mandatory before any early private capture.
 const bool observer=capture&&capture->service==this&&capture->context.self==self;
 const bool batch=CurrentSelf(self);
 if(linker==class_linker_&&(observer||batch))CaptureAllocated(handle);
 functions_.load_class(linker,self,dex,def,handle);
}
bool Service::EnsureInstanceIds(const void* handle)noexcept {
 if(!originals_)return false;
 CaptureLinked(handle);return functions_.ensure_ids(handle);
}
bool Service::Retire(const runnable_jni::Context& c,source_handle_roots::Pool& pool,void* old_root,void* new_root,void* cache_root,void* loader_root)const noexcept{
 if(!monitor_||!originals_||!roots_->ReadyForSourceAccess())return false;
 void* old=nullptr;void* object=nullptr;void* cache=nullptr;
 if(!pool.Read(old_root,&old)||!pool.Read(new_root,&object)||!pool.Read(cache_root,&cache)||!old||!object||old==object)return false;
 field_copy::CopyClassView before{},after{};
 if(!Class(*metadata_,old,&before)||!Class(*metadata_,object,&after)||before.loader!=after.loader||
    std::strcmp(before.descriptor,after.descriptor)||metadata_->Reference(object,offsetof(class_metadata::Class11,dex_cache))!=cache)return false;
 auto found=functions_.find(class_linker_,c.self,before.descriptor,loader_root);
 if(!pool.Read(old_root,&old)||found.reference!=uintptr_t(old)||!pool.Read(new_root,&object)||!pool.Read(cache_root,&cache)||old==object||
    !Class(*metadata_,object,&after)||after.loader!=metadata_->Reference(old,offsetof(class_metadata::Class11,class_loader))||
    std::strcmp(before.descriptor,after.descriptor)||metadata_->Reference(object,offsetof(class_metadata::Class11,dex_cache))!=cache)return false;
 // IDs-boundary capture occurs after ForceClassInitialized has made the class
 // visibly initialized. Status 14 cannot be retired even with exclusive locks.
 class_metadata::State state{};const auto& layout=Layout(*metadata_);
 if(!Mapped(object,layout.size)||!class_metadata::ReadState(layout,object,layout.size,&state)||state.status==14)return false;
 auto entered=enter_(c.self,{uintptr_t(object)},false);
 if(!pool.Read(new_root,&object)||entered.reference!=uintptr_t(object))return false;
 const bool readable=class_metadata::ReadState(layout,object,layout.size,&state)&&state.status!=14;
 if(readable&&state.status!=1)functions_.retire_status(object,1);
 const bool retired=readable&&class_metadata::ReadState(layout,object,layout.size,&state)&&state.status==1;
 if(!pool.Read(new_root,&object)||!exit_(c.self,{uintptr_t(object)}))std::abort();
 return retired;
}
struct Batch::State final:rooted_hierarchy::Access {
 Service& service;runnable_jni::Context context;native_hierarchy_graph::Authority& authority;
 std::unique_ptr<source_handle_roots::Pool> owned_pool;source_handle_roots::Pool& pool;rooted_hierarchy::Allocator allocator;
 enum class Phase {Unallocated,Authorized,Captured,Linked};
 const source_handle_roots::StoppedReader*stopped_reader=nullptr;
 struct Record {RecordView view{};evolution::ClassSchema declarations;size_t node=SIZE_MAX;void* loader_root=nullptr;void* allocated_root=nullptr;bool captured_allocated=false,captured=false;Phase phase=Phase::Unallocated;};
 std::deque<Record> records;Record* pending=nullptr;void* invoking_owner=nullptr;bool prepared=false,failed=false,finished=false,committing=false;
 State(Service& s,const runnable_jni::Context& c,native_hierarchy_graph::Authority& a):service(s),context(c),authority(a),owned_pool(new source_handle_roots::Pool(*s.roots_)),pool(*owned_pool){}
 State(Service& s,const runnable_jni::Context& c,native_hierarchy_graph::Authority& a,source_handle_roots::Pool& p):service(s),context(c),authority(a),pool(p){}
 void* Get(rooted_hierarchy::Root slot)const override{void* p=nullptr;if(stopped_reader&&stopped_reader->FinalRootWindow())return stopped_reader->ReadStoppedRoot(slot,&p)?p:nullptr;return pool.Read(slot,&p)?p:nullptr;}
 rooted_hierarchy::Root Copy(rooted_hierarchy::Root slot)override{return pool.Add(Get(slot),nullptr);}
 rooted_hierarchy::Root Empty()override{return pool.Add(nullptr,nullptr);}
 void Set(rooted_hierarchy::Root slot,void* object)override{if(!pool.Write(slot,object))Fail("Invalid source root write");}
 rooted_hierarchy::Root Loader(rooted_hierarchy::Root root)override{field_copy::CopyClassView view{};if(!Class(*service.metadata_,Get(root),&view)){Fail("Source class loader identity unavailable");return nullptr;}return pool.Add(view.loader,nullptr);}
 rooted_hierarchy::Root Cache(rooted_hierarchy::Root root)override{return pool.Add(service.metadata_->Reference(Get(root),offsetof(class_metadata::Class11,dex_cache)),nullptr);}
 uint16_t ClassIndex(rooted_hierarchy::Root root)const override{
  class_metadata::Identity identity{};const auto& layout=Layout(*service.metadata_);auto* p=Get(root);
  if(!Mapped(p,layout.size)||!class_metadata::ReadIdentity(layout,p,layout.size,&identity)||identity.class_def_index<0||identity.class_def_index>UINT16_MAX){const_cast<State*>(this)->Fail("Unrepresentable source class_def index");return 0;}
  return uint16_t(identity.class_def_index);
 }
 std::string RequestedParent(rooted_hierarchy::Root root,uint16_t index)override{
  evolution::MappedDexMetadata dex{};const char* parent=nullptr;std::string error;
  if(!Dex(Get(root),index,&dex)||!evolution::ReadMappedDexSuperclassDescriptor(dex,&parent,&error)){Fail("Incoming source superclass DEX identity unavailable");return {};}
  return parent?parent:"";
 }
 rooted_hierarchy::Root Resolve(const std::string& name,rooted_hierarchy::Root loader)override{
  auto found=service.functions_.find(service.class_linker_,context.self,name.c_str(),loader);
  if(!found.reference){Fail("Original source FindClass failed");return nullptr;}
  return pool.Add(reinterpret_cast<void*>(found.reference),nullptr);
 }
 bool Subclass(rooted_hierarchy::Root child,rooted_hierarchy::Root parent)const override{
  auto* target=Get(parent);void* cursor=Get(child);
  for(size_t depth=0;cursor&&depth<4096;++depth){if(cursor==target)return true;field_copy::CopyClassView view{};if(!Class(*service.metadata_,cursor,&view))return false;cursor=view.parent;}
  return false;
 }
 void Fail(const char* text)override{failed=true;if(invoking_owner)service.functions_.failure(invoking_owner,JVMTI_ERROR_UNSUPPORTED_REDEFINITION_HIERARCHY_CHANGED,text);}
 void* Allocate(void* owner,rooted_hierarchy::Root old,rooted_hierarchy::Root parent,rooted_hierarchy::Root cache,uint16_t index,rooted_hierarchy::Root result)override{
  if(failed)return nullptr;
  records.emplace_back();auto& record=records.back();record.view={owner,index,old,result,cache,parent,&record.declarations,{}};
  std::string declarations_error;
  if(!Dex(Get(cache),index,&record.view.dex)||!evolution::ReadMappedDexDeclarations(record.view.dex,&record.declarations,&declarations_error)){Fail("Actual incoming source declarations unavailable");return nullptr;}
  auto* graph=authority.Staged();field_copy::CopyClassView before{};
  if(!graph||!Class(*service.metadata_,Get(old),&before)||record.declarations.descriptor!=before.descriptor){Fail("Source old Class/incoming descriptor differs");return nullptr;}
  for(size_t n=0;n<graph->nodes.size();++n)if(graph->nodes[n].klass==Get(old)){record.node=n;break;}
  if(record.node==SIZE_MAX||!graph->nodes[record.node].affected){Fail("Source allocator class absent from staged affected graph");return nullptr;}
  record.loader_root=Loader(old);record.allocated_root=Empty();if(!record.loader_root||!record.allocated_root)return nullptr;
  const auto public_original=service.functions_.find(service.class_linker_,context.self,before.descriptor,record.loader_root);
  if(public_original.reference!=uintptr_t(Get(old))){Fail("Original ClassLinker lookup differs from rooted source Class");return nullptr;}
  native_hierarchy_graph::Prepared value{owner,Get(old),nullptr,Get(cache),Get(parent),index,&record.declarations,old};
  if(!authority.AuthorizeAllocation(context,value,nullptr)){Fail("Staged graph rejected actual source allocation");return nullptr;}
  record.phase=Phase::Authorized;
  auto* previous=pending;pending=&record;void* returned=nullptr;
  {ScopedCapture guard(&service,&pool,result,record.allocated_root,context);returned=reinterpret_cast<void*>(service.functions_.allocate(owner,old,parent,cache,index).reference);}
  pending=previous;
  // Never overwrite the early captured root with a different allocator return.
  if(!returned)return nullptr;
  if(!record.captured||Get(result)!=returned){Fail("Original allocator result lacks exact IDs-boundary capture");return nullptr;}
  record.phase=Phase::Captured;
  value={owner,Get(old),Get(result),Get(cache),Get(parent),index,&record.declarations,old};
  if(!authority.PrepareLinked(context,&value,nullptr)){Fail("Captured source private-cache preparation failed");return nullptr;}
  if(!authority.VerifyPrepared(context,value,nullptr)){Fail("Linked native Class differs from staged graph");return nullptr;}
  record.phase=Phase::Linked;return Get(result);
 }
 bool ValidateReplacement(rooted_hierarchy::Root old,rooted_hierarchy::Root replacement,rooted_hierarchy::Root parent,rooted_hierarchy::Root cache)const override{
  field_copy::CopyClassView before{},after{};
  return Class(*service.metadata_,Get(old),&before)&&Class(*service.metadata_,Get(replacement),&after)&&
   before.loader==after.loader&&!std::strcmp(before.descriptor,after.descriptor)&&after.parent==Get(parent)&&
   service.metadata_->Reference(Get(replacement),offsetof(class_metadata::Class11,dex_cache))==Get(cache);
 }
};
bool Service::CurrentSelf(void* self)const noexcept{return Current()&&Current()->state_->context.self==self;}
Batch* Service::Current()const noexcept{return active_batch && &active_batch->state_->service==this ? active_batch : nullptr;}
Batch::Batch(Service& service,const runnable_jni::Context& c,native_hierarchy_graph::Authority& a):state_(new State(service,c,a)){
 if(active_batch)throw std::bad_alloc();
 active_batch=this;
}
Batch::Batch(Service& service,const runnable_jni::Context& c,native_hierarchy_graph::Authority& a,source_handle_roots::Pool& p):state_(new State(service,c,a,p)){
 if(active_batch||!p.UsesService(*service.roots_)||!p.ReadyForSourceAccess())throw std::bad_alloc();
 active_batch=this;
}
Batch::~Batch(){if(state_){if(!state_->finished&&!Complete(false,nullptr))std::abort();if(active_batch!=this)std::abort();active_batch=nullptr;}}
bool Batch::Prepare(const transaction_root::Holder* holder,std::string* error){
 auto& s=*state_;if(s.prepared||!s.service.observed_||!s.service.monitor_||!s.pool.ReadyForSourceAccess())return false;
 try{source_allocation_transaction::Snapshot snapshot;
  if(!source_allocation_transaction::Read(*s.service.hooks_,*s.service.metadata_,holder,s.pool,&snapshot,error)||!s.allocator.Prepare(std::move(snapshot.targets)))return false;
  s.prepared=true;return true;
 }catch(const std::bad_alloc&){s.failed=true;Error(error,"Source hierarchy preparation allocation failed");return false;}
}
void* Batch::Allocate(void* owner,const void* old,const void* cache,uint16_t index,std::string* error){
 auto& s=*state_;if(!s.prepared||s.failed||s.finished)return nullptr;
 s.invoking_owner=owner;
 try{void* old_value=nullptr;void* cache_value=nullptr;
  if(!s.service.roots_->BorrowHandle(s.context,old,&old_value)||!s.service.roots_->BorrowHandle(s.context,cache,&cache_value))return nullptr;
  auto old_root=s.pool.Add(old_value,error),cache_root=s.pool.Add(cache_value,error);if(!old_root||!cache_root)return nullptr;
  return s.allocator.Allocate(s,owner,old_root,cache_root,index);
 }catch(const std::bad_alloc&){s.failed=true;s.pending=nullptr;s.service.functions_.failure(owner,JVMTI_ERROR_OUT_OF_MEMORY,"Source hierarchy allocation failed");Error(error,"Source hierarchy allocation failed");return nullptr;}
}
void Batch::CaptureAllocated(const void* handle){
 auto& s=*state_;if(!s.pending||s.failed)return;
 void* value=nullptr;if(!s.service.roots_->BorrowHandle(s.context,handle,&value)||!value)return;
 auto& record=*s.pending;field_copy::CopyClassView before{},actual{};
 if(!Class(*s.service.metadata_,s.Get(record.view.old_root),&before)||!Class(*s.service.metadata_,value,&actual)||
    value==s.Get(record.view.old_root)||actual.loader!=before.loader||std::strcmp(actual.descriptor,before.descriptor)||
    s.service.metadata_->Reference(value,offsetof(class_metadata::Class11,dex_cache))!=s.Get(record.view.cache_root)){
  s.failed=true;return;
 }
 if(record.captured_allocated&&s.Get(record.allocated_root)!=value){s.failed=true;return;}
 if(!s.pool.Write(record.allocated_root,value)){s.failed=true;return;}record.captured_allocated=true;
}
void Batch::CaptureLinked(const void* handle){
 auto& s=*state_;if(!s.pending||s.failed)return;
 void* captured=nullptr;if(!s.service.roots_->BorrowHandle(s.context,handle,&captured)||!captured){s.failed=true;return;}
 auto& r=*s.pending;field_copy::CopyClassView old{},linked{};
 if(!Class(*s.service.metadata_,s.Get(r.view.old_root),&old)||!Class(*s.service.metadata_,captured,&linked)||
    old.loader!=linked.loader||std::strcmp(old.descriptor,linked.descriptor)||linked.parent!=s.Get(r.view.parent_root)||
    s.service.metadata_->Reference(captured,offsetof(class_metadata::Class11,dex_cache))!=s.Get(r.view.cache_root)){s.failed=true;return;}
 if(r.captured&&s.Get(r.view.new_root)!=captured){s.failed=true;return;}
 if(!s.pool.Write(r.view.new_root,captured)){s.failed=true;return;}r.captured=true;
}
bool Batch::Complete(bool committed,std::string* error){
 auto& s=*state_;if(s.finished)return true;
 if(committed&&!s.committing){Error(error,"Unsealed hierarchy commit cannot finish");return false;}
 if(!committed&&s.committing){Error(error,"Source hierarchy mutation has already begun");return false;}
 if(!committed)for(auto& r:s.records){
  if(r.captured&&!s.service.Retire(s.context,s.pool,r.view.old_root,r.view.new_root,r.view.cache_root,r.loader_root)){Error(error,"Captured linked source temporary retirement failed");return false;}
  if(r.captured_allocated&&s.Get(r.allocated_root)!=s.Get(r.view.new_root)&&!s.service.Retire(s.context,s.pool,r.view.old_root,r.allocated_root,r.view.cache_root,r.loader_root)){Error(error,"Captured allocation source temporary retirement failed");return false;}
 }
 s.finished=true;return true;
}
size_t Batch::Count()const noexcept{return state_->records.size();}
void Batch::SetStoppedReader(const source_handle_roots::StoppedReader*r)noexcept{if(state_->stopped_reader||state_->finished)std::abort();state_->stopped_reader=r;}
source_handle_roots::Pool& Batch::Roots()const noexcept{return state_->pool;}
bool Batch::At(size_t index,RecordView* out)const noexcept{if(!out||index>=Count())return false;*out=state_->records[index].view;return true;}
void* Batch::Get(void* root)const noexcept{return state_->Get(root);}
void* Batch::Find(const char* name,void* loader)const{
 auto& s=*state_;if(!name||s.failed)return nullptr;
 for(const auto& r:s.records){field_copy::CopyClassView view{};auto* value=s.Get(r.view.new_root);
  if(value&&Class(*s.service.metadata_,value,&view)&&view.loader==loader&&!std::strcmp(view.descriptor,name))return r.view.new_root;
 }
 return nullptr;
}
void* Batch::OriginalFind(const char* name,void* loader_root)const{
 auto& s=*state_;if(!name||!s.pool.ReadyForSourceAccess())return nullptr;
 return reinterpret_cast<void*>(s.service.functions_.find(s.service.class_linker_,s.context.self,name,loader_root).reference);
}
bool Service::VerifyStaged(const native_hierarchy_graph::Graph& graph,const std::vector<native_hierarchy_graph::Input>& inputs,std::string* error)const{
 if(!originals_||!observed_||!monitor_||Current()||!roots_->ReadyForSourceAccess()||inputs.empty()||graph.nodes.empty()||inputs.size()!=graph.inputs.size()){
  Error(error,"Hierarchy source allocator/monitor observation is incomplete");return false;
 }
 for(size_t n=0;n<inputs.size();++n){const auto& input=inputs[n];
  // Original source Finish visits superclass descendants. A complete expanded
  // interface/array cohort needs a distinct real source owner; do not grant it.
  if(input.before.access!=input.after.access||input.before.interfaces!=input.after.interfaces){Error(error,"Source allocator cohort/class policy does not prove interface or modifier changes");return false;}
  if(graph.inputs[n]>=graph.nodes.size())return false;
  const auto& node=graph.nodes[graph.inputs[n]];
  if(node.strong_class!=input.strong_original||!node.affected||node.descriptor!=input.before.descriptor)return false;
 }
 for(const auto& node:graph.nodes){field_copy::CopyClassView actual{};class_metadata::State state{};const auto& layout=Layout(*metadata_);
  void* native_parent=nullptr;if(!native_hierarchy_graph::NativeSuperclass(graph,node,false,&native_parent))return false;
  if(!Mapped(node.klass,layout.size)||!class_metadata::ReadState(layout,node.klass,layout.size,&state)||state.IsPrimitive()||(state.access_flags&0x40000u)||
     !Class(*metadata_,node.klass,&actual)||node.descriptor!=actual.descriptor||node.loader!=actual.loader||
     (node.parent!=SIZE_MAX&&node.parent>=graph.nodes.size())||
     actual.parent!=native_parent)return false;
 }
 return true;
}
bool Service::VerifyPrepared(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared& p,std::string*)const{
 auto* current=Current();if(!current||!observed_||!monitor_)return false;auto& s=*current->state_;
 for(const auto& r:s.records)if(r.view.source_owner==p.source_owner&&r.view.class_index==p.class_index&&
  r.view.declarations==p.declarations&&r.view.old_root==p.source_old_root&&s.Get(r.view.old_root)==p.old_class&&s.Get(r.view.cache_root)==p.cache&&s.Get(r.view.parent_root)==p.parent&&
  p.new_class&&r.phase==Batch::State::Phase::Captured&&r.captured&&s.Get(r.view.new_root)==p.new_class)return true;
 return false;
}
bool Service::VerifyAllocation(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&p,std::string*)const{
 auto* current=Current();if(!current||!observed_||!monitor_||p.new_class)return false;auto& s=*current->state_;
 for(const auto&r:s.records)if(r.phase==Batch::State::Phase::Unallocated&&r.view.source_owner==p.source_owner&&r.view.class_index==p.class_index&&r.view.declarations==p.declarations&&r.view.old_root==p.source_old_root&&s.Get(r.view.old_root)==p.old_class&&s.Get(r.view.cache_root)==p.cache&&s.Get(r.view.parent_root)==p.parent&&!s.Get(r.view.new_root))return true;
 return false;
}
bool Service::RefreshPending(native_hierarchy_graph::Prepared*p)const noexcept{
 auto*current=Current();if(!current||!p)return false;auto&s=*current->state_;if(s.failed||s.finished)return false;
 for(const auto&r:s.records)if(r.phase==Batch::State::Phase::Captured&&r.captured&&r.view.source_owner==p->source_owner&&r.view.class_index==p->class_index&&r.view.old_root==p->source_old_root&&r.view.declarations==p->declarations){p->old_class=s.Get(r.view.old_root);p->new_class=s.Get(r.view.new_root);p->cache=s.Get(r.view.cache_root);p->parent=s.Get(r.view.parent_root);return p->old_class&&p->new_class&&p->cache;}
 return false;
}
bool Service::RefreshAllocations(native_hierarchy_graph::Graph* graph,std::vector<native_hierarchy_graph::Prepared>* values)const noexcept{
 if(!graph||!values)return false;auto* current=Current();
 if(!current)return values->empty()&&originals_&&observed_&&monitor_&&roots_->ReadyForSourceAccess()&&std::all_of(graph->nodes.begin(),graph->nodes.end(),[](const auto&n){return !n.replacement;});
 auto& s=*current->state_;if(s.failed||s.finished)return false;
 for(auto& value:*values){bool found=false;for(const auto& r:s.records)if(r.view.source_owner==value.source_owner&&r.view.class_index==value.class_index&&r.view.old_root==value.source_old_root&&value.declarations&&SameSchema(*r.view.declarations,*value.declarations)){
  if(r.node>=graph->nodes.size()||graph->nodes[r.node].klass!=s.Get(r.view.old_root)||!r.captured||r.phase!=Batch::State::Phase::Linked)return false;
  value.old_class=s.Get(r.view.old_root);value.new_class=s.Get(r.view.new_root);value.cache=s.Get(r.view.cache_root);value.parent=s.Get(r.view.parent_root);
  graph->nodes[r.node].replacement=value.new_class;found=true;break;
 }
 if(!found)return false;
 }
 return true;
}
bool Service::Verify(const native_hierarchy_graph::Graph& graph,const source_commit::Plan& plan)const noexcept{
 auto* current=Current();if(!current||!observed_||!monitor_)return false;auto& s=*current->state_;if(s.failed||s.finished||plan.old_classes.size()!=s.records.size()||plan.old_classes.size()!=plan.new_classes.size())return false;
 for(const auto& r:s.records){if(r.phase!=Batch::State::Phase::Linked||!r.captured_allocated||r.node>=graph.nodes.size()||graph.nodes[r.node].klass!=s.Get(r.view.old_root)||graph.nodes[r.node].replacement!=s.Get(r.view.new_root))return false;
  if(s.Get(r.allocated_root)!=s.Get(r.view.new_root)){class_metadata::State retired{};const auto& layout=Layout(*metadata_);if(!class_metadata::ReadState(layout,s.Get(r.allocated_root),layout.size,&retired)||retired.status!=1)return false;}
  bool found=false;for(size_t n=0;n<plan.old_classes.size();++n)if(plan.old_classes[n]==s.Get(r.view.old_root)&&plan.new_classes[n]==s.Get(r.view.new_root)){found=true;break;}if(!found)return false;
 }return true;
}
void Service::BeginCommit(const native_hierarchy_graph::Graph& graph,const source_commit::Plan& plan)const noexcept{if(!Verify(graph,plan))std::abort();Current()->state_->committing=true;}
}
