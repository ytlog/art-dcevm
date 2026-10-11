#pragma once
#include "platform/common/production_provenance.h"
#include "platform/common/runnable_jni_bridge.h"
#include <jvmti.h>

namespace artdcevm::production_bootstrap {
// Process owner supplies immutable APK bytes and successfully recorded final
// definitions. Exact JNI loader identity gates the APK fallback. A same-name
// Class in another loader never inherits this APK oracle. All borrowed data
// owners outlive this source; operations are serialized by the engine.
class InstalledDex final {
 public:
 InstalledDex(const std::vector<std::vector<unsigned char>>&apk,Provenance&provenance)
  :apk_(apk),provenance_(provenance){}
 ~InstalledDex();InstalledDex(const InstalledDex&)=delete;InstalledDex&operator=(const InstalledDex&)=delete;
 bool Bind(JNIEnv*,jvmtiEnv*,jclass apk_anchor,std::string*);
 bool Read(const runnable_jni::Context&,jclass,const std::string&,
           std::vector<unsigned char>*,std::string*)const;
 Provenance&Records()const noexcept{return provenance_;}
 private:
 const std::vector<std::vector<unsigned char>>&apk_;
 Provenance&provenance_;
 JavaVM*vm_=nullptr;jvmtiEnv*ti_=nullptr;jobject loader_=nullptr;
};
}
