#include "platform/common/production_bootstrap.h"
#include "platform/common/contextual_field_id.h"
#include <cassert>
#include <iostream>
namespace artdcevm::contextual_field_id {
std::shared_ptr<Proof> Prepare(JNIEnv*,jobject,jfieldID,std::string*){std::abort();}
void Release(JNIEnv*,std::shared_ptr<Proof>*){std::abort();}
}
int main(){
 using artdcevm::production_bootstrap::Prepare;
 // Deliberately unusable JNI/JVMTI handles prove the rejected initialization
 // never calls into VM, installs hooks or starts a calibration transaction.
 auto* env=reinterpret_cast<JNIEnv*>(uintptr_t(1));
 auto* ti=reinterpret_cast<jvmtiEnv*>(uintptr_t(3));
 auto calibration=reinterpret_cast<jobject>(uintptr_t(5));
 std::string error="old message";
 assert(!Prepare(env,ti,calibration,&error));
 assert(error.find("transaction callgraph bootstrap is not integrated")!=std::string::npos);
 assert(error.find("public JVMTI body/additive operations remain available")!=std::string::npos);
 assert(!Prepare(nullptr,nullptr,nullptr,nullptr));
 assert(!Prepare(env,ti,calibration,&error));
 std::cout<<"Actual production bootstrap: fail-before-JNI/calibration/mutation, no private owner, precise dependency diagnostic PASS\n";
}
