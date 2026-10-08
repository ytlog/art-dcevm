#pragma once
#include "api/runtime_backend.h"
#include <memory>
#include <jvmti.h>
namespace artdcevm::production_bootstrap {
class Engine {
 public:
  virtual ~Engine()=default;
  virtual bool Source13Candidate()const=0;
  virtual void RecordDefinitions(JNIEnv*,const std::vector<Definition>&){}
  virtual bool FieldDeletionReady()const{return false;}
  virtual bool Activate(JNIEnv*,std::string*)=0;
  virtual int Apply(JNIEnv*,jvmtiEnv*,const std::vector<Definition>&,std::string*)=0;
};
// Only original installed APK DEX and public JVMTI/reflection observations may
// construct this engine. A source-family label never authorizes any private ABI.
std::unique_ptr<Engine> Prepare(JNIEnv*,jvmtiEnv*,jobject,std::string*);
}
