#pragma once
#include "api/runtime_backend.h"
#include "evolution/migration_plan.h"
#include "jvmti.h"
namespace artdcevm::evolution_planner {
struct BatchPlan {
  std::vector<evolution::MigrationPlan> classes;
  uint32_t required_features = 0;
};
int PlanEvolution(jvmtiEnv* env, JNIEnv* jni, const std::vector<Definition>& definitions,
                  BatchPlan* output, std::string* error);
std::string DescribeBatchPlan(const BatchPlan& plan);
}
