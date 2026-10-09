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
  uint32_t dex_index=UINT32_MAX;
};
// Fixed split/merged-field source candidates; only an actual owned reflection oracle
// may publish it. The source class and reflected Fields stay strongly rooted.
// The caller enters a calibrated Runnable callback, owns stable name/descriptor
// bytes, and supplies every declared field of an initialized fixture class.
enum class Model { kSplit, kMerged };
// Ephemeral permissions view of the caller's current mapping snapshot. It does
// not establish object lifetime, GC exclusion or mutator access. Caller must
// retain the same actual no-safepoint, GC-stable rooted scope for the whole read;
// never cache this context across windows. Rejection never falls back to OS maps.
struct ReadContext {
  void* owner=nullptr;
  bool (*extent)(void*,const void*,size_t*)=nullptr;
};
// Explicit view or the legacy OS lookup; a supplied view never falls back.
bool ReadMappingExtent(const ReadContext*,const void*,size_t*);
class FieldMetadata {
 public:
  bool Bind(HookEngine&, std::string*);
  bool Calibrate(const runnable_jni::Context&, jobject strong_global_class,
                 const char* expected_class_descriptor, const FieldOracle*, size_t count, std::string*);
  // Consumes the independently calibrated family without recalibrating from a
  // caller class. Public complete reflected fields + immutable DEX indices
  // must enumerate every native array member exactly, including empty arrays.
  bool VerifyProvenDeclaration(const runnable_jni::Context&,jobject strong_global_class,
      const char* expected_class_descriptor,const FieldOracle*,size_t count,std::string*)const;
  bool Calibrated() const { return calibrated_; }
  bool InterfaceDeclarationCalibrated()const{return interface_declaration_calibrated_;}
  Model StorageModel() const { return model_; }
  bool Fields(void* klass, bool is_static, field_array::View*, std::string*, const ReadContext* =nullptr) const;
  field_copy::CopyClassView Class(void* klass, bool is_static,const ReadContext* =nullptr) const;
  // Reject unsupported Class/field text instead of invoking commit invariants.
  // Caller owns real native roots and the calibrated source/mutator scope.
  bool ClassIdentity(void*,field_copy::CopyClassView*,std::string*,const ReadContext* =nullptr)const;
  // Exact independently staged public descriptor and, for a proxy terminal,
  // its public Class.getName String root/bytes. No platform string is mutated.
  bool ClassIdentityVerified(void*,const char* descriptor,void* public_proxy_name,
      const char* proxy_name,const char* negative_proxy_name,
      field_copy::CopyClassView*,std::string*,const ReadContext* =nullptr)const;
  bool FieldIdentity(void*,field_copy::CopyFieldView*,std::string*,const ReadContext* =nullptr)const;
  field_copy::CopyFieldView Field(void* klass, bool is_static, size_t ordinal,const ReadContext* =nullptr) const;
  void* FieldDeclaringClass(void* field)const;
  void* Reference(void* object, size_t offset, bool is_volatile=false) const;
 private:
  using FieldLeaf = void* (*)(void*);
  using FieldText = const char* (*)(void*);
  using ClassText = const char* (*)(void*, void*);
  using Mark = void* (*)(void*);
  using StringEquals=bool (*)(void*,const char*);
  bool Descriptor(void* klass, Model, const char** output, std::string*, const ReadContext* =nullptr) const;
  bool Candidate(const runnable_jni::Context&, void* klass, Model, const char*, const FieldOracle*, size_t, std::string*) const;
  bool FieldsCandidate(void*,Model,bool,field_array::View*,std::string*,const ReadContext* =nullptr) const;
  FieldLeaf field_leaf_ = nullptr;
  FieldText name_ = nullptr, descriptor_ = nullptr;
  ClassText class_descriptor_ = nullptr;
  StringEquals string_equals_=nullptr;
  Mark mark_ = nullptr;
  Mark root_ = nullptr;
  bool Text(void* field, Model, const char** name, const char** descriptor, std::string*, const ReadContext* =nullptr) const;
  bool bound_ = false, calibrated_ = false;
  bool named_interface_relation_=false,interface_declaration_calibrated_=false;
  Model model_ = Model::kSplit;
};
}
