#include "platform/common/native_cache_semantic.h"
#include "platform/common/field_storage.h"
#include <cstring>
#include <array>
namespace artdcevm::native_cache_semantic {
namespace {
bool Fail(std::string* e,const char* s){if(e)*e=s;return false;}
bool Span(const modern_field_metadata::ReadContext* context,const void* p,size_t count,size_t size){
  size_t n=0;return !count||(p&&count<=SIZE_MAX/size&&
      modern_field_metadata::ReadMappingExtent(context,p,&n)&&count*size<=n);
}
}
struct StorageReader final:TypeReader {
 bool Read(const Service&,void*,uint32_t,type_cache::View*,const modern_field_metadata::ReadContext*)const noexcept override;
};
bool Service::PrepareStorage(const modern_field_metadata::FieldMetadata& metadata,
 const modern_field_storage::Service& storage,void* cache,void* owner,void* types,void* full,
 uint64_t public_types,uint64_t public_full,const modern_field_metadata::ReadContext* context,std::string* e){
 if((StoragePrepared()&&(metadata_!=&metadata||storage_!=&storage||fields_[0]!=types||fields_[1]!=full))||!leaves_.visit||!leaves_.type||!leaves_.method||!metadata.Calibrated()||!storage.Calibrated()||
    !cache||!owner||!types||!full||types==full)
  return Fail(e,"Semantic cache instance/source field owner preparation rejected");
 size_t prefix=0;
 if(!modern_field_metadata::ReadMappingExtent(context,cache,&prefix)||prefix<8||metadata.Reference(cache,0)!=owner)return Fail(e,"Semantic cache independent public Class differs");
 const char* names[]={"resolvedTypes","resolvedTypesArray"};void* fields[]={types,full};uint64_t values[]={public_types,public_full};
 for(size_t i=0;i<2;++i){
  field_copy::CopyFieldView text;size_t extent=0;
  if(metadata.FieldDeclaringClass(fields[i])!=owner||!metadata.FieldIdentity(fields[i],&text,e,context)||
     !text.name||std::strcmp(text.name,names[i])||!text.descriptor||std::strcmp(text.descriptor,"J")||
     !modern_field_metadata::ReadMappingExtent(context,fields[i],&extent)||extent<sizeof(abi::arm64::Field))
   return Fail(e,"Semantic cache exact public/native instance-long declaration differs");
  const auto& f=*static_cast<const abi::arm64::Field*>(fields[i]);
  if((f.access_flags&8u)||f.offset%8||!modern_field_metadata::ReadMappingExtent(context,cache,&extent)||
      f.offset>extent||sizeof(uint64_t)>extent-f.offset||
      field_storage::Read(cache,f,'J',nullptr)!=values[i])
   return Fail(e,"Semantic cache source long read differs from original public GetLongField");
 }
 metadata_=&metadata;storage_=&storage;fields_[0]=types;fields_[1]=full;static const StorageReader reader;reader_=&reader;return true;
}
bool StorageReader::Read(const Service& service,void* cache,uint32_t count,type_cache::View* out,const modern_field_metadata::ReadContext* context)const noexcept{
 const auto* metadata_=service.metadata_;const auto* storage_=service.storage_;const auto* fields_=service.fields_;
 if(!service.StoragePrepared()||!cache||!out||!metadata_->Calibrated()||!storage_->Calibrated())return false;
 size_t prefix=0;
 if(!modern_field_metadata::ReadMappingExtent(context,cache,&prefix)||prefix<8)return false;
 for(auto* f:std::array<void*,2>{fields_[0],fields_[1]})if(!modern_field_metadata::ReadMappingExtent(context,f,&prefix)||prefix<sizeof(abi::arm64::Field))return false;
 void* owner=metadata_->FieldDeclaringClass(fields_[0]);
 if(!owner||metadata_->FieldDeclaringClass(fields_[1])!=owner||metadata_->Reference(cache,0)!=owner)return false;
 uint64_t values[2]{};
 for(size_t i=0;i<2;++i){size_t extent=0;
  if(!modern_field_metadata::ReadMappingExtent(context,fields_[i],&extent)||extent<sizeof(abi::arm64::Field))return false;
  const auto& f=*static_cast<const abi::arm64::Field*>(fields_[i]);
  if((f.access_flags&8u)||f.offset%8||!modern_field_metadata::ReadMappingExtent(context,cache,&extent)||
     f.offset>extent||sizeof(uint64_t)>extent-f.offset)return false;
  values[i]=field_storage::Read(cache,f,'J',nullptr);
 }
 type_cache::View result{reinterpret_cast<type_cache::Pair*>(uintptr_t(values[0])),values[0]?1024u:0u,
     reinterpret_cast<uint32_t*>(uintptr_t(values[1])),values[1]?count:0u};
 if((values[0]&&values[0]%8)||(values[1]&&values[1]%4)||
     !Span(context,result.pairs,result.pair_count,sizeof(type_cache::Pair))||
     !Span(context,result.full,result.full_count,sizeof(uint32_t)))return false;
 *out=result;return true;
}
}
