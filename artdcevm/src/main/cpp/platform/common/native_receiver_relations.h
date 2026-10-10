#pragma once
#include <cstddef>
#include <cstdint>
namespace artdcevm::receiver_guard {
enum class ClassKind:uint8_t {Ordinary,Array,Proxy};
struct ClassView {void* parent=nullptr;void* loader=nullptr;const char* descriptor=nullptr;uint32_t flags=0;size_t interface_count=0;size_t direct_count=0;ClassKind kind=ClassKind::Ordinary;};
// Every callback reads live native roots under the actual calibrated mutator
// scope. InterfaceAt reads the real complete IfTable, not DEX direct interfaces.
// Obsolete is the named fixed Class::GetObsoleteClass carrier. Reads may not
// suspend/allocate/JNI; truncated/foreign/cyclic metadata must return failure.
struct ClassAccess {
 void* owner=nullptr;
 bool (*read)(void*,void*,ClassView*)noexcept=nullptr;
 bool (*interface_at)(void*,void*,size_t,void**)noexcept=nullptr;
 bool (*direct_at)(void*,void*,size_t,void**)noexcept=nullptr;
 bool (*obsolete)(void*,void*,void**)noexcept=nullptr;
};
class Relations final {
 public:
 explicit Relations(ClassAccess access):access_(access){}
 bool Normalize(void* declaring,void* receiver_class,void** current)const noexcept;
 bool Assignable(void* declaring,void* receiver_class,bool*)const noexcept;
 bool Special(void* declaring,void* caller,bool*)const noexcept;
 private:
 bool Next(void* klass,bool history,void** next)const noexcept;
 bool Acyclic(void*,bool history)const noexcept;
 bool Historical(void* current,void* declaring,bool*)const noexcept;
 ClassAccess access_;
};
}
