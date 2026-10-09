#pragma once
#include <cstddef>
#include <vector>
namespace artdcevm::prepared_roots {
// Registration runs in the Collected owner window, while native references
// are live. The owner converts them into its actual strong, GC-updated roots.
struct Registrar {void* owner=nullptr;bool (*add)(void*,void*,size_t*)=nullptr;};
// Final resolution is a fixed indexed root read: no JNI, allocation or safepoint.
struct Resolver {void* owner=nullptr;void* (*get)(void*,size_t) noexcept=nullptr;};
class Bindings final {
 public:
  bool Add(void** location,const Registrar& registrar){
    if(sealed_||!location||!registrar.add)return false;
    if(!*location)return true; // Actual bootstrap-loader/null-parent identity.
    size_t slot=0;if(!registrar.add(registrar.owner,*location,&slot))return false;
    entries_.push_back({location,slot});return true;
  }
  void Seal() noexcept {sealed_=true;}
  bool CanRefresh(const Resolver& resolver)const noexcept {
    if(!sealed_||!resolver.get)return false;
    for(const auto& entry:entries_)if(!resolver.get(resolver.owner,entry.slot))return false;
    return true;
  }
  // The owner validates every prepared participant before applying any refresh.
  // Indexed roots are immutable throughout this original disabled-GC window.
  void Refresh(const Resolver& resolver) noexcept {
    for(const auto& entry:entries_)*entry.location=resolver.get(resolver.owner,entry.slot);
  }
 private:
  struct Entry {void** location;size_t slot;};
  std::vector<Entry> entries_;
  bool sealed_=false;
};
}
