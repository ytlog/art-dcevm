#pragma once
#include "api/hook_engine.h"
#include <jvmti.h>
#include <cstdint>
#include <string>
namespace artdcevm::android13 {
bool ConfigureEvolutionTransactions(HookEngine&,void* implementation,std::string* error);
// SourceTransactionScope and target global JNI roots surround this complete
// call. ART's named original owns transforms, allocation, linking and commit.
jvmtiError RedefineEvolutionTransaction(jvmtiEnv*,jint,const jvmtiClassDefinition*,bool allow_transform_hierarchy);
bool NeedsEvolutionReceiverGuard(jint,const jvmtiClassDefinition*);
bool DrainEvolutionCompiledFrames();
uint64_t EvolutionGuardTransactionDiagnostics(int index);
uint64_t EvolutionCompiledPrecommitDiagnostics(int index);
}
