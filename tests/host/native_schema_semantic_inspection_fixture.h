#pragma once
#include "platform/common/native_cache_semantic.h"
namespace art {
class ReflectionSourceInfo {public:virtual ~ReflectionSourceInfo()=default;virtual void Describe(std::ostream& out)const{out<<kind_;}explicit ReflectionSourceInfo(uint32_t k):kind_(k){}private:uint32_t kind_;};
class DexCacheSourceInfo final:public ReflectionSourceInfo {public:DexCacheSourceInfo(uint32_t k,size_t i,void* c):ReflectionSourceInfo(k),index_(i),cache_(c){}void Describe(std::ostream& out)const override{ReflectionSourceInfo::Describe(out);out<<index_<<cache_;}private:size_t index_;void* cache_;};
}
static unsigned semantic_visits=0;
static void* semantic_native_field=nullptr;
static void* semantic_native_method=nullptr;
static uint32_t semantic_type_index=0,semantic_type_count=0;
static bool SemanticMappings(void* raw,std::string*) {
 auto* run=artdcevm::native_schema_run_state::RunState::Current();
 assert(run&&run->OwnsMapping(raw)&&!run->OwnsMapping(&semantic_visits)&&run->StoppedWindow());
 assert(Read32(thread.data())==74u<<24&&gc&&stopped==1&&!waiting_locked);
 if(!std::strcmp(waiting_case,"throw"))throw std::bad_alloc();
 if(std::strcmp(waiting_case,"valid"))return false;
 return RunStateMappings::Refresh(raw,nullptr);
}
static void SemanticVisit(void* cache,art::ReflectiveValueVisitor* visitor){
 assert(gc&&stopped==1&&Read32(thread.data())==74u<<24&&cache==published_cache);
 auto* run=artdcevm::native_schema_run_state::RunState::Current();assert(run&&run->CurrentReadContext());
 const auto queries=artdcevm::method_identity::MappingOsQueries();++semantic_visits;
 art::DexCacheSourceInfo field(9,0,cache),method(8,0,cache);
 assert(visitor->VisitField(reinterpret_cast<art::ArtField*>(semantic_native_field),field)==reinterpret_cast<art::ArtField*>(semantic_native_field));
 assert(visitor->VisitMethod(reinterpret_cast<art::ArtMethod*>(semantic_native_method),method)==reinterpret_cast<art::ArtMethod*>(semantic_native_method));
 assert(artdcevm::method_identity::MappingOsQueries()==queries);
}
static void* SemanticType(void* cache,uint32_t index){assert(cache==published_cache&&index==semantic_type_index&&gc&&stopped);return old_class;}
static void* SemanticMethod(void* cache,uint32_t index){assert(cache==published_cache&&index==0&&gc&&stopped);return semantic_native_method;}
struct SemanticTypes final:artdcevm::native_cache_semantic::TypeReader {
 bool Read(const artdcevm::native_cache_semantic::Service&,void* cache,uint32_t n,artdcevm::type_cache::View* out,const artdcevm::modern_field_metadata::ReadContext* context)const noexcept override{
  if(cache!=published_cache||n!=semantic_type_count||!out||!context||!context->owner||!context->extent||!gc||!stopped)return false;
  size_t extent=0;if(!context->extent(context->owner,cache,&extent))return false;
  return artdcevm::type_cache::Read(artdcevm::dex_cache_metadata::Model::DualArrays,cache,extent,n,out);
 }
};
namespace artdcevm::native_cache_semantic {
// Only the opaque original type-storage boundary is supplied independently.
// Calibrated metadata/storage objects, Service::Calibrate, original visitor,
// RunState controller, source-stop proof and stopped Pool reads stay actual.
struct HostFixture {static void Bind(Service& service,const modern_field_metadata::FieldMetadata& metadata,const modern_field_storage::Service& storage){static const SemanticTypes reader;service.leaves_={SemanticVisit,SemanticType,SemanticMethod};service.metadata_=&metadata;service.storage_=&storage;service.reader_=&reader;}};
}
