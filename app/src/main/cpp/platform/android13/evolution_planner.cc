#include "platform/android13/evolution_planner.h"
#include "platform/android13/schema_snapshot.h"
#include <sstream>
namespace artdcevm::android13 {
int PlanEvolution(jvmtiEnv* env, JNIEnv* jni, const std::vector<Definition>& definitions,
                  BatchPlan* output, std::string* error) {
  if (definitions.empty()) { *error = "Empty patch batch"; return JVMTI_ERROR_ILLEGAL_ARGUMENT; }
  BatchPlan batch;
  for (size_t index = 0; index < definitions.size(); ++index) {
    const auto& definition = definitions[index];
    for (size_t prior = 0; prior < index; ++prior) {
      if (jni->IsSameObject(definitions[prior].klass, definition.klass)) {
        *error = "Same live class occurs twice in patch batch";
        return JVMTI_ERROR_ILLEGAL_ARGUMENT;
      }
    }
    evolution::ClassSchema before, after;
    if (!evolution::ReadDexSchema(definition.dex, &after, error)) return JVMTI_ERROR_INVALID_CLASS_FORMAT;
    int result = ReadLiveSchema(env, jni, definition.klass, &before, error);
    if (result) return result;
    if (before.descriptor != after.descriptor) { *error = "Target and patch class names differ"; return JVMTI_ERROR_NAMES_DONT_MATCH; }
    evolution::MigrationPlan migration;
    if (!evolution::PlanMigration(before, after, &migration, error)) return JVMTI_ERROR_ILLEGAL_ARGUMENT;
    batch.required_features |= migration.required_features;
    batch.classes.push_back(std::move(migration));
  }
  *output = std::move(batch); error->clear(); return JVMTI_ERROR_NONE;
}
std::string DescribeBatchPlan(const BatchPlan& plan) {
  std::ostringstream out;
  out << "{\"requiredFeatures\":" << plan.required_features << ",\"classes\":[";
  for (size_t i = 0; i < plan.classes.size(); ++i) {
    if (i) out << ',';
    out << evolution::DescribeMigration(plan.classes[i]);
  }
  return out.str() + "]}";
}
}
