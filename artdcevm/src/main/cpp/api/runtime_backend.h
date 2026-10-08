#pragma once
#include <jni.h>
#include <string>
#include <vector>

namespace artdcevm {
enum class Operation { kBody = 0, kStructure = 1, kEvolve = 2 };
struct Definition { jclass klass; std::vector<unsigned char> dex; };
class RuntimeBackend {
 public:
  virtual ~RuntimeBackend() = default;
  virtual int Attach(JavaVM* vm) = 0;
  virtual std::string Capabilities() const = 0;
  virtual int Apply(Operation op, const std::vector<Definition>& defs) = 0;
  virtual std::string Analyze(JNIEnv* jni, const std::vector<Definition>& defs) = 0;
  virtual std::string ErrorName(int error) const = 0;
  virtual int EnableValidationHooks() = 0;
  virtual int EnableStandardRedefineRouting() = 0;
  virtual int InitializeEvolution(JNIEnv* jni, jobject calibration) = 0;
  // Explicitly initializes only capabilities independently proven by the backend.
  virtual int InitializeCapabilities(JNIEnv* jni, jobject calibration) {
    return InitializeEvolution(jni, calibration);
  }
};
}  // namespace artdcevm
