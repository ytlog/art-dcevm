#include "platform/common/native_source_stop.h"
// The ordinary pipeline has a doubled CleanupOwner and no continuous source
// Session. Deny that boundary; the aggregate links the real source-stop TU.
namespace artdcevm::native_source_stop {
bool Session::CanPublishDex(void*,const void*,void*)const noexcept{return false;}
}
