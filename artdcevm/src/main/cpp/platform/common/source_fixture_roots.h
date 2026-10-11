#pragma once
#include "platform/common/source_handle_roots.h"
#include "platform/common/bounded_managed_roots.h"
#include "platform/common/native_source_stop.h"
#include <cstdlib>
#include <utility>
namespace artdcevm::source_fixture_roots {
// Shared bootstrap cohort, distinct from durable opaque membership. Owns one
// native HandleScope anchor in its actual original Runnable callback; only an
// actual Session Window authorizes stopped reads. Destroy after stop resume.
class Owner final:private source_handle_roots::StoppedReader {
 public:
 Owner(const runnable_jni::Context&context,const source_handle_roots::Service&service,
              const modern_field_metadata::FieldMetadata&metadata)
  :context_(context),service_(service),metadata_(metadata),pool_(service){
  roots_.Bind({this,nullptr,Oracle,Decode,Element});
 }
 ~Owner(){if(stop_)std::abort();roots_.Clear(context_.env);}
 Owner(const Owner&)=delete;Owner&operator=(const Owner&)=delete;
 bool Stage(const std::vector<jobject>&objects,std::vector<bounded_managed_roots::Slot>*out,std::string*error){
  if(!out||!service_.Calibrated()||!runnable_jni::Bridge::BorrowOriginalScope(context_)||
     !pool_.ReserveAnchor(error))return false;
  std::vector<bounded_managed_roots::Slot>slots;
  if(!roots_.StageMany(context_,objects,&slots,error)||slots.size()!=objects.size()||
     !roots_.RetainArrays({this,Retain,Read,Release,Staging}))return false;
  *out=std::move(slots);return true;
 }
 const bounded_managed_roots::Store&Store()const noexcept{return roots_;}
 class Window final {
  public:
  Window(Owner&owner,const native_source_stop::Session&stop):owner_(owner){
   if(owner_.stop_||!stop.IsStopped(owner_.context_.self))std::abort();owner_.stop_=&stop;
  }
  ~Window(){owner_.stop_=nullptr;}
  Window(const Window&)=delete;Window&operator=(const Window&)=delete;
  private:Owner&owner_;
 };
 private:
 bool FinalRootWindow()const noexcept override{return stop_&&stop_->IsStopped(context_.self);}
 bool ReadStoppedRoot(const void*slot,void**out)const noexcept override{return pool_.ReadStopped(*this,slot,out);}
 static bool Oracle(void*raw,const runnable_jni::Context&context,jobject root,void**out){
  auto&owner=*static_cast<Owner*>(raw);
  return &context==&owner.context_&&runnable_jni::Bridge::BorrowOriginalScope(context)&&
   owner.service_.BorrowOwnedStrongGlobal(context,root,out);
 }
 static bool Decode(void*raw,jobject root,void**out){
  auto&owner=*static_cast<Owner*>(raw);
  return !owner.stop_&&owner.service_.BorrowOwnedStrongGlobal(owner.context_,root,out);
 }
 static void*Element(void*raw,void*array,size_t index){
  auto&owner=*static_cast<Owner*>(raw);
  return index<size_t(bounded_managed_roots::Store::kChunkSize)?owner.metadata_.Reference(array,12+4*index):nullptr;
 }
 static void*Retain(void*raw,void*array){return static_cast<Owner*>(raw)->pool_.Add(array,nullptr);}
 static bool Read(void*raw,const void*slot,void**out)noexcept{
  auto&owner=*static_cast<Owner*>(raw);
  return owner.stop_?owner.ReadStoppedRoot(slot,out):owner.pool_.Read(slot,out);
 }
 static void Release(void*,void*)noexcept{} // Pool owns all cells through teardown.
 static bool Staging(void*raw)noexcept{
  auto&owner=*static_cast<Owner*>(raw);return !owner.stop_&&owner.pool_.ReadyForSourceAccess();
 }
 const runnable_jni::Context&context_;const source_handle_roots::Service&service_;
 const modern_field_metadata::FieldMetadata&metadata_;
 source_handle_roots::Pool pool_;bounded_managed_roots::Store roots_;
 const native_source_stop::Session*stop_=nullptr;
};
}
