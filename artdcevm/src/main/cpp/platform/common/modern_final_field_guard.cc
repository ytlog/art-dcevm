#include "platform/common/modern_final_field_guard.h"
#include <cstring>
namespace artdcevm::final_field_guard {
namespace { FinalFieldGuardState process_state; }
void MarkFinalFieldGuard() noexcept { process_state.Mark(); }
bool FinalFieldGuardEnabled() noexcept { return process_state.Enabled(); }

Decision Check(const Access& access) noexcept {
  if(access.path==Path::VarHandle &&
     (!access.var_mode_valid||access.var_mode_template>4))return Decision::Unavailable;
  if(!(access.field_flags&0x10u))return Decision::Delegate;
  switch(access.path){
    case Path::Jni:return Decision::Delegate;
    case Path::Reflection:return access.reflection_override?Decision::Delegate:Decision::IllegalAccessException;
    case Path::Bytecode:return access.same_declaring_owner?Decision::Delegate:Decision::IllegalAccessError;
    case Path::MethodHandleSetter:return Decision::IllegalAccessError;
    case Path::VarHandle:return access.var_mode_template==0?Decision::Delegate:Decision::UnsupportedOperationException;
  }
  return Decision::Unavailable;
}
bool Reject(const Boundary& boundary,void* field,void* self,const Access& input,void* result) noexcept {
  if(!boundary.owner||!boundary.read||!boundary.raise||!field||!self)return false;
  Access access=input;
  Decision decision=boundary.read(boundary.owner,field,&access.field_flags)?Check(access):Decision::Unavailable;
  if(decision==Decision::Delegate)return false;
  const char *type=nullptr,*text=nullptr;
  switch(decision){
    case Decision::IllegalAccessError:type="Ljava/lang/IllegalAccessError;";text="Write targets a final field outside its declaring owner";break;
    case Decision::IllegalAccessException:type="Ljava/lang/IllegalAccessException;";text="Reflective write targets a final field without an access override";break;
    case Decision::UnsupportedOperationException:type="Ljava/lang/UnsupportedOperationException;";text="Final field does not support VarHandle write access";break;
    default:type="Ljava/lang/IllegalStateException;";text="Final field write lost calibrated native access";break;
  }
  if(result){const uint64_t zero=0;std::memcpy(result,&zero,sizeof(zero));}
  boundary.raise(boundary.owner,self,type,text);return true;
}
} // namespace artdcevm::final_field_guard
