#pragma once
#include "platform/common/source_handle_roots.h"
namespace artdcevm::source_cache_writer_root {
// Source callbacks are not runnable_executor bridge callbacks. Import the
// ordinary retained global through the exact Run's calibrated source owner,
// then use its collector-visible Pool slot for all post-Java root snapshots.
inline bool Import(const source_handle_roots::Service& service,
                   const runnable_jni::Context& context,source_handle_roots::Pool& pool,
                   jobject strong_writer,void** slot,std::string* error){
 auto fail=[&](const char*why){if(error)*error=why;return false;};
 if(!slot||!pool.UsesService(service))return fail("Cache writer root belongs to another source Pool owner");
 void*writer=nullptr;
 if(!service.BorrowOwnedStrongGlobal(context,strong_writer,&writer)||!writer)
  return fail("Cache writer actual source strong Class root unavailable");
 void*owned=pool.Add(writer,error);if(!owned)return false;
 *slot=owned;return true;
}
}
