#include "platform/common/native_cache_semantic.h"
#include "platform/common/native_hierarchy_cache.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <cstdlib>
#include <new>
thread_local bool exclusive_window=false;
thread_local unsigned window_epoch=0;
void* operator new(std::size_t n){assert(!exclusive_window);if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
namespace art {
// Fixed reflective_value_visitor.h source declarations, not an invented token.
class ReflectionSourceInfo {
 public:virtual ~ReflectionSourceInfo()=default;virtual void Describe(std::ostream& out)const{out<<type_;}
 explicit ReflectionSourceInfo(uint32_t value):type_(value){}
 private:const uint32_t type_;
};
class DexCacheSourceInfo final:public ReflectionSourceInfo {
 public:DexCacheSourceInfo(uint32_t type,size_t index,void* cache):ReflectionSourceInfo(type),index_(index),cache_(cache){}
 public:void Describe(std::ostream& out)const override{ReflectionSourceInfo::Describe(out);out<<index_<<cache_;}
 private:size_t index_;void* cache_;
};
}
namespace {
using namespace artdcevm;
struct Entry {uint32_t index;void* value;};
struct Fixture {
 // Opaque actual cache object is never interpreted as a DexCache layout.
 std::array<type_cache::Pair,1024> types{};
 std::array<uint32_t,4> full{};
 std::array<Entry,4> fields{},methods{};
 uintptr_t string=0x1133,callsite=0x2255;
 void* expected_class=reinterpret_cast<void*>(0x12345000);
 void* expected_field=reinterpret_cast<void*>(uintptr_t{0x113456789});
 void* expected_method=reinterpret_cast<void*>(uintptr_t{0x223456789});
 uint32_t bad_kind=0;bool wrong_owner=false,wrong_index=false,wrong_type=false,wrong_method=false;
 unsigned callbacks=0,writes=0,barriers=0,last_write_epoch=0;bool require_exclusive=false;
 Fixture(bool fields_present){
  types[1]={uint32_t(uintptr_t(expected_class)),1};full[1]=uint32_t(uintptr_t(expected_class));
  if(fields_present)fields[0]={2,expected_field};
  methods[0]={3,expected_method};methods[1]={3,expected_method};
 }
};
void OriginalVisit(void* cache,art::ReflectiveValueVisitor* visitor){
 auto& f=*static_cast<Fixture*>(cache);assert(!f.require_exclusive||exclusive_window);bool wrote=false;
 auto apply=[&](auto& entries,uint32_t kind){for(auto& e:entries)if(e.value){
  art::DexCacheSourceInfo info(f.bad_kind?f.bad_kind:kind,f.wrong_index?4:e.index,f.wrong_owner?&f.callsite:cache);
  ++f.callbacks;void* next=kind==8?reinterpret_cast<void*>(visitor->VisitMethod(static_cast<art::ArtMethod*>(e.value),info)):
   reinterpret_cast<void*>(visitor->VisitField(static_cast<art::ArtField*>(e.value),info));
  if(next!=e.value){e.value=next;++f.writes;wrote=true;f.last_write_epoch=window_epoch;}
 }};
 apply(f.fields,9);apply(f.methods,8);if(wrote)++f.barriers;
}
void* OriginalType(void* cache,uint32_t i){auto& f=*static_cast<Fixture*>(cache);return i==1&&!f.wrong_type?f.expected_class:nullptr;}
void* OriginalMethod(void* cache,uint32_t i){auto& f=*static_cast<Fixture*>(cache);return i==3&&!f.wrong_method?f.expected_method:nullptr;}
struct SourceTypes final:native_cache_semantic::TypeReader {
bool Read(const native_cache_semantic::Service&,void* cache,uint32_t n,type_cache::View* out,const modern_field_metadata::ReadContext*)const noexcept override{
 if(!cache||!out||n!=4)return false;auto& f=*static_cast<Fixture*>(cache);
 *out={f.types.data(),1024,f.full.data(),4};return true;
}};
}
namespace artdcevm::native_cache_semantic {
struct HostFixture {
 static void Bind(Service* s){
  static modern_field_metadata::FieldMetadata source_metadata;
  static modern_field_storage::Service source_storage;
  s->leaves_={OriginalVisit,OriginalType,OriginalMethod};s->metadata_=&source_metadata;s->storage_=&source_storage;static const SourceTypes reader;s->reader_=&reader;
 }
};
}
namespace {
void Test(bool fields_present){
 Fixture f(fields_present);native_cache_semantic::Service service;native_cache_semantic::HostFixture::Bind(&service);std::string error;
 const auto cal=[&](bool resolved=true){return service.Calibrate(&f,4,4,4,1,f.expected_class,2,f.expected_field,3,f.expected_method,resolved,nullptr,&error);};
 assert(!service.Readonly(&f,4,4)&&!service.Clear(&f,4,4));assert(f.writes==0);
 assert(!cal(false)&&!service.Calibrated());
 f.wrong_type=true;assert(!cal());f.wrong_type=false;
 f.wrong_method=true;assert(!cal());f.wrong_method=false;
 f.bad_kind=7;assert(!cal());f.bad_kind=0;
 f.wrong_owner=true;assert(!cal());f.wrong_owner=false;
 f.wrong_index=true;assert(!cal());f.wrong_index=false;
 f.full[1]=0;assert(!cal());f.full[1]=uint32_t(uintptr_t(f.expected_class));
 // Fixed DEFINE_DUAL_CACHE reads the full array first, even if an old pair exists.
 f.types[1]={uint32_t(uintptr_t(f.expected_class)),2};assert(cal());f.types[1].index=1;
 assert(f.writes==0&&f.barriers==0&&cal()&&service.Calibrated());
 assert(service.CheckWitness(&f,4,4,2,f.expected_field,3,f.expected_method));
 assert(!service.CheckWitness(&f,4,4,2,f.expected_field,3,reinterpret_cast<void*>(0x123)));
 assert(service.Readonly(&f,4,4));f.wrong_owner=true;assert(!service.Clear(&f,4,4));f.wrong_owner=false;
 assert(f.writes==0&&service.Clear(&f,4,4)&&service.Cleared(&f,4,4));
 assert(f.writes==(fields_present?3u:2u)&&f.barriers==1);
 assert(f.types[1].object==uint32_t(uintptr_t(f.expected_class))&&f.full[1]==uint32_t(uintptr_t(f.expected_class))&&f.string==0x1133&&f.callsite==0x2255);
 assert(!service.CheckWitness(&f,4,4,2,f.expected_field,3,f.expected_method));
 f.methods[0]={3,f.expected_method};assert(!service.Cleared(&f,4,4));
 assert(service.Clear(&f,4,4)&&service.Cleared(&f,4,4));
}
class Mapper final:public native_hierarchy_cache::SourceMapper {
 public:
 Fixture calibration{false},incoming{true},moved{true};
 void* cal_root=&calibration;void* incoming_root=&incoming;
 void* old_class=calibration.expected_class;void* replacement=reinterpret_cast<void*>(0x23456000);
 native_cache_semantic::Service service;
 native_hierarchy_cache::Source source{&incoming_root,this,&incoming,0,{4,4,4}};
 native_hierarchy_graph::Prepared prepared{};
 bool private_window=true,frozen=true,readable=true,change_during_lookup=false,deny_inspection=false,move_during_lookup=false,change_occupancy=false,deny_barrier=false;mutable unsigned inspections=0;unsigned barriers=0;
 Mapper(){calibration.require_exclusive=incoming.require_exclusive=moved.require_exclusive=true;native_cache_semantic::HostFixture::Bind(&service);prepared.source_owner=this;prepared.old_class=old_class;prepared.new_class=replacement;prepared.cache=incoming_root;static evolution::ClassSchema declaration;prepared.declarations=&declaration;}
 void* Get(native_hierarchy_cache::Root root)const noexcept override{return root?*static_cast<void**>(root):nullptr;}
 bool Readable(const void* p,size_t n)const noexcept override{return readable&&p&&n<=sizeof(Fixture);}
 native_cache_semantic::Service* Semantic()const noexcept override{return const_cast<native_cache_semantic::Service*>(&service);}
 bool InspectSemantic(void* payload,native_hierarchy_cache::SemanticOperation operation)const noexcept override{
  if(deny_inspection||!operation)return false;
  if(exclusive_window)return operation(payload,*this);
  ++inspections;++window_epoch;exclusive_window=true;const bool result=operation(payload,*this);exclusive_window=false;return result;
 }
 bool VerifyStaged(const native_hierarchy_graph::Graph&,const std::vector<native_hierarchy_graph::Input>&,std::string*)const override{return true;}
 bool Describe(const native_hierarchy_graph::Prepared&,native_hierarchy_cache::Source* out,std::string*)const override{*out=source;return true;}
 bool DescribeAllocation(const native_hierarchy_graph::Prepared&,native_hierarchy_cache::Source* out,std::string*)const override{*out=source;return true;}
 bool RefreshAllocation(native_hierarchy_graph::Prepared* p)const noexcept override{p->cache=incoming_root;return true;}
 bool FindSource(const native_hierarchy_graph::Prepared&,uint32_t i,native_hierarchy_cache::Root* out,std::string*)const override{
  assert(!exclusive_window);if(i!=1)return false;auto& self=*const_cast<Mapper*>(this);auto& current=*static_cast<Fixture*>(incoming_root);
  assert(current.writes==0&&current.types[1].object==uintptr_t(old_class)&&current.full[1]==uintptr_t(old_class));
  if(change_during_lookup)current.wrong_owner=true;
  if(change_occupancy)current.full[2]=uint32_t(uintptr_t(old_class));
  if(move_during_lookup&&self.incoming_root==&self.incoming){self.moved=self.incoming;self.incoming_root=&self.moved;self.incoming.types={};self.incoming.full={};}
  *out=const_cast<void**>(&replacement);return true;
 }
 bool OwnsUnpublished(const native_hierarchy_graph::Prepared&p,const native_hierarchy_cache::Source&s)const noexcept override{return exclusive_window&&private_window&&p.source_owner==this&&s.private_token==&incoming&&s.cache_root==&incoming_root;}
 bool Barrier(native_hierarchy_cache::Root root)const noexcept override{
  assert(exclusive_window);auto& actual=*static_cast<Fixture*>(Get(root));
  assert(actual.last_write_epoch==window_epoch&&actual.types[1].object==uintptr_t(replacement)&&actual.full[1]==uintptr_t(replacement));
  ++const_cast<Mapper*>(this)->barriers;return !deny_barrier;
 }
 bool RefreshPrepared(native_hierarchy_graph::Prepared* p)const noexcept override{p->cache=incoming_root;p->new_class=replacement;return true;}
 bool VerifyFrozen(const native_hierarchy_graph::Graph&,const native_hierarchy_graph::Prepared&,const native_hierarchy_cache::Source&)const noexcept override{return frozen;}
 bool VerifyCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override{return frozen;}
 void BeginCommit(const native_hierarchy_graph::Graph&,const source_commit::Plan&)const noexcept override{}
 native_hierarchy_cache::Witness Witness(){native_hierarchy_cache::Witness w{1,2,3,&old_class,calibration.expected_field,calibration.expected_method};w.has_public_storage=true;w.public_types=uintptr_t(calibration.types.data());w.public_types_array=uintptr_t(calibration.full.data());w.semantic_resolution_proved=true;return w;}
};
void OwnerTest(){
 Mapper mapper;native_hierarchy_cache::Owner owner;std::string error;auto witness=mapper.Witness();
 witness.semantic_resolution_proved=false;assert(!owner.Calibrate(mapper,&mapper.cal_root,0,{4,4,4},witness,&error));
 witness=mapper.Witness();assert(owner.Calibrate(mapper,&mapper.cal_root,0,{4,4,4},witness,&error));
 mapper.deny_inspection=true;assert(!owner.Prepare(mapper.prepared,&error));mapper.deny_inspection=false;
 mapper.private_window=false;assert(!owner.Prepare(mapper.prepared,&error));mapper.private_window=true;
 mapper.incoming.wrong_owner=true;assert(!owner.Prepare(mapper.prepared,&error));mapper.incoming.wrong_owner=false;
 mapper.change_during_lookup=true;assert(!owner.Prepare(mapper.prepared,&error));mapper.change_during_lookup=false;mapper.incoming.wrong_owner=false;
 assert(mapper.incoming.types[1].object==uintptr_t(mapper.old_class)&&mapper.incoming.full[1]==uintptr_t(mapper.old_class));
 assert(mapper.incoming.writes==0&&mapper.barriers==0);
 const auto before_write=mapper.inspections;
 assert(owner.Prepare(mapper.prepared,&error)&&mapper.barriers==1);assert(mapper.inspections==before_write+2);
 assert(mapper.incoming.types[1].object==uintptr_t(mapper.replacement)&&mapper.incoming.full[1]==uintptr_t(mapper.replacement));
 assert(mapper.incoming.string==0x1133&&mapper.incoming.callsite==0x2255&&mapper.incoming.writes==3);
 assert(owner.VerifyPrepared({},mapper.prepared,&error)&&owner.Verify({},{}));
 mapper.incoming.methods[0]={3,mapper.incoming.expected_method};assert(!owner.Verify({},{}));mapper.incoming.methods[0].value=nullptr;
 mapper.incoming.types[1].index=2;assert(!owner.Verify({},{}));mapper.incoming.types[1].index=1;
 mapper.frozen=false;assert(!owner.Verify({},{}));mapper.frozen=true;
 assert(owner.Refresh()&&owner.Verify({},{}));
 Mapper gc;native_hierarchy_cache::Owner moving;auto gcw=gc.Witness();
 assert(moving.Calibrate(gc,&gc.cal_root,0,{4,4,4},gcw,&error));
 auto pre=gc.prepared;pre.new_class=nullptr;pre.source_old_root=&gc.old_class;
 assert(moving.VerifyAllocation({},pre,&error));
 gc.move_during_lookup=true;assert(moving.Prepare(gc.prepared,&error));
 assert(gc.incoming_root==&gc.moved&&gc.moved.writes==3&&gc.barriers==1&&gc.incoming.writes==0);
 assert(gc.moved.types[1].object==uintptr_t(gc.replacement)&&gc.moved.full[1]==uintptr_t(gc.replacement));
 assert(gc.RefreshPrepared(&gc.prepared)&&moving.Refresh()&&moving.VerifyPrepared({},gc.prepared,&error));
 // A later actual moving GC changes only array location, not source/index roles.
 gc.incoming=gc.moved;gc.incoming_root=&gc.incoming;gc.moved.types={};gc.moved.full={};
 assert(moving.Refresh()&&moving.Verify({},{}));
 Mapper changed;native_hierarchy_cache::Owner refusing;assert(refusing.Calibrate(changed,&changed.cal_root,0,{4,4,4},changed.Witness(),&error));
 changed.change_occupancy=true;assert(!refusing.Prepare(changed.prepared,&error));assert(changed.incoming.writes==0&&changed.barriers==0);
 Mapper missing;missing.deny_inspection=true;native_hierarchy_cache::Owner uncalibrated;
 assert(!uncalibrated.Calibrate(missing,&missing.cal_root,0,{4,4,4},missing.Witness(),&error)&&!missing.service.Calibrated());
 Mapper barrier;native_hierarchy_cache::Owner aborted;assert(aborted.Calibrate(barrier,&barrier.cal_root,0,{4,4,4},barrier.Witness(),&error));
 barrier.deny_barrier=true;assert(!aborted.Prepare(barrier.prepared,&error));assert(barrier.incoming.writes==3&&!aborted.Verify({},{}));
 assert(!exclusive_window);

}

}
int main(){Test(false);Test(true);OwnerTest();std::cout<<"semantic original cache visitor ABI/oracles/refusal/zero-field/native-writer/readback PASS\n";}
