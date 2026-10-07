#include "evolution/checked_method_resolution.h"

namespace artdcevm::evolution {
namespace {
void* Resolve(const MethodResolutionContext& context, CheckedMethodResolution& source, bool cache_first) {
  void* method=cache_first?source.CachedMethod(context):nullptr;
  if(!source.ResolveAndRootClass(context) || source.RejectClassInvokeType(context))return nullptr;
  if(!cache_first)method=source.CachedMethod(context);
  if(!method)method=source.FindResolvedMethod(context);
  if(method && context.referrer && source.RejectAccess(context,method))return nullptr;
  if(method && !source.RejectMethodInvokeType(context,method))return method;
  if(!method)method=source.FindIncompatibleMethod(context);
  if(method)source.ThrowInvokeMismatch(context,method);
  else source.ThrowMissingMethod(context);
  return nullptr;
}
}
void* ResolveCheckedMethod(const MethodResolutionContext& context, CheckedMethodResolution& source) {
  return Resolve(context,source,true);
}
void* ResolveCheckedReferrerMethod(const MethodResolutionContext& context, CheckedMethodResolution& source) {
  return Resolve(context,source,false);
}
}
