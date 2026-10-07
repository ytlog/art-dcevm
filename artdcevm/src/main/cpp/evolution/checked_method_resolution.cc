#include "evolution/checked_method_resolution.h"

namespace artdcevm::evolution {
void* ResolveCheckedMethod(const MethodResolutionContext& context, CheckedMethodResolution& source) {
  void* method=source.CachedMethod(context);
  if(!source.ResolveAndRootClass(context) || source.RejectClassInvokeType(context))return nullptr;
  if(!method)method=source.FindResolvedMethod(context);
  if(method && context.referrer && source.RejectAccess(context,method))return nullptr;
  if(method && !source.RejectMethodInvokeType(context,method))return method;
  if(!method)method=source.FindIncompatibleMethod(context);
  if(method)source.ThrowInvokeMismatch(context,method);
  else source.ThrowMissingMethod(context);
  return nullptr;
}
}
