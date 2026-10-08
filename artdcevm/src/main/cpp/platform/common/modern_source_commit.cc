#include "platform/common/modern_source_commit.h"
namespace artdcevm::modern_source_commit {
namespace {
void RestoreMethods(const structural_methods::Services* service) {
  static_cast<const modern_methods::Service*>(service)->RestoreOldDeclaringClasses();
}
}
std::unique_ptr<Services> Prepare(HookEngine& hooks,const Dependencies& dependencies,
    const source_commit::Plan& plan,const std::vector<const modern_methods::Service*>& methods,
    std::string* error) {
  if(methods.size()!=plan.old_classes.size()||methods.size()!=plan.new_classes.size()) {
    if(error)*error="Modern commit requires every actual prepared method table";
    return nullptr;
  }
  std::vector<native_source_commit::MethodPair> pairs;
  pairs.reserve(methods.size());
  for(size_t i=0;i<methods.size();++i)
    pairs.push_back({plan.old_classes[i],plan.new_classes[i],methods[i],RestoreMethods});
  auto native=dependencies;
  native.native_roots=native_source_commit::NativeRootPolicy::kRestore;
  return Services::Prepare(hooks,native,plan,std::move(pairs),error);
}
}
