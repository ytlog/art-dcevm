#pragma once
#include <jni.h>
#include <jvmti.h>
#include <string>
#include <vector>
#include "evolution/class_schema.h"
#include "platform/common/class_layout.h"
#include "platform/common/runtime_thread.h"
#include "platform/abi/arm64/common/redefinition.h"

// java.lang.reflect.Proxy classes that implement a redefined interface
// (docs/STRUCTURAL_REDEFINE_ENGINE.md, "Proxy 实现类"). A proxy class has no
// DEX for ART to re-link, and its instances need no migration: it stays, and
// at commit its IfTable entry for the interface is replaced by one built for
// the new interface. Each new method maps to the proxy method of the same name
// and descriptor, so the invocation handler keeps receiving it, now as the new
// interface method; a method the proxy never had maps to the new declaration
// itself (an abstract one throws AbstractMethodError, a default runs its body),
// as a JVM's re-linked proxy class does. Parent interfaces the target adds
// join the proxy's IfTable the same way. The proxy's IMT is sealed
// (imt_sealing.h) because the new methods may occupy other slots.
namespace artdcevm::proxy_relink {
// An interface target of the batch.
struct Interface {
  jclass klass;
  const evolution::ClassSchema* live;
  const jvmtiClassDefinition* definition;
};
// Resolves the named services; failure leaves Proxy implementations rejected.
bool Install(const class_layout::Services& services, std::string* failure);
bool Available();
// Native: for each loaded proxy class and each interface target it implements,
// a long[] sized for the new interface's IfTable methods. A target that adds
// parent interfaces gives the proxy a new IfTable with an entry for each one
// it does not implement yet, superinterfaces first. A target that drops a
// parent interface is rejected, unlike JBR: the proxy would also need its
// IfTable entries and the references it fills dropped.
jvmtiError Prepare(JNIEnv* jni, const std::vector<jclass>& proxies, const std::vector<Interface>& interfaces,
                   std::string* error);
// Before ART's no-failure point, match each interface by Class identity in
// the structural owner's class arrays, fill its Proxy arrays and build IMTs.
bool Plan(const runtime_thread::VerifiedThread& thread, abi::arm64::ClassRedefinitionPrefix* self,
          const abi::arm64::RedefinitionDataIter& data, int selected, std::string* why);
// At commit, after ART replaced the interface's references: install the
// arrays, point the proxy methods at their new interface methods and switch
// to the sealed IMTs. No JNI: every thread is suspended.
void Apply(abi::arm64::ClassRedefinitionPrefix* self, const abi::arm64::RedefinitionDataIter& data) noexcept;
}  // namespace artdcevm::proxy_relink
