#pragma once
#include "api/hook_engine.h"
#include "platform/common/field_copy.h"
#include "platform/common/runnable_jni_bridge.h"
#include "platform/common/field_array_view.h"
#include <string>
namespace artdcevm::modern_field_metadata {
struct FieldOracle {
  jobject strong_global_field;
  const char* name;
  const char* descriptor;
  uint32_t modifiers;
};
// Fixed split/merged-field source candidates; only an actual owned reflection oracle
// may publish it. The source class and reflected Fields stay strongly rooted.
// The caller enters a calibrated Runnable callback, owns stable name/descriptor
// bytes, and supplies every declared field of an initialized fixture class.
enum class Model { kSplit, kMerged };
class FieldMetadata {
 public:
  bool Bind(HookEngine&, std::string*);
  bool Calibrate(const runnable_jni::Context&, jobject strong_global_class,
                 const char* expected_class_descriptor, const FieldOracle*, size_t count, std::string*);
  bool Calibrated() const { return calibrated_; }
  Model StorageModel() const { return model_; }
  bool Fields(void* klass, bool is_static, field_array::View*, std::string*) const;
  field_copy::CopyClassView Class(void* klass, bool is_static) const;
  field_copy::CopyFieldView Field(void* klass, bool is_static, size_t ordinal) const;
  void* Reference(void* object, size_t offset, bool is_volatile=false) const;
 private:
  using FieldLeaf = void* (*)(void*);
  using FieldText = const char* (*)(void*);
  using ClassText = const char* (*)(void*, void*);
  using Mark = void* (*)(void*);
  bool Descriptor(void* klass, Model, const char** output, std::string*) const;
  bool Candidate(const runnable_jni::Context&, void* klass, Model, const char*, const FieldOracle*, size_t, std::string*) const;
  bool FieldsCandidate(void*,Model,bool,field_array::View*,std::string*) const;
  FieldLeaf field_leaf_ = nullptr;
  FieldText name_ = nullptr, descriptor_ = nullptr;
  ClassText class_descriptor_ = nullptr;
  Mark mark_ = nullptr;
  Mark root_ = nullptr;
  bool Text(void* field, Model, const char** name, const char** descriptor, std::string*) const;
  bool bound_ = false, calibrated_ = false;
  Model model_ = Model::kSplit;
};
}
