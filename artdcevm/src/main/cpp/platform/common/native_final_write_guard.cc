#include "platform/common/native_final_write_guard.h"
namespace artdcevm::final_write_guard {
bool Guard::BeginProof(const Oracle& oracle) noexcept {
 if(!oracle.thread||!oracle.method||!oracle.field||!access_.owner||
    !access_.field||!access_.method_owner||!access_.equivalent_owner||!access_.raise)return false;
 uint32_t flags=0;void *field_owner=nullptr,*method_owner=nullptr;
 if(!access_.field(access_.owner,oracle.field,&flags,&field_owner)||!(flags&0x10u)||
    !access_.method_owner(access_.owner,oracle.method,&method_owner)||!field_owner||!method_owner||
    access_.equivalent_owner(access_.owner,field_owner,method_owner))return false;
 std::lock_guard<std::mutex> lock(lock_);if(probing_)return false;
 ++epoch_;oracle_=oracle;probing_=true;seen_=false;rejected_=false;return true;
}
bool Guard::EndProof(bool exception,bool unchanged) noexcept {
 std::lock_guard<std::mutex> lock(lock_);if(!probing_)return false;
 const bool pass=seen_&&rejected_&&exception&&unchanged;
 probing_=false;oracle_={};seen_=false;rejected_=false;
 // A failed new proof cannot authorize a previously unproven dispatcher.
 proven_=proven_||pass;return pass;
}
bool Guard::Proven() const noexcept {std::lock_guard<std::mutex> lock(lock_);return proven_;}
bool Guard::FieldWritten(void* thread,void* method,void* field) noexcept {
 if(!thread||!method||!field)return false;
 uint64_t epoch=0;
 {std::lock_guard<std::mutex> lock(lock_);
  if(probing_&&thread==oracle_.thread&&method==oracle_.method&&field==oracle_.field){epoch=epoch_;seen_=true;}}
 uint32_t flags=0;void *field_owner=nullptr,*method_owner=nullptr;
 const bool valid=access_.owner&&access_.field&&access_.method_owner&&access_.equivalent_owner&&
    access_.raise&&access_.field(access_.owner,field,&flags,&field_owner)&&field_owner&&
    access_.method_owner(access_.owner,method,&method_owner)&&method_owner;
 if(!valid){
  if(access_.raise){access_.raise(access_.owner,thread,"Ljava/lang/IllegalStateException;",
   "Final bytecode write lost calibrated field or method identity");}
  return access_.raise!=nullptr;
 }
 final_field_guard::Access access;access.path=final_field_guard::Path::Bytecode;
 access.field_flags=flags;
 access.same_declaring_owner=access_.equivalent_owner(access_.owner,field_owner,method_owner);
 if(final_field_guard::Check(access)==final_field_guard::Decision::Delegate)return false;
 access_.raise(access_.owner,thread,"Ljava/lang/IllegalAccessError;",
   "Write targets a final field outside its declaring owner");
 if(epoch){std::lock_guard<std::mutex> lock(lock_);if(probing_&&epoch_==epoch)rejected_=true;}
 return true;
}
} // namespace artdcevm::final_write_guard
