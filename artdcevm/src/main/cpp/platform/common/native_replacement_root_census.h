#pragma once
#include "platform/common/native_typed_heap_walk.h"
#include "platform/common/object_replacement_map.h"
namespace artdcevm::native_replacement_root_census {
// Affected strong roots from the original complete AllRoots stream only.
// Counts are not writable slots, weak-root coverage or native no-escape proof.
struct Report {
 native_typed_heap_walk::RootReport roots{};
 size_t affected_roots=0,pointer_roots=0,compressed_roots=0,frame_roots=0;
 bool completed=false;
};
inline Report Observe(const object_replacement::ObjectReplacementMap&map,
 const native_typed_heap_walk::RootProvenance&source,const native_source_stop::Session&stop,
 void*self,const modern_field_metadata::ReadContext&read)noexcept{
 Report report;
 if(!stop.IsStopped(self)||!object_replacement::VerifyObjectReplacementMap(map,nullptr))return report;
 struct Sink final:native_typed_heap_walk::RootSink {
  const object_replacement::ObjectReplacementMap&map;Report&report;
  const modern_field_metadata::ReadContext&read;
  Sink(const object_replacement::ObjectReplacementMap&m,Report&r,
       const modern_field_metadata::ReadContext&c):map(m),report(r),read(c){}
  bool Root(const native_typed_heap_walk::RootRecord&root)noexcept override{
   void*replacement=map.Replacement(root.value);if(!replacement)return true;
   // Destination identities must still have a readable object prefix in the
   // same GC-disabled mapping window. Complete plan/schema checks are separate.
   size_t extent=0;
   if(!root.slot||!read.owner||!read.extent||
      !read.extent(read.owner,replacement,&extent)||extent<sizeof(uint32_t))return false;
   ++report.affected_roots;
   if(root.representation==native_typed_heap_walk::RootRepresentation::Pointer)++report.pointer_roots;
   else if(root.representation==native_typed_heap_walk::RootRepresentation::Compressed)++report.compressed_roots;
   else return false;
   if(root.type==abi::arm64::runtime_root_visit::kJavaFrameRoot){
    if(!root.original_frame_visitor)return false;++report.frame_roots;
   }
   return true;
  }
 }sink(map,report,read);
 report.roots=source.ObserveFrameRoots(stop,self,read,sink);
 report.completed=report.roots.completed&&stop.IsStopped(self)&&
   report.affected_roots==report.pointer_roots+report.compressed_roots&&
   object_replacement::VerifyObjectReplacementMap(map,nullptr);
 return report;
}
}
