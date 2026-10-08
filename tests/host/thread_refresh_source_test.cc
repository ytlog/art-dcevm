#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "platform/common/thread_refresh.h"
using namespace artdcevm::thread_refresh;
int main() {
  SourceLayout old{9,10,11};
  assert(!SourceLayoutFor(static_cast<SourceDeclaration>(-1), &old));
  assert(old.gc_marking_offset==9 && old.suspend_trigger_offset==10 && old.prefix_extent==11);
  assert(!SourceLayoutFor(SourceDeclaration::kAndroid16,nullptr));
  for (auto declaration : {SourceDeclaration::kAndroid16, SourceDeclaration::kAndroid17}) {
    SourceLayout layout{};
    assert(SourceLayoutFor(declaration,&layout));
    assert(layout.gc_marking_offset+sizeof(uint32_t)<=layout.prefix_extent);
    assert(layout.suspend_trigger_offset+sizeof(uintptr_t*)<=layout.prefix_extent);
    assert(layout.gc_marking_offset%alignof(uint32_t)==0);
    assert(layout.suspend_trigger_offset%alignof(uintptr_t*)==0);
    assert(layout.gc_marking_offset<layout.suspend_trigger_offset);
    printf("source family=%d gc_marking=%zu suspend_trigger=%zu prefix_extent=%zu (not runtime calibrated)\n",
      static_cast<int>(declaration),layout.gc_marking_offset,layout.suspend_trigger_offset,layout.prefix_extent);
  }
}
