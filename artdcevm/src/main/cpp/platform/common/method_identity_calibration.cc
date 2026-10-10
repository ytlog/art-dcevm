#include "platform/common/method_identity_calibration.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <algorithm>
#include <cstdlib>
#if !defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif
#include "platform/common/readable_mapping_scope.h"
#include "platform/common/proc_mapping_region.h"
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

namespace artdcevm::method_identity {
namespace {
thread_local ScopedReadableMappings* active_mappings=nullptr;
thread_local uint64_t mapping_os_queries=0;
uintptr_t LookupAddress(const void* address){
  uintptr_t pointer=reinterpret_cast<uintptr_t>(address);
#if defined(__ANDROID__) && defined(__aarch64__)
  pointer &= UINT64_C(0x00ffffffffffffff);
#endif
  return pointer;
}
bool ReadRegion(uintptr_t pointer,std::pair<uintptr_t,uintptr_t>* region){
 ++mapping_os_queries;
#if defined(__APPLE__)
  mach_vm_address_t start=pointer;mach_vm_size_t size=0;
  vm_region_basic_info_data_64_t info{};mach_msg_type_number_t length=VM_REGION_BASIC_INFO_COUNT_64;
  mach_port_t object=MACH_PORT_NULL;
  const auto status=mach_vm_region(mach_task_self(),&start,&size,VM_REGION_BASIC_INFO_64,
      reinterpret_cast<vm_region_info_t>(&info),&length,&object);
  if(object!=MACH_PORT_NULL)mach_port_deallocate(mach_task_self(),object);
  if(status!=KERN_SUCCESS||!(info.protection&VM_PROT_READ)||pointer<start||pointer-start>=size||size>UINTPTR_MAX-start)return false;
  *region={uintptr_t(start),uintptr_t(start+size)};return true;
#else
  const int fd=open("/proc/self/maps",O_RDONLY|O_CLOEXEC);if(fd<0)return false;
  // Direct syscalls and fixed stack storage also serve final no-allocation
  // validation. stdio allocates its FILE and read buffer even without C++ new.
  auto match=[&](const char* line,size_t length){
    proc_mapping_region::Region parsed;
    return proc_mapping_region::Parse(line,length,&parsed)&&parsed.readable&&
      pointer>=parsed.begin&&pointer<parsed.end&&
      (*region=std::pair<uintptr_t,uintptr_t>{parsed.begin,parsed.end},true);
  };
  char chunk[4096],line[1024];size_t length=0;bool found=false;
  for(ssize_t n;(n=read(fd,chunk,sizeof(chunk)))>0&&!found;){
    for(ssize_t i=0;i<n;++i){if(chunk[i]=='\n'){
        if(match(line,length)){found=true;break;}length=0;
      }else if(length<sizeof(line))line[length++]=chunk[i];
    }
  }
  close(fd);return found;
#endif
}
}
uint64_t MappingOsQueries()noexcept{return mapping_os_queries;}
bool ImmediateReadableRegions::Extent(const void* address,size_t* extent)noexcept{
 if(extent)*extent=0;
 if(!address||!extent)return false;
 const uintptr_t pointer=LookupAddress(address);
 for(size_t i=0;i<count_;++i){const auto& region=regions_[i];
  if(pointer>=region.first&&pointer<region.second){*extent=size_t(region.second-pointer);return true;}
 }
 std::pair<uintptr_t,uintptr_t> region{};++os_queries_;
 if(!ReadRegion(pointer,&region)||region.first>pointer||pointer>=region.second)return false;
 // A bounded FIFO retains only kernel-proven ranges in this immediate read
 // interval. Eviction causes a fresh OS lookup, never a fabricated extent.
 regions_[next_]=region;next_=(next_+1)%kCapacity;
 if(count_<kCapacity)++count_;
 *extent=size_t(region.second-pointer);return true;
}

ScopedReadableMappings::ScopedReadableMappings() noexcept:previous_(active_mappings){
#if defined(__APPLE__)
  try {
   mach_vm_address_t start=0;
   for(;;){mach_vm_size_t size=0;vm_region_basic_info_data_64_t info{};
    mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object=MACH_PORT_NULL;
    ++mapping_os_queries;++os_queries_;
    const auto status=mach_vm_region(mach_task_self(),&start,&size,VM_REGION_BASIC_INFO_64,reinterpret_cast<vm_region_info_t>(&info),&count,&object);
    if(object!=MACH_PORT_NULL)mach_port_deallocate(mach_task_self(),object);
    if(status==KERN_INVALID_ADDRESS){complete_=true;break;}
    if(status!=KERN_SUCCESS||!size||size>UINTPTR_MAX-start)break;
    if(info.protection&VM_PROT_READ)regions_.emplace_back(uintptr_t(start),uintptr_t(start+size));
    start+=size;
   }
  }catch(...){complete_=false;regions_.clear();}
#else
  ++mapping_os_queries;++os_queries_;FILE* maps=std::fopen("/proc/self/maps","r");
  if(maps){try{char line[1024];
    while(std::fgets(line,sizeof(line),maps)){
      proc_mapping_region::Region parsed;
      if(proc_mapping_region::Parse(line,std::strlen(line),&parsed)&&parsed.readable)
        regions_.emplace_back(parsed.begin,parsed.end);
    }
    complete_=std::ferror(maps)==0;
   }catch(...){complete_=false;regions_.clear();}
   std::fclose(maps);
  }
#endif
  active_mappings=this;
}
ScopedReadableMappings::~ScopedReadableMappings(){if(active_mappings!=this)std::abort();active_mappings=previous_;}
bool ScopedReadableMappings::Extent(const void* address,size_t* extent)const noexcept {
 if(extent)*extent=0;
 if(active_mappings!=this||!complete_||!address||!extent)return false;
 const uintptr_t pointer=LookupAddress(address);
 auto at=std::upper_bound(regions_.begin(),regions_.end(),pointer,[](uintptr_t p,const auto&r){return p<r.first;});
 if(at==regions_.begin())return false;--at;
 if(pointer<at->first||pointer>=at->second)return false;*extent=size_t(at->second-pointer);return true;
}
bool ScopedReadableMappings::Find(uintptr_t pointer,size_t* extent){
  auto at=std::upper_bound(regions_.begin(),regions_.end(),pointer,
      [](uintptr_t p,const auto& region){return p<region.first;});
  if(at!=regions_.begin()){--at;if(pointer>=at->first&&pointer<at->second){*extent=size_t(at->second-pointer);return true;}}
  if(complete_)return false;
  std::pair<uintptr_t,uintptr_t> region;++os_queries_;
  if(!ReadRegion(pointer,&region))return false;
  regions_.insert(std::lower_bound(regions_.begin(),regions_.end(),region),region);
  *extent=size_t(region.second-pointer);return true;
}
bool ReadableMappingExtent(const void* address,size_t* extent){
  if(!address||!extent)return false;
  const uintptr_t pointer=LookupAddress(address);
  if(active_mappings)return active_mappings->Find(pointer,extent);
  std::pair<uintptr_t,uintptr_t> region;if(!ReadRegion(pointer,&region))return false;
  *extent=size_t(region.second-pointer);return true;
}

bool Calibrate(const runnable_jni::Context& context,
               abi::common::FromReflectedMethod named_leaf,
               const Oracle* oracles, size_t count, Calibration* output,
               std::string* error) {
  const auto fail = [&](const char* message) {
    if (error) *error = message;
    return false;
  };
  if (!context.self || !context.env || !context.vm || !named_leaf || !oracles ||
      !output || count < 3 || count > 32) return fail("Invalid owned method calibration request");
  if (context.env->ExceptionCheck()) return fail("Pending JNI exception during method calibration");
  bool has_native = false, has_instance = false, has_static = false;
  std::set<uint32_t> indices;
  for (size_t i = 0; i < count; ++i) {
    const auto& oracle = oracles[i];
    if (!oracle.strong_global_method || oracle.dex_method_index == UINT32_MAX ||
        !indices.insert(oracle.dex_method_index).second ||
        context.env->GetObjectRefType(oracle.strong_global_method) != JNIGlobalRefType)
      return fail("Method calibration requires distinct owned strong globals and DEX indices");
    if (context.env->ExceptionCheck()) return fail("JNI reference validation raised an exception");
    if (oracle.java_modifiers & 0x100u) has_native = true;
    else if (oracle.java_modifiers & 8u) has_static = true;
    else has_instance = true;
  }
  if (!has_native || !has_static || !has_instance)
    return fail("Method calibration requires native, instance and static fixture methods");
  bool matches11 = true, matches12 = true;
  Calibration candidate{};
  candidate.identities.reserve(count);
  candidate.readable_extents.reserve(count);
  std::set<const void*> methods;
  // Stable Java visibility, static, final and native bits. ART's verifier/JIT
  // flags occupy additional bits and are neither identity nor a reflection oracle.
  constexpr uint32_t kModifierMask = 0x11fu;
  const abi::common::RunnableJniScope scope{context.self, context.env, context.vm};
  for (size_t i = 0; i < count; ++i) {
    const auto& oracle = oracles[i];
    void* method = named_leaf(scope, oracle.strong_global_method);
    if (context.env->ExceptionCheck()) return fail("Named reflected-method leaf raised an exception");
    size_t extent = 0;
    if (!method || !methods.insert(method).second ||
        reinterpret_cast<uintptr_t>(method) % alignof(uint32_t) ||
        !ReadableMappingExtent(method, &extent) || extent < sizeof(method_metadata::Method12))
      return fail("Named method has no sufficient live readable mapping");
    const uint32_t flags = method_metadata::ReadAccessFlags(method);
    uint32_t index11 = UINT32_MAX, index12 = UINT32_MAX;
    if (extent >= sizeof(method_metadata::Method11))
      std::memcpy(&index11, static_cast<const unsigned char*>(method) +
          offsetof(method_metadata::Method11, dex_method_index), sizeof(index11));
    std::memcpy(&index12, static_cast<const unsigned char*>(method) +
        offsetof(method_metadata::Method12, dex_method_index), sizeof(index12));
    const bool modifiers_match = (flags & kModifierMask) == (oracle.java_modifiers & kModifierMask);
    matches11 = matches11 && modifiers_match && index11 == oracle.dex_method_index;
    matches12 = matches12 && modifiers_match && index12 == oracle.dex_method_index;
    candidate.identities.push_back({flags, oracle.dex_method_index});
    candidate.readable_extents.push_back(extent);
  }
  if (matches11 == matches12) return fail(matches11
      ? "Ambiguous ArtMethod identity declarations" : "No ArtMethod declaration matches independent oracles");
  candidate.model = matches11 ? Model::kMethod11 : Model::kMethod12Onward;
  *output = std::move(candidate);
  if (error) error->clear();
  return true;
}
}  // namespace artdcevm::method_identity
