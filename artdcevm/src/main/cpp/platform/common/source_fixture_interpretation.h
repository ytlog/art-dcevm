#pragma once
#include "platform/common/source_fixture_roots.h"
#include "platform/common/native_selective_interpretation.h"
#include "platform/common/readable_mapping_scope.h"
namespace artdcevm::source_fixture_interpretation {
// One scoped inspection path for unchanged, independently rooted calibration
// fixtures. No root, mapping, stop or publication permission survives Inspect.
inline bool Inspect(const native_source_stop::Service&service,const runnable_jni::Context&actual,
    source_fixture_roots::Owner&roots,void*owner,
    bool(*operation)(void*,native_source_stop::Session&,const modern_field_metadata::ReadContext&)noexcept,
    std::string*error){
 size_t extent=0;
 if(!operation||!runnable_jni::Bridge::BorrowOriginalScope(actual)||
    !method_identity::ReadableMappingExtent(actual.self,&extent))return false;
 struct Request {source_fixture_roots::Owner&roots;void*owner;decltype(operation)run;bool completed=false;};
 Request request{roots,owner,operation};native_source_stop::Session stop(service);
 const bool inspected=stop.Inspect(actual.self,extent,&request,[](void*raw,native_source_stop::Session&stop)noexcept{
  auto&r=*static_cast<Request*>(raw);source_fixture_roots::Owner::Window window(r.roots,stop);
  method_identity::ScopedReadableMappings mappings;if(!mappings.Ready())return false;
  const modern_field_metadata::ReadContext read{&mappings,method_identity::ScopedReadableMappings::ReadExtent};
  r.completed=r.run(r.owner,stop,read);return r.completed;
 });
 if(stop.Reserved()||stop.IsStopped(actual.self))std::abort();
 if(!inspected||!request.completed){if(error)*error="fixture-actual-stopped-inspection-refused";return false;}
 return runnable_jni::Bridge::BorrowOriginalScope(actual);
}
inline bool Activate(const native_source_stop::Service&stop,const runnable_jni::Context&actual,
    source_fixture_roots::Owner&roots,native_selective_interpretation::Service&journal,std::string*error){
 struct Request {native_selective_interpretation::Service&journal;void*self;const char*failure=nullptr;};
 Request request{journal,actual.self};
 const bool activated=Inspect(stop,actual,roots,&request,[](void*raw,native_source_stop::Session&stop,
     const modern_field_metadata::ReadContext&read)noexcept{
  auto&r=*static_cast<Request*>(raw);native_selective_interpretation::Report report;
  const bool completed=r.journal.Activate(stop,r.self,read,&report);r.failure=report.failure;return completed;
 },error);
 if(!activated&&error&&request.failure)*error=request.failure;return activated;
}
// Never unwind past an owned native-set entry, including partial activation.
// Original Java has returned; these fixtures discard no Class/method metadata.
inline bool Refund(const native_source_stop::Service&stop,const runnable_jni::Context&actual,
    source_fixture_roots::Owner&roots,native_selective_interpretation::Service&journal,std::string*error)noexcept{
 struct Request {native_selective_interpretation::Service&journal;void*self;const char*failure=nullptr;};
 Request request{journal,actual.self};bool refunded=false;
 try{
  refunded=Inspect(stop,actual,roots,&request,[](void*raw,native_source_stop::Session&stop,
      const modern_field_metadata::ReadContext&read)noexcept{
   auto&r=*static_cast<Request*>(raw);native_selective_interpretation::RefundReport report;
   const bool completed=r.journal.Refund(stop,r.self,read,&report)&&report.complete&&!report.pending;
   r.failure=report.failure;return completed;
  },error);
  if(!refunded&&error&&request.failure)*error=request.failure;
 }catch(...){if(journal.PendingRefund())std::abort();return false;}
 if(journal.PendingRefund())std::abort();return refunded;
}
}
