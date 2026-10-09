#include "platform/common/native_final_write_guard.h"
#include <cstdlib>
namespace artdcevm::final_write_guard {
bool Guard::BeginProof(const Oracle& oracle) noexcept {
 if(proof_committed_.load(std::memory_order_acquire))return false;
 if(!oracle.thread||!oracle.method||!oracle.field||!access_.owner||
    !access_.field||!access_.method_owner||(!access_.equivalent_owner&&!access_.equivalent_writer)||!access_.raise)return false;
 uint32_t flags=0;void *field_owner=nullptr,*method_owner=nullptr;
 if(!access_.field(access_.owner,oracle.field,&flags,&field_owner)||!(flags&0x10u)||
    !access_.method_owner(access_.owner,oracle.method,&method_owner)||!field_owner||!method_owner||
    (access_.equivalent_writer?access_.equivalent_writer(access_.owner,oracle.thread,oracle.field,oracle.method):access_.equivalent_owner(access_.owner,field_owner,method_owner)))return false;
 std::lock_guard<std::mutex> lock(lock_);if(probing_)return false;
 proof_ready_.store(false,std::memory_order_release);++epoch_;proof_epoch_.store(epoch_,std::memory_order_release);oracle_=oracle;probing_=true;seen_=false;rejected_=false;return true;
}
bool Guard::EndProof(bool exception,bool unchanged) noexcept {
 std::lock_guard<std::mutex> lock(lock_);if(!probing_)return false;
 const bool pass=seen_&&rejected_&&exception&&unchanged;
 probing_=false;oracle_={};seen_=false;rejected_=false;
 // A failed new proof cannot authorize a previously unproven dispatcher.
 proven_=proven_||pass;proof_ready_.store(pass,std::memory_order_release);return pass;
}
void Guard::ResetUncommittedProof() noexcept {if(proof_committed_.load(std::memory_order_acquire))std::abort();std::lock_guard<std::mutex> lock(lock_);proof_ready_.store(false,std::memory_order_release);++epoch_;proof_epoch_.store(epoch_,std::memory_order_release);oracle_={};probing_=false;seen_=false;rejected_=false;proven_=false;}
Guard::ProofWitness Guard::FreezeProof()const noexcept{std::lock_guard<std::mutex> lock(lock_);ProofWitness witness;if(proven_&&!probing_&&proof_ready_.load(std::memory_order_acquire)){witness.owner_=this;witness.epoch_=epoch_;}return witness;}
bool Guard::ValidateProof(const ProofWitness&witness)const noexcept{return witness.owner_==this&&witness.epoch_&&proof_ready_.load(std::memory_order_acquire)&&proof_epoch_.load(std::memory_order_acquire)==witness.epoch_;}
void Guard::CommitProof(const ProofWitness&witness)noexcept{if(!ValidateProof(witness))std::abort();proof_committed_.store(true,std::memory_order_release);}
bool Guard::Proven() const noexcept {std::lock_guard<std::mutex> lock(lock_);return proven_;}
bool Guard::FieldWritten(void* thread,void* method,void* field,bool reference) noexcept {
 if(!thread||!method||!field)return false;
 uint64_t epoch=0;
 {std::lock_guard<std::mutex> lock(lock_);
  if(probing_&&thread==oracle_.thread&&method==oracle_.method&&field==oracle_.field&&reference==oracle_.reference){epoch=epoch_;seen_=true;}
  if(!proven_&&!epoch)return false;}
 uint32_t flags=0;void *field_owner=nullptr,*method_owner=nullptr;
 const bool field_valid=access_.owner&&access_.field&&access_.raise&&
    access_.field(access_.owner,field,&flags,&field_owner)&&field_owner;
 if(field_valid&&!(flags&0x10u))return false;
 const bool valid=field_valid&&access_.method_owner&&(access_.equivalent_owner||access_.equivalent_writer)&&
    access_.method_owner(access_.owner,method,&method_owner)&&method_owner;
 if(!valid){
  if(access_.raise){access_.raise(access_.owner,thread,"Ljava/lang/IllegalStateException;",
   "Final bytecode write lost calibrated field or method identity");}
  return access_.raise!=nullptr;
 }
 final_field_guard::Access access;access.path=final_field_guard::Path::Bytecode;
 access.field_flags=flags;
 access.same_declaring_owner=access_.equivalent_writer?access_.equivalent_writer(access_.owner,thread,field,method):access_.equivalent_owner(access_.owner,field_owner,method_owner);
 if(access_.equivalent_writer){
  uint32_t fresh_flags=0;void* fresh_owner=nullptr;
  if(!access_.field(access_.owner,field,&fresh_flags,&fresh_owner)||!fresh_owner){
   access_.raise(access_.owner,thread,"Ljava/lang/IllegalStateException;","Final bytecode write lost field identity after weak-root suspension");return true;
  }
  access.field_flags=fresh_flags;
 }
 if(final_field_guard::Check(access)==final_field_guard::Decision::Delegate)return false;
 access_.raise(access_.owner,thread,"Ljava/lang/IllegalAccessError;",
   "Write targets a final field outside its declaring owner");
 if(epoch){std::lock_guard<std::mutex> lock(lock_);if(probing_&&epoch_==epoch)rejected_=true;}
 return true;
}
} // namespace artdcevm::final_write_guard
