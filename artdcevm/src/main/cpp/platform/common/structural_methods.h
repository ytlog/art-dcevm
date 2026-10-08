#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::structural_methods {
// Provider owns ART final exclusive mutator boundary and independently validates
// all source layouts. These operations neither allocate nor issue JNI requests.
class Services {
 public:
  virtual ~Services()=default;
  virtual size_t Count(void* klass) const=0;
  virtual void* At(void* klass,size_t index) const=0;
  virtual uint32_t Flags(void* method) const=0;
  virtual void* DeclaringClass(void* method) const=0;
  virtual bool SameIdentity(void* a,void* b) const=0;
  virtual bool IsRetired(void* method) const=0;
  virtual void TransferNativeBinding(void* source,void* target) const=0;
  virtual void AddFlags(void* method,uint32_t flags) const=0;
  virtual void SetObsoleteEntry(void* method) const=0;
};
void* FindReplacement(const Services&,void* old_method,void* new_class);
void Update(const Services&,void* old_class,void* new_class);
}
